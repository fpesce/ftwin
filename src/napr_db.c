/**
 * @file napr_db.c
 * @brief Implementation of napr_db storage engine
 *
 * LMDB-style embedded key-value store with:
 * - Memory-mapped I/O and zero-copy reads
 * - B+ Tree indexing with Copy-on-Write transactions
 * - MVCC for lock-free reads
 * - Single-writer/multiple-reader concurrency (SWMR)
 */

#include "napr_db_internal.h"
#include <apr_file_io.h>
#include <apr_mmap.h>
#include <apr_thread_mutex.h>
#include <apr_proc_mutex.h>
#include <apr_global_mutex.h>
#include <apr_portable.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Create a database environment handle.
 *
 * Allocates and initializes a new environment handle.
 * The environment must be configured with napr_db_env_set_mapsize()
 * and opened with napr_db_env_open() before use.
 *
 * @param env Pointer to receive the environment handle
 * @param pool APR pool for allocations
 * @return APR_SUCCESS or error code
 */
apr_status_t napr_db_env_create(napr_db_env_t **env, apr_pool_t *pool)
{
    if (!env || !pool) {
        return APR_EINVAL;
    }

    /* Allocate environment structure from pool */
    napr_db_env_t *env_handle = apr_pcalloc(pool, sizeof(napr_db_env_t));
    if (!env_handle) {
        return APR_ENOMEM;
    }

    /* Initialize fields */
    env_handle->pool = pool;
    env_handle->mapsize = 0;
    env_handle->flags = 0;
    env_handle->file = NULL;
    env_handle->mmap = NULL;
    env_handle->map_addr = NULL;
    env_handle->meta0 = NULL;
    env_handle->meta1 = NULL;
    env_handle->live_meta = NULL;
    env_handle->writer_thread_mutex = NULL;
    env_handle->writer_proc_mutex = NULL;

    *env = env_handle;
    return APR_SUCCESS;
}

/**
 * @brief Set the memory map size for the database.
 *
 * Must be called before napr_db_env_open(). The size should be large
 * enough to accommodate the expected database growth.
 *
 * @param env Database environment handle
 * @param size Memory map size in bytes
 * @return APR_SUCCESS or error code
 */
apr_status_t napr_db_env_set_mapsize(napr_db_env_t *env, apr_size_t size)
{
    if (!env) {
        return APR_EINVAL;
    }

    if (size == 0) {
        return APR_EINVAL;
    }

    env->mapsize = size;
    return APR_SUCCESS;
}

/**
 * @brief Initialize meta page with default values.
 *
 * @param meta Pointer to meta page in memory
 * @param txnid Transaction ID for this meta page
 */
static void init_meta_page(DB_MetaPage *meta, txnid_t txnid)
{
    memset(meta, 0, sizeof(DB_MetaPage));
    meta->magic = DB_MAGIC;
    meta->version = DB_VERSION;
    meta->txnid = txnid;
    meta->root = 0;             /* No root page yet (empty tree) */
    meta->last_pgno = 1;        /* Pages 0 and 1 are meta pages */
    meta->free_db_root = 0;     /* No Free DB root page yet (empty tree) */
}

/**
 * @brief Validate a meta page.
 *
 * Checks magic number and version to ensure the meta page is valid.
 *
 * @param meta Pointer to meta page
 * @return APR_SUCCESS if valid, APR_EINVAL otherwise
 */
static apr_status_t validate_meta_page(const DB_MetaPage *meta)
{
    if (meta->magic != DB_MAGIC) {
        return APR_EINVAL;
    }
    if (meta->version != DB_VERSION) {
        return APR_EINVAL;
    }
    return APR_SUCCESS;
}

/**
 * @brief Select the live meta page (highest valid TXNID).
 *
 * @param env Database environment
 * @return APR_SUCCESS or error code
 */
static apr_status_t select_live_meta(napr_db_env_t *env)
{
    apr_status_t status0 = APR_SUCCESS;
    apr_status_t status1 = APR_SUCCESS;

    status0 = validate_meta_page(env->meta0);
    status1 = validate_meta_page(env->meta1);

    /* Both invalid - database is corrupted */
    if (status0 != APR_SUCCESS && status1 != APR_SUCCESS) {
        return APR_EINVAL;
    }

    /* Only meta0 is valid */
    if (status0 == APR_SUCCESS && status1 != APR_SUCCESS) {
        env->live_meta = env->meta0;
        return APR_SUCCESS;
    }

    /* Only meta1 is valid */
    if (status0 != APR_SUCCESS && status1 == APR_SUCCESS) {
        env->live_meta = env->meta1;
        return APR_SUCCESS;
    }

    /* Both valid - select highest TXNID */
    if (env->meta0->txnid > env->meta1->txnid) {
        env->live_meta = env->meta0;
    }
    else {
        env->live_meta = env->meta1;
    }

    return APR_SUCCESS;
}

/**
 * @brief Open or create the database file.
 *
 * @param env Database environment
 * @param path Path to database file
 * @param[out] is_new_file Set to 1 if the file was created, 0 otherwise
 * @return APR_SUCCESS or error code
 */
static apr_status_t db_file_open(napr_db_env_t *env, const char *path, int *is_new_file)
{
    apr_status_t status = APR_SUCCESS;
    apr_int32_t file_flags = 0;

    if (env->flags & NAPR_DB_RDONLY) {
        file_flags = APR_READ | APR_BINARY;
    }
    else {
        file_flags = APR_READ | APR_WRITE | APR_BINARY;
    }

    apr_finfo_t finfo;
    status = apr_stat(&finfo, path, APR_FINFO_SIZE, env->pool);

    if (status != APR_SUCCESS && status != APR_ENOENT) {
        return status;
    }

    if (status == APR_SUCCESS) {
        *is_new_file = 0;
        return apr_file_open(&env->file, path, file_flags, APR_OS_DEFAULT, env->pool);
    }

    /* File doesn't exist */
    if (!(env->flags & NAPR_DB_CREATE)) {
        return APR_ENOENT;
    }

    *is_new_file = 1;
    file_flags |= APR_CREATE | APR_TRUNCATE;
    status = apr_file_open(&env->file, path, file_flags, APR_FPROT_UREAD | APR_FPROT_UWRITE, env->pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    apr_off_t min_size = (apr_off_t) 2 * PAGE_SIZE;
    status = apr_file_trunc(env->file, min_size);
    if (status != APR_SUCCESS) {
        apr_file_close(env->file);
        env->file = NULL;
    }
    return status;
}

/**
 * @brief Memory map the database file.
 *
 * @param env Database environment
 * @return APR_SUCCESS or error code
 */
static apr_status_t db_mmap_file(napr_db_env_t *env)
{
    apr_status_t status = APR_SUCCESS;

    status = apr_mmap_create(&env->mmap, env->file, 0, env->mapsize, APR_MMAP_READ | ((env->flags & NAPR_DB_RDONLY) ? 0 : APR_MMAP_WRITE), env->pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = apr_mmap_offset(&env->map_addr, env->mmap, 0);
    if (status != APR_SUCCESS) {
        apr_mmap_delete(env->mmap);
        env->mmap = NULL;
    }

    return status;
}

/**
 * @brief Initialize or validate meta pages.
 *
 * @param env Database environment
 * @param is_new_file 1 if the database is new, 0 otherwise
 * @return APR_SUCCESS or error code
 */
static apr_status_t db_init_meta(napr_db_env_t *env, int is_new_file)
{
    env->meta0 = (DB_MetaPage *) env->map_addr;
    env->meta1 = (DB_MetaPage *) ((char *) env->map_addr + PAGE_SIZE);

    if (is_new_file) {
        init_meta_page(env->meta0, 0);
        init_meta_page(env->meta1, 1);

        apr_status_t status = apr_file_sync(env->file);
        if (status != APR_SUCCESS) {
            return status;
        }
        env->live_meta = env->meta1;
        return APR_SUCCESS;
    }

    return select_live_meta(env);
}

/**
 * @brief Initialize the writer lock.
 *
 * @param env Database environment
 * @return APR_SUCCESS or error code
 */
static apr_status_t db_init_writer_lock(napr_db_env_t *env)
{
    if (env->flags & NAPR_DB_INTRAPROCESS_LOCK) {
        env->writer_proc_mutex = NULL;
        return apr_thread_mutex_create(&env->writer_thread_mutex, APR_THREAD_MUTEX_DEFAULT, env->pool);
    }

    env->writer_thread_mutex = NULL;
    return apr_proc_mutex_create(&env->writer_proc_mutex, NULL, APR_LOCK_DEFAULT, env->pool);
}

/**
 * @brief Open a database environment.
 *
 * Opens and memory-maps the database file. If the file doesn't exist
 * and NAPR_DB_CREATE is set, creates and initializes a new database.
 *
 * @param env Database environment handle
 * @param path Path to database file
 * @param flags Flags (NAPR_DB_CREATE, NAPR_DB_RDONLY, NAPR_DB_INTRAPROCESS_LOCK)
 * @return APR_SUCCESS or error code
 */
apr_status_t napr_db_env_open(napr_db_env_t *env, const char *path, unsigned int flags)
{
    apr_status_t status = APR_SUCCESS;
    int is_new_file = 0;

    if (!env || !path) {
        return APR_EINVAL;
    }

    if (env->mapsize == 0) {
        return APR_EINVAL;      /* Must call set_mapsize first */
    }

    env->flags = flags;

    status = db_file_open(env, path, &is_new_file);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = db_mmap_file(env);
    if (status != APR_SUCCESS) {
        apr_file_close(env->file);
        env->file = NULL;
        return status;
    }

    status = db_init_meta(env, is_new_file);
    if (status != APR_SUCCESS) {
        apr_mmap_delete(env->mmap);
        env->mmap = NULL;
        env->map_addr = NULL;
        apr_file_close(env->file);
        env->file = NULL;
        return status;
    }

    status = db_init_writer_lock(env);
    if (status != APR_SUCCESS) {
        apr_mmap_delete(env->mmap);
        env->mmap = NULL;
        env->map_addr = NULL;
        apr_file_close(env->file);
        env->file = NULL;
        return status;
    }

    /* Initialize reader tracking table for MVCC */
    memset(env->reader_table, 0, sizeof(env->reader_table));
    status = apr_thread_mutex_create(&env->reader_table_mutex, APR_THREAD_MUTEX_DEFAULT, env->pool);
    if (status != APR_SUCCESS) {
        apr_mmap_delete(env->mmap);
        env->mmap = NULL;
        env->map_addr = NULL;
        apr_file_close(env->file);
        env->file = NULL;
        return status;
    }

    return status;
}

/**
 * @brief Close a database environment.
 *
 * Unmaps memory and closes file handles. Any open transactions or
 * cursors must be closed before calling this.
 *
 * @param env Database environment handle
 * @return APR_SUCCESS or error code
 */
apr_status_t napr_db_env_close(napr_db_env_t *env)
{
    if (!env) {
        return APR_EINVAL;
    }

    /* Delete memory map if it exists */
    if (env->mmap) {
        apr_mmap_delete(env->mmap);
        env->mmap = NULL;
        env->map_addr = NULL;
        env->meta0 = NULL;
        env->meta1 = NULL;
        env->live_meta = NULL;
    }

    /* Close file if it's open */
    if (env->file) {
        apr_file_close(env->file);
        env->file = NULL;
    }

    return APR_SUCCESS;
}

/*
 * Internal helper functions for transaction synchronization
 */

/**
 * @brief Acquire the writer lock.
 *
 * Implements SWMR by serializing write transactions using the
 * appropriate mutex type (thread or process mutex).
 *
 * @param env Database environment
 * @return APR_SUCCESS or error code
 */
static apr_status_t db_writer_lock(napr_db_env_t *env)
{
    if (env->writer_thread_mutex) {
        /* Intra-process locking */
        return apr_thread_mutex_lock(env->writer_thread_mutex);
    }
    if (env->writer_proc_mutex) {
        /* Inter-process locking */
        return apr_proc_mutex_lock(env->writer_proc_mutex);
    }
    /* No mutex initialized - this should not happen */
    return APR_EINVAL;
}

/**
 * @brief Release the writer lock.
 *
 * @param env Database environment
 * @return APR_SUCCESS or error code
 */
static apr_status_t db_writer_unlock(napr_db_env_t *env)
{
    if (env->writer_thread_mutex) {
        /* Intra-process locking */
        return apr_thread_mutex_unlock(env->writer_thread_mutex);
    }
    if (env->writer_proc_mutex) {
        /* Inter-process locking */
        return apr_proc_mutex_unlock(env->writer_proc_mutex);
    }
    /* No mutex initialized - this should not happen */
    return APR_EINVAL;
}

/**
 * @brief Get the oldest active reader TXNID from the reader tracking table.
 *
 * Scans the reader table to find the minimum TXNID among all active readers.
 * This is used to determine which pages can be safely reclaimed (pages freed
 * by transactions older than the oldest reader are no longer visible).
 *
 * @param env Database environment
 * @param oldest_txnid_out Output: oldest active reader TXNID (0 if no active readers)
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_get_oldest_reader_txnid(napr_db_env_t *env, txnid_t *oldest_txnid_out)
{
    apr_status_t status = APR_SUCCESS;
    txnid_t oldest_txnid = 0;
    int slot_idx = 0;

    if (!env || !oldest_txnid_out) {
        return APR_EINVAL;
    }

    status = apr_thread_mutex_lock(env->reader_table_mutex);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Scan all slots to find minimum active TXNID */
    for (slot_idx = 0; slot_idx < MAX_READERS; slot_idx++) {
        txnid_t slot_txnid = env->reader_table[slot_idx].txnid;
        if (slot_txnid > 0) {
            if (oldest_txnid == 0 || slot_txnid < oldest_txnid) {
                oldest_txnid = slot_txnid;
            }
        }
    }

    apr_thread_mutex_unlock(env->reader_table_mutex);

    *oldest_txnid_out = oldest_txnid;
    return APR_SUCCESS;
}

/*
 * Transaction API implementation
 */

/**
 * @brief Begin a transaction.
 *
 * Starts a new read or write transaction. Write transactions acquire
 * the writer lock (SWMR enforcement). Read transactions are lock-free
 * and operate on a snapshot.
 *
 * @param env Database environment
 * @param flags Transaction flags (NAPR_DB_RDONLY for read-only)
 * @param txn Pointer to receive transaction handle
 * @return APR_SUCCESS or error code
 */
apr_status_t napr_db_txn_begin(napr_db_env_t *env, unsigned int flags, napr_db_txn_t **txn)
{
    apr_status_t status = APR_SUCCESS;
    napr_db_txn_t *txn_handle = NULL;
    apr_pool_t *txn_pool = NULL;
    int is_write = !(flags & NAPR_DB_RDONLY);

    if (!env || !txn) {
        return APR_EINVAL;
    }

    /* Create a child pool for the transaction */
    status = apr_pool_create(&txn_pool, env->pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Allocate transaction handle */
    txn_handle = apr_pcalloc(txn_pool, sizeof(napr_db_txn_t));
    if (!txn_handle) {
        apr_pool_destroy(txn_pool);
        return APR_ENOMEM;
    }

    /* If write transaction, acquire writer lock (SWMR enforcement) */
    if (is_write) {
        status = db_writer_lock(env);
        if (status != APR_SUCCESS) {
            apr_pool_destroy(txn_pool);
            return status;
        }
    }

    /* Initialize transaction */
    txn_handle->env = env;
    txn_handle->pool = txn_pool;
    txn_handle->flags = flags;

    /* Capture snapshot from current live meta page */
    txn_handle->txnid = env->live_meta->txnid;
    txn_handle->root_pgno = env->live_meta->root;
    txn_handle->free_db_root_pgno = env->live_meta->free_db_root;

    /* Initialize dirty page tracking and allocation state */
    txn_handle->dirty_pages = NULL;
    txn_handle->new_last_pgno = env->live_meta->last_pgno;
    txn_handle->freed_pages = NULL;

    /* If write transaction, increment TXNID and create dirty pages tracker */
    if (is_write) {
        txn_handle->txnid++;

        /* Create hash table for tracking dirty pages (CoW) */
        txn_handle->dirty_pages = apr_hash_make(txn_pool);
        if (!txn_handle->dirty_pages) {
            db_writer_unlock(env);
            apr_pool_destroy(txn_pool);
            return APR_ENOMEM;
        }

        /* Create array for tracking freed pages */
        txn_handle->freed_pages = apr_array_make(txn_pool, DB_FREED_PAGES_DFLT_SIZE, sizeof(pgno_t));
        if (!txn_handle->freed_pages) {
            db_writer_unlock(env);
            apr_pool_destroy(txn_pool);
            return APR_ENOMEM;
        }
    }
    else {
        /* Read-only transaction: Register in reader tracking table */
        apr_os_proc_t pid = getpid();
        apr_os_thread_t tid = apr_os_thread_current();
        int slot_idx = 0;
        int found_slot = 0;

        status = apr_thread_mutex_lock(env->reader_table_mutex);
        if (status != APR_SUCCESS) {
            apr_pool_destroy(txn_pool);
            return status;
        }

        /* Find an empty slot (txnid == 0) */
        for (slot_idx = 0; slot_idx < MAX_READERS; slot_idx++) {
            if (env->reader_table[slot_idx].txnid == 0) {
                env->reader_table[slot_idx].pid = pid;
                env->reader_table[slot_idx].tid = tid;
                env->reader_table[slot_idx].txnid = txn_handle->txnid;
                found_slot = 1;
                break;
            }
        }

        apr_thread_mutex_unlock(env->reader_table_mutex);

        if (!found_slot) {
            apr_pool_destroy(txn_pool);
            return APR_ENOMEM;  /* Too many concurrent readers */
        }
    }

    *txn = txn_handle;
    return APR_SUCCESS;
}

/**
 * @brief Commit a transaction.
 *
 * For read-only transactions, this just releases resources.
 * For write transactions, this is skeletal - it releases the writer lock
 * and cleans up. Full commit logic (CoW, writeback, meta page update)
 * will be implemented in later iterations.
 *
 * @param txn Transaction handle
 * @return APR_SUCCESS or error code
 */
/**
 * @brief Build a mapping from old to new page numbers for all dirty pages.
 * @param txn The transaction.
 * @param pgno_map_ptr Pointer to receive the new pgno map.
 * @return APR_SUCCESS or an error code.
 */
static apr_status_t build_pgno_map(napr_db_txn_t *txn, apr_hash_t **pgno_map_ptr)
{
    apr_hash_index_t *h_index = NULL;
    apr_hash_t *pgno_map = apr_hash_make(txn->pool);
    if (!pgno_map) {
        return APR_ENOMEM;
    }

    for (h_index = apr_hash_first(txn->pool, txn->dirty_pages); h_index; h_index = apr_hash_next(h_index)) {
        const void *key = NULL;
        void *val = NULL;
        pgno_t old_pgno = 0;
        pgno_t new_pgno = 0;
        pgno_t *old_pgno_ptr = NULL;
        pgno_t *new_pgno_ptr = NULL;
        DB_PageHeader *dirty_page = NULL;

        apr_hash_this(h_index, &key, NULL, &val);
        old_pgno = *(const pgno_t *) key;

        dirty_page = (DB_PageHeader *) val;
        new_pgno = dirty_page->pgno;

        old_pgno_ptr = apr_palloc(txn->pool, sizeof(pgno_t));
        new_pgno_ptr = apr_palloc(txn->pool, sizeof(pgno_t));

        if (!old_pgno_ptr || !new_pgno_ptr) {
            return APR_ENOMEM;
        }

        *old_pgno_ptr = old_pgno;
        *new_pgno_ptr = new_pgno;
        apr_hash_set(pgno_map, old_pgno_ptr, sizeof(pgno_t), new_pgno_ptr);
    }

    *pgno_map_ptr = pgno_map;
    return APR_SUCCESS;
}

/**
 * @brief Update pointers in dirty pages to point to new page locations.
 * @param txn The transaction.
 * @param pgno_map The mapping from old to new page numbers.
 * @param new_root_pgno_ptr Pointer to store the new root page number.
 */
static void update_dirty_page_pointers(napr_db_txn_t *txn, apr_hash_t *pgno_map, pgno_t *new_root_pgno_ptr)
{
    apr_hash_index_t *h_index = NULL;
    pgno_t *mapped_root = apr_hash_get(pgno_map, &txn->root_pgno, sizeof(pgno_t));
    if (mapped_root) {
        *new_root_pgno_ptr = *mapped_root;
    }
    else {
        *new_root_pgno_ptr = txn->root_pgno;
    }

    for (h_index = apr_hash_first(txn->pool, txn->dirty_pages); h_index; h_index = apr_hash_next(h_index)) {
        void *val = NULL;
        DB_PageHeader *dirty_page = NULL;
        uint16_t idx = 0;

        apr_hash_this(h_index, NULL, NULL, &val);
        dirty_page = (DB_PageHeader *) val;

        if (!(dirty_page->flags & P_BRANCH)) {
            continue;
        }

        for (idx = 0; idx < dirty_page->num_keys; idx++) {
            DB_BranchNode *branch_node = db_page_branch_node(dirty_page, idx);
            pgno_t old_child_pgno = branch_node->pgno;
            pgno_t *new_child_pgno = apr_hash_get(pgno_map, &old_child_pgno, sizeof(pgno_t));

            if (new_child_pgno) {
                branch_node->pgno = *new_child_pgno;
            }
        }
    }
}

/**
 * @brief Extend the database file if necessary.
 * @param txn The transaction.
 * @return APR_SUCCESS or an error code.
 */
static apr_status_t extend_database_file(napr_db_txn_t *txn)
{
    apr_status_t status = APR_SUCCESS;

    if (txn->new_last_pgno > txn->env->live_meta->last_pgno) {
        apr_off_t new_file_size = (apr_off_t) (txn->new_last_pgno + 1) * PAGE_SIZE;
        apr_off_t current_pos = 0;

        status = apr_file_seek(txn->env->file, APR_END, &current_pos);
        if (status != APR_SUCCESS) {
            return status;
        }

        if (current_pos < new_file_size) {
            apr_off_t seek_pos = new_file_size - 1;
            apr_size_t bytes_written = 1;
            char zero_byte = 0;

            status = apr_file_seek(txn->env->file, APR_SET, &seek_pos);
            if (status != APR_SUCCESS) {
                return status;
            }

            status = apr_file_write(txn->env->file, &zero_byte, &bytes_written);
            if (status != APR_SUCCESS) {
                return status;
            }

            status = apr_file_sync(txn->env->file);
        }
    }

    return status;
}

/**
 * @brief Write all dirty pages to disk.
 * @param txn The transaction.
 * @return APR_SUCCESS or an error code.
 */
static apr_status_t write_dirty_pages_to_disk(napr_db_txn_t *txn)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    apr_hash_index_t *h_index = NULL;

    for (h_index = apr_hash_first(txn->pool, txn->dirty_pages); h_index; h_index = apr_hash_next(h_index)) {
        void *val = NULL;
        DB_PageHeader *dirty_page = NULL;
        pgno_t new_pgno = 0;
        apr_off_t offset = 0;
        apr_size_t bytes_to_write = PAGE_SIZE;

        apr_hash_this(h_index, NULL, NULL, &val);
        dirty_page = (DB_PageHeader *) val;
        new_pgno = dirty_page->pgno;

        offset = (apr_off_t) new_pgno *PAGE_SIZE;

        status = apr_file_seek(txn->env->file, APR_SET, &offset);
        if (status != APR_SUCCESS) {
            return status;
        }

        status = apr_file_write_full(txn->env->file, dirty_page, bytes_to_write, NULL);
        if (status != APR_SUCCESS) {
            return status;
        }
    }

    return apr_file_sync(txn->env->file);
}

/* Forward declarations for Free DB helper functions */
static apr_status_t propagate_split_up_tree_in_tree(napr_db_txn_t *txn, const pgno_t *path, uint16_t path_len, napr_db_val_t *current_key, DB_SplitPgnos pgnos);
static apr_status_t handle_root_split_in_tree(napr_db_txn_t *txn, pgno_t old_root_pgno, pgno_t right_child_pgno, const napr_db_val_t *divider_key, pgno_t *new_root_out);

/**
 * @brief Populate the Free DB with freed pages from this transaction.
 *
 * This function inserts an entry into the Free DB mapping:
 * Key = current TXNID, Value = array of freed pgno_t values.
 *
 * The Free DB is a separate B+ tree that tracks which pages were freed
 * by which transaction. This enables safe page reclamation based on
 * the oldest active reader.
 *
 * @param txn The write transaction
 * @param new_free_db_root_out Output: the new Free DB root page number
 * @return APR_SUCCESS or an error code
 */
static apr_status_t initialize_empty_free_db(napr_db_txn_t *txn, const napr_db_val_t *key, const napr_db_val_t *data, pgno_t *new_root_pgno_out)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    pgno_t new_root_pgno;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    DB_PageHeader *new_root;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    pgno_t *pgno_key;

    status = db_page_alloc(txn, 1, &new_root_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    new_root = apr_pcalloc(txn->pool, PAGE_SIZE);
    if (!new_root) {
        return APR_ENOMEM;
    }

    new_root->pgno = new_root_pgno;
    new_root->flags = P_LEAF;
    new_root->num_keys = 0;
    new_root->lower = sizeof(DB_PageHeader);
    new_root->upper = PAGE_SIZE;

    status = db_page_insert(new_root, 0, key, data, 0);
    if (status != APR_SUCCESS) {
        return status;
    }

    pgno_key = apr_palloc(txn->pool, sizeof(pgno_t));
    if (!pgno_key) {
        return APR_ENOMEM;
    }
    *pgno_key = new_root_pgno;
    apr_hash_set(txn->dirty_pages, pgno_key, sizeof(pgno_t), new_root);
    *new_root_pgno_out = new_root_pgno;

    return APR_SUCCESS;
}

static apr_status_t insert_into_free_db(napr_db_txn_t *txn, const napr_db_val_t *key, const napr_db_val_t *data, pgno_t *new_free_db_root_out)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    pgno_t path[MAX_TREE_DEPTH] = { 0 };
    uint16_t path_len = 0;
    DB_PageHeader *leaf_page = NULL;
    uint16_t index = 0;
    napr_db_val_t current_key = *key;
    napr_db_val_t current_data = *data;
    pgno_t right_child_pgno = 0;

    status = db_find_leaf_page_with_path_in_tree(txn, txn->free_db_root_pgno, &current_key, path, &path_len, &leaf_page);
    if (status != APR_SUCCESS) {
        return initialize_empty_free_db(txn, key, data, new_free_db_root_out);
    }

    status = db_page_search(leaf_page, &current_key, &index);
    if (status == APR_SUCCESS) {
        return APR_EEXIST;
    }

    for (int i = (int)path_len - 1; i >= 0; i--) {
        pgno_t current_pgno = path[i];
        DB_PageHeader *dirty_page = NULL;
        DB_PageHeader *page_to_cow = apr_hash_get(txn->dirty_pages, &current_pgno, sizeof(pgno_t));
        if (page_to_cow) {
            dirty_page = page_to_cow;
        }
        else {
            DB_PageHeader *original_page = (DB_PageHeader *) ((char *) txn->env->map_addr + (current_pgno * PAGE_SIZE));
            status = db_page_get_writable(txn, original_page, &dirty_page);
            if (status != APR_SUCCESS) {
                return status;
            }
        }
        if (i == (int) path_len - 1) {
            leaf_page = dirty_page;
        }
    }

    status = db_page_insert(leaf_page, index, &current_key, &current_data, right_child_pgno);
    if (status == APR_SUCCESS) {
        *new_free_db_root_out = path[0];
        return APR_SUCCESS;
    }
    if (status != APR_ENOSPC) {
        return status;
    }

    DB_PageHeader *left_page = leaf_page;
    DB_PageHeader *right_page = NULL;
    napr_db_val_t divider_key = { 0 };
    status = db_split_leaf(txn, left_page, &right_page, &divider_key);
    if (status != APR_SUCCESS) {
        return status;
    }

    apr_status_t search_status = db_page_search(left_page, &current_key, &index);
    if (search_status == APR_NOTFOUND && index >= left_page->num_keys) {
        (void) db_page_search(right_page, &current_key, &index);
        status = db_page_insert(right_page, index, &current_key, &current_data, 0);
    }
    else {
        status = db_page_insert(left_page, index, &current_key, &current_data, 0);
    }
    if (status != APR_SUCCESS) {
        return status;
    }

    right_child_pgno = right_page->pgno;
    current_key = divider_key;
    status = propagate_split_up_tree_in_tree(txn, path, path_len, &current_key, (DB_SplitPgnos) {
                                             .right_child_pgno = &right_child_pgno,.new_root_out = new_free_db_root_out}
    );
    if (status == APR_INCOMPLETE) {
        *new_free_db_root_out = path[0];
        return APR_SUCCESS;
    }
    if (status != APR_SUCCESS) {
        return status;
    }

    return handle_root_split_in_tree(txn, path[0], right_child_pgno, &current_key, new_free_db_root_out);
}

static apr_status_t populate_free_db(napr_db_txn_t *txn, pgno_t *new_free_db_root_out)
{
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    txnid_t txnid_key = 0;

    if (!txn || !new_free_db_root_out) {
        return APR_EINVAL;
    }

    *new_free_db_root_out = txn->free_db_root_pgno;
    if (!txn->freed_pages || txn->freed_pages->nelts == 0) {
        return APR_SUCCESS;
    }

    txnid_key = txn->txnid;
    key.data = &txnid_key;
    key.size = sizeof(txnid_t);
    data.data = txn->freed_pages->elts;
    data.size = (apr_size_t) txn->freed_pages->nelts * txn->freed_pages->elt_size;

    pgno_t max_valid_pgno = (txn->flags & NAPR_DB_RDONLY) ? txn->env->live_meta->last_pgno : txn->new_last_pgno;
    if (txn->free_db_root_pgno == 0 || txn->free_db_root_pgno > max_valid_pgno) {
        return initialize_empty_free_db(txn, &key, &data, new_free_db_root_out);
    }

    return insert_into_free_db(txn, &key, &data, new_free_db_root_out);
}

/**
 * @brief Read an entry from the Free DB by TXNID.
 *
 * This helper function queries the Free DB to retrieve the list of pages
 * freed by a specific transaction. Used for testing and debugging.
 *
 * NOTE: This function is NOT part of the public API. It is exposed only for testing purposes.
 *
 * @param txn Transaction handle (can be read-only)
 * @param txnid The transaction ID to look up
 * @param freed_pages_out Output: Array of pgno_t values freed by this transaction
 * @param num_pages_out Output: Number of pages in the array
 * @return APR_SUCCESS if found, APR_NOTFOUND if not found, error code otherwise
 */
apr_status_t read_from_free_db(napr_db_txn_t *txn, txnid_t txnid, pgno_t **freed_pages_out, size_t *num_pages_out)
{
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    DB_PageHeader *leaf_page = NULL;
    uint16_t index = 0;
    apr_status_t status = APR_SUCCESS;

    if (!txn || !freed_pages_out || !num_pages_out) {
        return APR_EINVAL;
    }

    /* Initialize outputs */
    *freed_pages_out = NULL;
    *num_pages_out = 0;

    /* If Free DB is empty, nothing to find */
    if (txn->free_db_root_pgno == 0) {
        return APR_NOTFOUND;
    }

    /* Build the key: TXNID (8 bytes, little-endian) */
    key.data = &txnid;
    key.size = sizeof(txnid_t);

    /* Find the leaf page in the Free DB tree */
    status = db_find_leaf_page_in_tree(txn, txn->free_db_root_pgno, &key, &leaf_page);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Search for the key in the leaf page */
    status = db_page_search(leaf_page, &key, &index);
    if (status != APR_SUCCESS) {
        return APR_NOTFOUND;    /* Key not found */
    }

    /* Key found - retrieve the data */
    DB_LeafNode *node = db_page_leaf_node(leaf_page, index);
    data.data = db_leaf_node_value(node);
    data.size = node->data_size;

    /* Parse the data as an array of pgno_t values */
    if (data.size % sizeof(pgno_t) != 0) {
        return APR_EGENERAL;    /* Invalid data format */
    }

    *num_pages_out = data.size / sizeof(pgno_t);
    *freed_pages_out = (pgno_t *) data.data;

    return APR_SUCCESS;
}

/**
 * @brief Atomically update the meta page to commit the transaction.
 * @param txn The transaction.
 * @param pgnos The page numbers to commit.
 * @return APR_SUCCESS or an error code.
 */
static apr_status_t commit_meta_page(napr_db_txn_t *txn, DB_CommitPgnos pgnos)
{
    apr_status_t status = APR_SUCCESS;
    DB_MetaPage updated_meta;
    DB_MetaPage *stale_meta = NULL;
    pgno_t meta_pgno = 0;
    apr_off_t meta_offset = 0;
    apr_size_t meta_bytes = PAGE_SIZE;

    if (txn->env->live_meta == txn->env->meta0) {
        stale_meta = txn->env->meta1;
        meta_pgno = 1;
    }
    else {
        stale_meta = txn->env->meta0;
        meta_pgno = 0;
    }

    memset(&updated_meta, 0, sizeof(DB_MetaPage));
    updated_meta.magic = DB_MAGIC;
    updated_meta.version = DB_VERSION;
    updated_meta.txnid = txn->txnid;
    updated_meta.root = pgnos.new_root_pgno;
    updated_meta.last_pgno = txn->new_last_pgno;
    updated_meta.free_db_root = pgnos.new_free_db_root_pgno;

    meta_offset = (apr_off_t) meta_pgno *PAGE_SIZE;

    status = apr_file_seek(txn->env->file, APR_SET, &meta_offset);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = apr_file_write_full(txn->env->file, &updated_meta, meta_bytes, NULL);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = apr_file_sync(txn->env->file);
    if (status != APR_SUCCESS) {
        return status;
    }

    memcpy(stale_meta, &updated_meta, sizeof(DB_MetaPage));
    txn->env->live_meta = stale_meta;

    return APR_SUCCESS;
}

apr_status_t napr_db_txn_commit(napr_db_txn_t *txn)
{
    apr_status_t status = APR_SUCCESS;
    int is_write = 0;
    apr_hash_t *pgno_map = NULL;
    pgno_t new_root_pgno = 0;

    if (!txn) {
        return APR_EINVAL;
    }

    is_write = !(txn->flags & NAPR_DB_RDONLY);

    /* Unregister read-only transaction from reader table */
    if (!is_write) {
        apr_os_proc_t pid = getpid();
        apr_os_thread_t tid = apr_os_thread_current();
        int slot_idx = 0;

        apr_thread_mutex_lock(txn->env->reader_table_mutex);

        /* Find and clear our slot */
        for (slot_idx = 0; slot_idx < MAX_READERS; slot_idx++) {
            if (txn->env->reader_table[slot_idx].txnid == txn->txnid && txn->env->reader_table[slot_idx].pid == pid && txn->env->reader_table[slot_idx].tid == tid) {
                txn->env->reader_table[slot_idx].txnid = 0;
                txn->env->reader_table[slot_idx].pid = 0;
                txn->env->reader_table[slot_idx].tid = 0;
                break;
            }
        }

        apr_thread_mutex_unlock(txn->env->reader_table_mutex);
    }

    if (!is_write || !txn->dirty_pages || apr_hash_count(txn->dirty_pages) == 0) {
        if (is_write) {
            db_writer_unlock(txn->env);
        }
        apr_pool_destroy(txn->pool);
        return APR_SUCCESS;
    }

    /* Populate the Free DB with freed pages from this transaction FIRST
     * so that Free DB pages are added to dirty_pages before we update pointers */
    pgno_t new_free_db_root = 0;
    status = populate_free_db(txn, &new_free_db_root);
    if (status != APR_SUCCESS) {
        goto cleanup;
    }

    status = build_pgno_map(txn, &pgno_map);
    if (status != APR_SUCCESS) {
        goto cleanup;
    }

    update_dirty_page_pointers(txn, pgno_map, &new_root_pgno);

    status = extend_database_file(txn);
    if (status != APR_SUCCESS) {
        goto cleanup;
    }

    status = write_dirty_pages_to_disk(txn);
    if (status != APR_SUCCESS) {
        goto cleanup;
    }

    status = commit_meta_page(txn, (DB_CommitPgnos) {
                              .new_root_pgno = new_root_pgno,.new_free_db_root_pgno = new_free_db_root}
    );
    if (status != APR_SUCCESS) {
        goto cleanup;
    }

  cleanup:
    db_writer_unlock(txn->env);
    apr_pool_destroy(txn->pool);
    return status;
}

/**
 * @brief Abort a transaction.
 *
 * Discards all changes (for write transactions) and releases resources.
 * For now, this is skeletal - it just releases the writer lock and
 * cleans up. Full abort logic will be implemented in later iterations.
 *
 * @param txn Transaction handle
 * @return APR_SUCCESS or error code
 */
apr_status_t napr_db_txn_abort(napr_db_txn_t *txn)
{
    apr_status_t status = APR_SUCCESS;
    int is_write = !(txn->flags & NAPR_DB_RDONLY);

    if (!txn) {
        return APR_EINVAL;
    }

    /* Unregister read-only transaction from reader table */
    if (!is_write) {
        apr_os_proc_t pid = getpid();
        apr_os_thread_t tid = apr_os_thread_current();
        int slot_idx = 0;

        apr_thread_mutex_lock(txn->env->reader_table_mutex);

        /* Find and clear our slot */
        for (slot_idx = 0; slot_idx < MAX_READERS; slot_idx++) {
            if (txn->env->reader_table[slot_idx].txnid == txn->txnid && txn->env->reader_table[slot_idx].pid == pid && txn->env->reader_table[slot_idx].tid == tid) {
                txn->env->reader_table[slot_idx].txnid = 0;
                txn->env->reader_table[slot_idx].pid = 0;
                txn->env->reader_table[slot_idx].tid = 0;
                break;
            }
        }

        apr_thread_mutex_unlock(txn->env->reader_table_mutex);
    }

    /* If write transaction, release writer lock */
    if (is_write) {
        status = db_writer_unlock(txn->env);
    }

    /* Destroy transaction pool (frees all transaction allocations) */
    apr_pool_destroy(txn->pool);

    return status;
}

/**
 * @brief Retrieve a value for a given key (zero-copy).
 *
 * Searches the B+ tree for the specified key and returns a pointer
 * to the value data directly from the memory map (zero-copy).
 *
 * The returned data pointer is valid only for the lifetime of the
 * transaction and must not be modified.
 *
 * @param txn Transaction handle
 * @param key Key to search for
 * @param data Output: value data (size and pointer set on success)
 * @return APR_SUCCESS if found, APR_NOTFOUND if not found, error code on failure
 */
apr_status_t napr_db_get(napr_db_txn_t *txn, const napr_db_val_t *key, napr_db_val_t *data)
{
    DB_PageHeader *leaf_page = NULL;
    uint16_t index = 0;
    apr_status_t status = APR_SUCCESS;
    DB_LeafNode *leaf_node = NULL;

    if (!txn || !key || !data) {
        return APR_EINVAL;
    }

    /* Check for empty database */
    if (txn->root_pgno == 0) {
        return APR_NOTFOUND;
    }

    /* Find the leaf page that should contain the key */
    status = db_find_leaf_page(txn, key, &leaf_page);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Search within the leaf page for the exact key */
    status = db_page_search(leaf_page, key, &index);
    if (status == APR_NOTFOUND) {
        /* Key not found in the leaf */
        return APR_NOTFOUND;
    }
    if (status != APR_SUCCESS) {
        /* Other error */
        return status;
    }

    /* Found the key - retrieve the value (zero-copy) */
    leaf_node = db_page_leaf_node(leaf_page, index);

    /* Set the output data pointer and size */
    data->size = leaf_node->data_size;
    data->data = db_leaf_node_value(leaf_node);

    return APR_SUCCESS;
}

/**
 * @brief Insert a key/value pair into the B+ tree with split handling.
 *
 * This implements recursive insertion with support for page splits:
 * 1. Find the leaf page for insertion
 * 2. CoW the path from root to leaf
 * 3. Insert into leaf, splitting if necessary
 * 4. Propagate splits up the tree recursively
 * 5. Handle root split (increases tree height)
 *
 * @param txn Write transaction handle
 * @param key Key to insert
 * @param data Value to insert
 * @return APR_SUCCESS on success, APR_EEXIST if key exists, error code on failure
 */
static apr_status_t handle_empty_tree_put(napr_db_txn_t *txn, const napr_db_val_t *key, const napr_db_val_t *data)
{
    pgno_t new_root_pgno = 0;
    DB_PageHeader *new_root = NULL;
    apr_status_t status = APR_SUCCESS;

    /* Allocate the first leaf page */
    status = db_page_alloc(txn, 1, &new_root_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Allocate dirty page in memory */
    new_root = apr_pcalloc(txn->pool, PAGE_SIZE);
    if (!new_root) {
        return APR_ENOMEM;
    }

    /* Initialize the new leaf page */
    new_root->pgno = new_root_pgno;
    new_root->flags = P_LEAF;
    new_root->num_keys = 0;
    new_root->lower = sizeof(DB_PageHeader);
    new_root->upper = PAGE_SIZE;

    /* Insert the first key */
    status = db_page_insert(new_root, 0, key, data, 0);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Store in dirty pages hash */
    pgno_t *pgno_key = apr_palloc(txn->pool, sizeof(pgno_t));
    if (!pgno_key) {
        return APR_ENOMEM;
    }
    *pgno_key = new_root_pgno;
    apr_hash_set(txn->dirty_pages, pgno_key, sizeof(pgno_t), new_root);

    /* Update transaction's root pointer */
    txn->root_pgno = new_root_pgno;

    return APR_SUCCESS;
}

static apr_status_t propagate_split_up_tree(napr_db_txn_t *txn, const pgno_t *path, uint16_t path_len, napr_db_val_t *current_key, pgno_t *right_child_pgno);
static apr_status_t handle_root_split(napr_db_txn_t *txn, pgno_t old_root_pgno, pgno_t right_child_pgno, const napr_db_val_t *divider_key);
static apr_status_t handle_page_split(napr_db_txn_t *txn, DB_PageHeader *leaf_page, const pgno_t *path, uint16_t path_len, napr_db_val_t *current_key, napr_db_val_t *current_data);

apr_status_t napr_db_put(napr_db_txn_t *txn, const napr_db_val_t *key, napr_db_val_t *data)
{
    pgno_t path[MAX_TREE_DEPTH] = { 0 };
    uint16_t path_len = 0;
    DB_PageHeader *leaf_page = NULL;
    DB_PageHeader *dirty_page = NULL;
    uint16_t index = 0;
    apr_status_t status = APR_SUCCESS;
    int idx = 0;

    napr_db_val_t current_key;
    napr_db_val_t current_data;
    pgno_t right_child_pgno = 0;

    /* Validate inputs */
    if (!txn || !key || !data) {
        return APR_EINVAL;
    }

    /* Key/data to insert at each level (may be updated during split propagation) */
    current_key = *key;
    current_data = *data;

    /* Ensure this is a write transaction */
    if (txn->flags & NAPR_DB_RDONLY) {
        return APR_EACCES;
    }

    /* Special case: Empty tree (no root yet) */
    if (txn->root_pgno == 0) {
        return handle_empty_tree_put(txn, &current_key, &current_data);
    }

    /* Normal case: Tree exists, find the leaf page and record path */
    status = db_find_leaf_page_with_path(txn, &current_key, path, &path_len, &leaf_page);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Search within the leaf page to find insertion point */
    status = db_page_search(leaf_page, &current_key, &index);

    /* If key exists, check if this is a same-transaction duplicate or an MVCC update.
     * Same-transaction duplicate: Leaf page is already dirty -> reject with APR_EEXIST
     * MVCC update: Leaf page is NOT dirty -> allow via CoW */
    if (status == APR_SUCCESS) {
        pgno_t leaf_pgno = path[path_len - 1];
        DB_PageHeader *check_dirty = apr_hash_get(txn->dirty_pages, &leaf_pgno, sizeof(pgno_t));
        if (check_dirty) {
            /* Same transaction - key already inserted, can't insert again */
            return APR_EEXIST;
        }
        /* Different transaction - will CoW and update below */
    }

    /* CoW path propagation: Copy all pages from leaf to root */
    for (idx = (int) path_len - 1; idx >= 0; idx--) {
        pgno_t current_pgno = path[idx];
        DB_PageHeader *page_to_cow = NULL;

        /* Check if page is already dirty */
        page_to_cow = apr_hash_get(txn->dirty_pages, &current_pgno, sizeof(pgno_t));

        if (page_to_cow) {
            dirty_page = page_to_cow;
        }
        else {
            /* Page is in mmap - CoW it */
            DB_PageHeader *original_page = (DB_PageHeader *) ((char *) txn->env->map_addr + (current_pgno * PAGE_SIZE));

            status = db_page_get_writable(txn, original_page, &dirty_page);
            if (status != APR_SUCCESS) {
                return status;
            }
        }

        /* Update leaf_page pointer if this is the leaf */
        if (idx == (int) path_len - 1) {
            leaf_page = dirty_page;
        }
    }

    /* Try to insert into the dirty leaf page */
    status = db_page_insert(leaf_page, index, &current_key, &current_data, right_child_pgno);

    /* If insertion succeeded, we're done */
    if (status == APR_SUCCESS) {
        return APR_SUCCESS;
    }

    /* If insertion failed due to insufficient space, handle split */
    if (status != APR_ENOSPC) {
        return status;          /* Other error */
    }

    return handle_page_split(txn, leaf_page, path, path_len, &current_key, &current_data);
}

static apr_status_t handle_page_split(napr_db_txn_t *txn, DB_PageHeader *leaf_page, const pgno_t *path, uint16_t path_len, napr_db_val_t *current_key, napr_db_val_t *current_data)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    DB_PageHeader *left_page = leaf_page;
    DB_PageHeader *right_page = NULL;
    napr_db_val_t divider_key = { 0 };
    uint16_t index = 0;

    status = db_split_leaf(txn, left_page, &right_page, &divider_key);
    if (status != APR_SUCCESS) {
        return status;
    }

    apr_status_t search_status = db_page_search(left_page, current_key, &index);
    if (search_status == APR_NOTFOUND && index >= left_page->num_keys) {
        (void) db_page_search(right_page, current_key, &index);
        status = db_page_insert(right_page, index, current_key, current_data, 0);
    }
    else {
        status = db_page_insert(left_page, index, current_key, current_data, 0);
    }
    if (status != APR_SUCCESS) {
        return status;
    }

    pgno_t right_child_pgno = right_page->pgno;
    *current_key = divider_key;

    status = propagate_split_up_tree(txn, path, path_len, current_key, &right_child_pgno);
    if (status == APR_INCOMPLETE) {
        return APR_SUCCESS;
    }
    if (status != APR_SUCCESS) {
        return status;
    }

    return handle_root_split(txn, path[0], right_child_pgno, current_key);
}

static apr_status_t propagate_split_up_tree(napr_db_txn_t *txn, const pgno_t *path, uint16_t path_len, napr_db_val_t *current_key, pgno_t *right_child_pgno)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    DB_PageHeader *right_page = NULL;
    napr_db_val_t divider_key = { 0 };
    uint16_t index = 0;
    int idx = 0;

    for (idx = (int) path_len - 2; idx >= 0; idx--) {
        pgno_t parent_pgno = path[idx];
        DB_PageHeader *parent_page = apr_hash_get(txn->dirty_pages, &parent_pgno, sizeof(pgno_t));
        if (!parent_page) {
            return APR_EGENERAL;
        }

        (void) db_page_search(parent_page, current_key, &index);

        status = db_page_insert(parent_page, index, current_key, NULL, *right_child_pgno);
        if (status == APR_SUCCESS) {
            /* Successfully inserted into this parent - split absorbed, no root split needed */
            return APR_INCOMPLETE;
        }
        if (status != APR_ENOSPC) {
            return status;
        }

        status = db_split_branch(txn, parent_page, &right_page, &divider_key);
        if (status != APR_SUCCESS) {
            return status;
        }

        apr_status_t search_status = db_page_search(parent_page, current_key, &index);
        if (search_status == APR_NOTFOUND && index >= parent_page->num_keys) {
            (void) db_page_search(right_page, current_key, &index);
            status = db_page_insert(right_page, index, current_key, NULL, *right_child_pgno);
        }
        else {
            status = db_page_insert(parent_page, index, current_key, NULL, *right_child_pgno);
        }

        if (status != APR_SUCCESS) {
            return status;
        }

        *right_child_pgno = right_page->pgno;
        *current_key = divider_key;
    }

    /* Exited loop - split propagated all the way to root, need new root */
    return APR_SUCCESS;
}

static apr_status_t handle_root_split(napr_db_txn_t *txn, pgno_t old_root_pgno, pgno_t right_child_pgno, const napr_db_val_t *divider_key)
{
    pgno_t new_root_pgno = 0;
    DB_PageHeader *new_root = NULL;
    DB_PageHeader *old_root_page = NULL;
    napr_db_val_t left_min_key = { 0 };
    apr_status_t status = APR_SUCCESS;

    status = db_page_alloc(txn, 1, &new_root_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    new_root = apr_pcalloc(txn->pool, PAGE_SIZE);
    if (!new_root) {
        return APR_ENOMEM;
    }

    new_root->pgno = new_root_pgno;
    new_root->flags = P_BRANCH;
    new_root->num_keys = 0;
    new_root->lower = sizeof(DB_PageHeader);
    new_root->upper = PAGE_SIZE;

    old_root_page = apr_hash_get(txn->dirty_pages, &old_root_pgno, sizeof(pgno_t));
    if (!old_root_page) {
        return APR_EGENERAL;
    }

    if (old_root_page->flags & P_BRANCH) {
        DB_BranchNode *left_first_node = db_page_branch_node(old_root_page, 0);
        left_min_key.data = db_branch_node_key(left_first_node);
        left_min_key.size = left_first_node->key_size;
    }
    else {
        DB_LeafNode *left_first_leaf = db_page_leaf_node(old_root_page, 0);
        left_min_key.data = db_leaf_node_key(left_first_leaf);
        left_min_key.size = left_first_leaf->key_size;
    }

    status = db_page_insert(new_root, 0, &left_min_key, NULL, old_root_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = db_page_insert(new_root, 1, divider_key, NULL, right_child_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    pgno_t *pgno_key = apr_palloc(txn->pool, sizeof(pgno_t));
    if (!pgno_key) {
        return APR_ENOMEM;
    }
    *pgno_key = new_root_pgno;
    apr_hash_set(txn->dirty_pages, pgno_key, sizeof(pgno_t), new_root);

    txn->root_pgno = new_root_pgno;

    return APR_SUCCESS;
}

/**
 * @brief Propagate a split up the tree, updating an arbitrary tree root.
 *
 * This is similar to propagate_split_up_tree() but accepts a root output parameter
 * instead of modifying txn->root_pgno. Used for Free DB operations.
 *
 * @param txn Transaction handle
 * @param path Array of page numbers from root to leaf
 * @param path_len Length of the path
 * @param current_key Key to insert at parent level (may be updated during splits)
 * @param pgnos The page numbers to update.
 * @return APR_SUCCESS if split propagated to root, APR_INCOMPLETE if absorbed, error otherwise
 */
static apr_status_t propagate_split_up_tree_in_tree(napr_db_txn_t *txn, const pgno_t *path, uint16_t path_len, napr_db_val_t *current_key, DB_SplitPgnos pgnos)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    DB_PageHeader *right_page = NULL;
    napr_db_val_t divider_key = { 0 };
    uint16_t index = 0;
    int idx = 0;

    for (idx = (int) path_len - 2; idx >= 0; idx--) {
        pgno_t parent_pgno = path[idx];
        DB_PageHeader *parent_page = apr_hash_get(txn->dirty_pages, &parent_pgno, sizeof(pgno_t));
        if (!parent_page) {
            return APR_EGENERAL;
        }

        (void) db_page_search(parent_page, current_key, &index);

        status = db_page_insert(parent_page, index, current_key, NULL, *pgnos.right_child_pgno);
        if (status == APR_SUCCESS) {
            /* Successfully inserted into this parent - split absorbed, no root split needed */
            return APR_INCOMPLETE;
        }
        if (status != APR_ENOSPC) {
            return status;
        }

        status = db_split_branch(txn, parent_page, &right_page, &divider_key);
        if (status != APR_SUCCESS) {
            return status;
        }

        apr_status_t search_status = db_page_search(parent_page, current_key, &index);
        if (search_status == APR_NOTFOUND && index >= parent_page->num_keys) {
            (void) db_page_search(right_page, current_key, &index);
            status = db_page_insert(right_page, index, current_key, NULL, *pgnos.right_child_pgno);
        }
        else {
            status = db_page_insert(parent_page, index, current_key, NULL, *pgnos.right_child_pgno);
        }

        if (status != APR_SUCCESS) {
            return status;
        }

        *pgnos.right_child_pgno = right_page->pgno;
        *current_key = divider_key;
    }

    /* Exited loop - split propagated all the way to root, need new root */
    return APR_SUCCESS;
}

/**
 * @brief Handle root split for an arbitrary tree.
 *
 * This is similar to handle_root_split() but accepts a root output parameter
 * instead of modifying txn->root_pgno. Used for Free DB operations.
 *
 * @param txn Transaction handle
 * @param old_root_pgno Old root page number
 * @param right_child_pgno Right child page number from split
 * @param divider_key Key separating left and right children
 * @param new_root_out Output: New root page number
 * @return APR_SUCCESS on success, error code on failure
 */
static apr_status_t handle_root_split_in_tree(napr_db_txn_t *txn, pgno_t old_root_pgno, pgno_t right_child_pgno, const napr_db_val_t *divider_key, pgno_t *new_root_out)
{
    pgno_t new_root_pgno = 0;
    DB_PageHeader *new_root = NULL;
    DB_PageHeader *old_root_page = NULL;
    napr_db_val_t left_min_key = { 0 };
    apr_status_t status = APR_SUCCESS;

    status = db_page_alloc(txn, 1, &new_root_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    new_root = apr_pcalloc(txn->pool, PAGE_SIZE);
    if (!new_root) {
        return APR_ENOMEM;
    }

    new_root->pgno = new_root_pgno;
    new_root->flags = P_BRANCH;
    new_root->num_keys = 0;
    new_root->lower = sizeof(DB_PageHeader);
    new_root->upper = PAGE_SIZE;

    old_root_page = apr_hash_get(txn->dirty_pages, &old_root_pgno, sizeof(pgno_t));
    if (!old_root_page) {
        return APR_EGENERAL;
    }

    if (old_root_page->flags & P_BRANCH) {
        DB_BranchNode *left_first_node = db_page_branch_node(old_root_page, 0);
        left_min_key.data = db_branch_node_key(left_first_node);
        left_min_key.size = left_first_node->key_size;
    }
    else {
        DB_LeafNode *left_first_leaf = db_page_leaf_node(old_root_page, 0);
        left_min_key.data = db_leaf_node_key(left_first_leaf);
        left_min_key.size = left_first_leaf->key_size;
    }

    status = db_page_insert(new_root, 0, &left_min_key, NULL, old_root_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = db_page_insert(new_root, 1, divider_key, NULL, right_child_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    pgno_t *pgno_key = apr_palloc(txn->pool, sizeof(pgno_t));
    if (!pgno_key) {
        return APR_ENOMEM;
    }
    *pgno_key = new_root_pgno;
    apr_hash_set(txn->dirty_pages, pgno_key, sizeof(pgno_t), new_root);

    /* Update output parameter instead of txn->root_pgno */
    *new_root_out = new_root_pgno;

    return APR_SUCCESS;
}

/**
 * @brief Delete a key-value pair from the database.
 *
 * This function removes a key from the B+ tree using CoW semantics:
 * 1. Traverse the tree to find the key, recording the path
 * 2. Make dirty copies of all pages in the path (CoW)
 * 3. Delete the entry from the dirty leaf page
 * 4. Update will be written on transaction commit
 *
 * Note: This implementation performs simple deletion without B+ tree rebalancing
 * (node merging/redistribution), which is acceptable for this use case.
 *
 * @param txn Write transaction handle
 * @param key Key to delete
 * @param data Currently unused (reserved for conditional delete)
 * @return APR_SUCCESS on success, APR_NOTFOUND if key doesn't exist, error otherwise
 */
apr_status_t napr_db_del(napr_db_txn_t *txn, const napr_db_val_t *key, napr_db_val_t *data)
{
    pgno_t path[MAX_TREE_DEPTH] = { 0 };
    uint16_t path_len = 0;
    DB_PageHeader *leaf_page = NULL;
    uint16_t index = 0;
    apr_status_t status = APR_SUCCESS;

    (void) data;                /* Reserved for future use (conditional delete) */

    if (!txn || !key || (txn->flags & NAPR_DB_RDONLY)) {
        return APR_EINVAL;
    }

    /* Empty tree - nothing to delete */
    if (txn->root_pgno == 0) {
        return APR_NOTFOUND;
    }

    /* Find the leaf page containing the key, recording path for CoW */
    status = db_find_leaf_page_with_path(txn, key, path, &path_len, &leaf_page);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Search for the key in the leaf page */
    status = db_page_search(leaf_page, key, &index);
    if (status != APR_SUCCESS) {
        return APR_NOTFOUND;
    }

    /* CoW: Make dirty copies of all pages in the path */
    for (int idx = (int)path_len - 1; idx >= 0; idx--) {
        pgno_t current_pgno = path[idx];
        DB_PageHeader *page_to_cow = NULL;
        DB_PageHeader *dirty_page = NULL;

        /* Check if page is already dirty */
        page_to_cow = apr_hash_get(txn->dirty_pages, &current_pgno, sizeof(pgno_t));

        if (!page_to_cow) {
            /* Page is in mmap - CoW it */
            DB_PageHeader *original_page = (DB_PageHeader *) ((char *) txn->env->map_addr + (current_pgno * PAGE_SIZE));

            status = db_page_get_writable(txn, original_page, &dirty_page);
            if (status != APR_SUCCESS) {
                return status;
            }
        }
    }

    /* Get the dirty copy of the leaf page */
    pgno_t leaf_pgno = path[path_len - 1];
    leaf_page = apr_hash_get(txn->dirty_pages, &leaf_pgno, sizeof(pgno_t));
    if (!leaf_page) {
        return APR_EGENERAL;
    }

    /* Delete the entry from the dirty leaf page */
    status = db_page_delete(leaf_page, index);
    if (status != APR_SUCCESS) {
        return status;
    }

    return APR_SUCCESS;
}
