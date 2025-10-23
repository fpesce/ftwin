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
#include <string.h>

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
static void init_meta_page(DB_MetaPage * meta, txnid_t txnid)
{
    memset(meta, 0, sizeof(DB_MetaPage));
    meta->magic = DB_MAGIC;
    meta->version = DB_VERSION;
    meta->txnid = txnid;
    meta->root = 0;             /* No root page yet (empty tree) */
    meta->last_pgno = 1;        /* Pages 0 and 1 are meta pages */
}

/**
 * @brief Validate a meta page.
 *
 * Checks magic number and version to ensure the meta page is valid.
 *
 * @param meta Pointer to meta page
 * @return APR_SUCCESS if valid, APR_EINVAL otherwise
 */
static apr_status_t validate_meta_page(const DB_MetaPage * meta)
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

    /* Initialize dirty page tracking and allocation state */
    txn_handle->dirty_pages = NULL;
    txn_handle->new_last_pgno = env->live_meta->last_pgno;

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
apr_status_t napr_db_txn_commit(napr_db_txn_t *txn)
{
    apr_status_t status = APR_SUCCESS;
    int is_write = !(txn->flags & NAPR_DB_RDONLY);
    apr_hash_index_t *hi = NULL;
    DB_MetaPage *stale_meta = NULL;
    pgno_t new_root_pgno = 0;

    if (!txn) {
        return APR_EINVAL;
    }

    /* Read-only transactions have nothing to commit */
    if (!is_write) {
        apr_pool_destroy(txn->pool);
        return APR_SUCCESS;
    }

    /* If no dirty pages, nothing to commit */
    if (!txn->dirty_pages || apr_hash_count(txn->dirty_pages) == 0) {
        status = db_writer_unlock(txn->env);
        apr_pool_destroy(txn->pool);
        return status;
    }

    /*
     * STEP 1: Allocate physical page numbers for all dirty pages
     *
     * Build a mapping from old (logical) page numbers to new (physical) page numbers.
     * We iterate through all dirty pages and assign each a new physical PGNO using
     * db_page_alloc. This mapping will be used in Step 2 to update pointers.
     */
    apr_hash_t *pgno_map = apr_hash_make(txn->pool);
    if (!pgno_map) {
        status = APR_ENOMEM;
        goto cleanup;
    }

    for (hi = apr_hash_first(txn->pool, txn->dirty_pages); hi; hi = apr_hash_next(hi)) {
        const void *key = NULL;
        void *val = NULL;
        pgno_t old_pgno = 0;
        pgno_t new_pgno = 0;
        pgno_t *old_pgno_ptr = NULL;
        pgno_t *new_pgno_ptr = NULL;

        apr_hash_this(hi, &key, NULL, &val);
        old_pgno = *(const pgno_t *) key;

        /* The page was already allocated by db_page_alloc when dirty page was created
         * We just need to use the pgno that's already in the page header */
        DB_PageHeader *dirty_page = (DB_PageHeader *) val;
        new_pgno = dirty_page->pgno;

        /* Store mapping: old_pgno -> new_pgno */
        old_pgno_ptr = apr_palloc(txn->pool, sizeof(pgno_t));
        new_pgno_ptr = apr_palloc(txn->pool, sizeof(pgno_t));
        if (!old_pgno_ptr || !new_pgno_ptr) {
            status = APR_ENOMEM;
            goto cleanup;
        }
        *old_pgno_ptr = old_pgno;
        *new_pgno_ptr = new_pgno;
        apr_hash_set(pgno_map, old_pgno_ptr, sizeof(pgno_t), new_pgno_ptr);
    }

    /*
     * STEP 2: Update pointers in dirty pages
     *
     * For each dirty branch page, update child page pointers to use the new
     * physical page numbers. If a child page was also modified (is dirty),
     * its pointer must be updated to point to the new location.
     *
     * Also identify the new root page number.
     */
    new_root_pgno = txn->root_pgno;

    /* Check if root was modified (is in dirty pages) */
    pgno_t *mapped_root = apr_hash_get(pgno_map, &txn->root_pgno, sizeof(pgno_t));
    if (mapped_root) {
        new_root_pgno = *mapped_root;
    }

    /* Iterate dirty pages and update child pointers in branch nodes */
    for (hi = apr_hash_first(txn->pool, txn->dirty_pages); hi; hi = apr_hash_next(hi)) {
        const void *key = NULL;
        void *val = NULL;
        DB_PageHeader *dirty_page = NULL;
        uint16_t i = 0;

        apr_hash_this(hi, &key, NULL, &val);
        dirty_page = (DB_PageHeader *) val;

        /* Only branch pages have child pointers to update */
        if (!(dirty_page->flags & P_BRANCH)) {
            continue;
        }

        /* Update each branch node's child pointer if child is also dirty */
        for (i = 0; i < dirty_page->num_keys; i++) {
            DB_BranchNode *branch_node = db_page_branch_node(dirty_page, i);
            pgno_t old_child_pgno = branch_node->pgno;
            pgno_t *new_child_pgno = apr_hash_get(pgno_map, &old_child_pgno, sizeof(pgno_t));

            if (new_child_pgno) {
                /* Child page was modified, update pointer to new location */
                branch_node->pgno = *new_child_pgno;
            }
        }
    }

    /*
     * STEP 3A: Extend file if necessary
     *
     * Before writing dirty pages, ensure the file is large enough to hold
     * all the newly allocated pages. We need to extend the file to accommodate
     * the new last_pgno.
     */
    if (txn->new_last_pgno > txn->env->live_meta->last_pgno) {
        apr_off_t new_file_size = (txn->new_last_pgno + 1) * PAGE_SIZE;
        apr_off_t current_pos = 0;

        /* Seek to the new end of file */
        status = apr_file_seek(txn->env->file, APR_END, &current_pos);
        if (status != APR_SUCCESS) {
            goto cleanup;
        }

        /* If file needs to grow, write a byte at the new end position */
        if (current_pos < new_file_size) {
            apr_off_t seek_pos = new_file_size - 1;
            apr_size_t bytes_written = 1;
            char zero_byte = 0;

            status = apr_file_seek(txn->env->file, APR_SET, &seek_pos);
            if (status != APR_SUCCESS) {
                goto cleanup;
            }

            status = apr_file_write(txn->env->file, &zero_byte, &bytes_written);
            if (status != APR_SUCCESS) {
                goto cleanup;
            }

            /* Flush the file extension to ensure it's persistent */
            status = apr_file_sync(txn->env->file);
            if (status != APR_SUCCESS) {
                goto cleanup;
            }
        }
    }

    /*
     * STEP 3B: Writeback phase - Write dirty pages to their new locations
     *
     * Copy each dirty page from transaction-private memory to its new location
     * in the file. We use apr_file_write_full to write directly to the file
     * at specific offsets, rather than accessing the mmap (which may not
     * reflect the extended file yet).
     */
    for (hi = apr_hash_first(txn->pool, txn->dirty_pages); hi; hi = apr_hash_next(hi)) {
        const void *key = NULL;
        void *val = NULL;
        DB_PageHeader *dirty_page = NULL;
        pgno_t new_pgno = 0;
        apr_off_t offset = 0;
        apr_size_t bytes_to_write = PAGE_SIZE;

        apr_hash_this(hi, &key, NULL, &val);
        dirty_page = (DB_PageHeader *) val;
        new_pgno = dirty_page->pgno;

        /* Calculate file offset for this page */
        offset = new_pgno * PAGE_SIZE;

        /* Seek to the page location */
        status = apr_file_seek(txn->env->file, APR_SET, &offset);
        if (status != APR_SUCCESS) {
            goto cleanup;
        }

        /* Write the dirty page to file */
        status = apr_file_write_full(txn->env->file, dirty_page, bytes_to_write, NULL);
        if (status != APR_SUCCESS) {
            goto cleanup;
        }
    }

    /*
     * STEP 4: Durability Step 1 - Flush data pages to disk
     *
     * Ensure all dirty pages are physically written to disk before updating
     * the meta page. This guarantees that if the meta page points to a page,
     * that page's data is durable.
     */
    status = apr_file_sync(txn->env->file);
    if (status != APR_SUCCESS) {
        goto cleanup;
    }

    /*
     * STEP 5 & 6: Meta page selection and preparation
     *
     * We have two meta pages (0 and 1). The "live" meta page is the one
     * that was used as the snapshot root for this transaction. The "stale"
     * meta page is the other one - we'll update it to commit our changes.
     *
     * Create an updated meta page in memory with:
     * - New transaction ID (incremented)
     * - New root page number
     * - New last_pgno (highest allocated page)
     */
    DB_MetaPage updated_meta;
    pgno_t meta_pgno = 0;
    apr_off_t meta_offset = 0;
    apr_size_t meta_bytes = PAGE_SIZE;

    /* Determine which meta page to update */
    if (txn->env->live_meta == txn->env->meta0) {
        stale_meta = txn->env->meta1;
        meta_pgno = 1;
    }
    else {
        stale_meta = txn->env->meta0;
        meta_pgno = 0;
    }

    /* Populate the updated meta page */
    memset(&updated_meta, 0, sizeof(DB_MetaPage));
    updated_meta.magic = DB_MAGIC;
    updated_meta.version = DB_VERSION;
    updated_meta.txnid = txn->txnid;    /* txnid was already incremented in txn_begin */
    updated_meta.root = new_root_pgno;
    updated_meta.last_pgno = txn->new_last_pgno;

    /*
     * STEP 7: Atomic commit point - Write and flush meta page to disk
     *
     * Write the updated meta page to disk and synchronously flush it.
     * Once this completes, the transaction is committed - the new state
     * is visible to future transactions.
     *
     * This is the atomic commit point: either this write succeeds (and all
     * changes are committed), or it fails (and the old meta page remains
     * valid, preserving the previous state).
     */
    meta_offset = meta_pgno * PAGE_SIZE;

    status = apr_file_seek(txn->env->file, APR_SET, &meta_offset);
    if (status != APR_SUCCESS) {
        goto cleanup;
    }

    status = apr_file_write_full(txn->env->file, &updated_meta, meta_bytes, NULL);
    if (status != APR_SUCCESS) {
        goto cleanup;
    }

    status = apr_file_sync(txn->env->file);
    if (status != APR_SUCCESS) {
        goto cleanup;
    }

    /* Copy updated meta to the mmap meta page for future reads */
    memcpy(stale_meta, &updated_meta, sizeof(DB_MetaPage));

    /*
     * STEP 8: Update environment - Switch to the new meta page
     *
     * Update the environment's live_meta pointer to point to the newly
     * committed meta page. Future transactions will now see the committed
     * state.
     */
    txn->env->live_meta = stale_meta;

  cleanup:
    /*
     * STEP 9: Release writer lock
     *
     * Allow the next write transaction to proceed.
     */
    if (is_write) {
        db_writer_unlock(txn->env);
    }

    /* Destroy transaction pool (frees all transaction allocations) */
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

apr_status_t napr_db_put(napr_db_txn_t *txn, const napr_db_val_t *key, napr_db_val_t *data)
{
    pgno_t path[MAX_TREE_DEPTH] = { 0 };
    uint16_t path_len = 0;
    DB_PageHeader *leaf_page = NULL;
    DB_PageHeader *dirty_page = NULL;
    uint16_t index = 0;
    apr_status_t status = APR_SUCCESS;
    int idx = 0;

    /* Validate inputs */
    if (!txn || !key || !data) {
        return APR_EINVAL;
    }

    /* Ensure this is a write transaction */
    if (txn->flags & NAPR_DB_RDONLY) {
        return APR_EACCES;
    }

    /* Special case: Empty tree (no root yet) */
    if (txn->root_pgno == 0) {
        pgno_t new_root_pgno = 0;
        DB_PageHeader *new_root = NULL;

        /* Allocate the first leaf page */
        status = db_page_alloc(txn, 1, &new_root_pgno);
        if (status != APR_SUCCESS) {
            return status;
        }

        /* Allocate dirty page in memory (cannot write to mmap beyond file size) */
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

        /* Store in dirty pages hash (will be written on commit) */
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

    /* Normal case: Tree exists, find the leaf page and record path */
    status = db_find_leaf_page_with_path(txn, key, path, &path_len, &leaf_page);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Search within the leaf page to find insertion point */
    status = db_page_search(leaf_page, key, &index);

    /* If key already exists, this is an update (for now, treat as error) */
    if (status == APR_SUCCESS) {
        return APR_EEXIST;      /* Key already exists - updates not yet supported */
    }

    /* CRITICAL: Copy-on-Write path propagation
     * Iterate through the path from leaf to root, calling db_page_get_writable
     * for each page. This ensures the entire path is copied and the transaction
     * operates on its own version of the tree structure.
     */
    for (idx = (int) path_len - 1; idx >= 0; idx--) {
        pgno_t current_pgno = path[idx];
        DB_PageHeader *page_to_cow = NULL;

        /* Check if page is already dirty (newly allocated or previously modified) */
        page_to_cow = apr_hash_get(txn->dirty_pages, &current_pgno, sizeof(pgno_t));

        if (page_to_cow) {
            /* Page is already dirty - no need to CoW, just use it */
            dirty_page = page_to_cow;
        }
        else {
            /* Page is in mmap - need to CoW it */
            DB_PageHeader *original_page = (DB_PageHeader *) ((char *) txn->env->map_addr + (current_pgno * PAGE_SIZE));

            status = db_page_get_writable(txn, original_page, &dirty_page);
            if (status != APR_SUCCESS) {
                return status;
            }
        }

        /* Update leaf_page pointer if this is the leaf (last element in path) */
        if (idx == (int) path_len - 1) {
            leaf_page = dirty_page;
        }
    }

    /* Insert the new key/value into the dirty leaf page */
    status = db_page_insert(leaf_page, index, key, data, 0);
    if (status != APR_SUCCESS) {
        return status;
    }

    return APR_SUCCESS;
}

apr_status_t napr_db_del(napr_db_txn_t *txn, const napr_db_val_t *key, napr_db_val_t *data)
{
    (void) txn;
    (void) key;
    (void) data;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_cursor_open(napr_db_txn_t *txn, napr_db_cursor_t **cursor)
{
    (void) txn;
    (void) cursor;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_cursor_close(napr_db_cursor_t *cursor)
{
    (void) cursor;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_cursor_get(napr_db_cursor_t *cursor, const napr_db_val_t *key, napr_db_val_t *data, int operation)
{
    (void) cursor;
    (void) key;
    (void) data;
    (void) operation;
    return APR_ENOTIMPL;
}
