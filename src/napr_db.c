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
apr_status_t napr_db_env_create(napr_db_env_t ** env, apr_pool_t *pool)
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
apr_status_t napr_db_env_set_mapsize(napr_db_env_t * env, apr_size_t size)
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
static apr_status_t select_live_meta(napr_db_env_t * env)
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
static apr_status_t db_file_open(napr_db_env_t * env, const char *path, int *is_new_file)
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
static apr_status_t db_mmap_file(napr_db_env_t * env)
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
static apr_status_t db_init_meta(napr_db_env_t * env, int is_new_file)
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
static apr_status_t db_init_writer_lock(napr_db_env_t * env)
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
apr_status_t napr_db_env_open(napr_db_env_t * env, const char *path, unsigned int flags)
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
apr_status_t napr_db_env_close(napr_db_env_t * env)
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
static apr_status_t db_writer_lock(napr_db_env_t * env)
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
static apr_status_t db_writer_unlock(napr_db_env_t * env)
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
apr_status_t napr_db_txn_begin(napr_db_env_t * env, unsigned int flags, napr_db_txn_t ** txn)
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
apr_status_t napr_db_txn_commit(napr_db_txn_t * txn)
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
 * @brief Abort a transaction.
 *
 * Discards all changes (for write transactions) and releases resources.
 * For now, this is skeletal - it just releases the writer lock and
 * cleans up. Full abort logic will be implemented in later iterations.
 *
 * @param txn Transaction handle
 * @return APR_SUCCESS or error code
 */
apr_status_t napr_db_txn_abort(napr_db_txn_t * txn)
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
apr_status_t napr_db_get(napr_db_txn_t * txn, const napr_db_val_t * key, napr_db_val_t * data)
{
    DB_PageHeader *leaf_page = NULL;
    uint16_t index = 0;
    apr_status_t status = APR_SUCCESS;
    DB_LeafNode *leaf_node = NULL;

    if (!txn || !key || !data) {
        return APR_EINVAL;
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

apr_status_t napr_db_put(napr_db_txn_t * txn, const napr_db_val_t * key, napr_db_val_t * data)
{
    (void) txn;
    (void) key;
    (void) data;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_del(napr_db_txn_t * txn, const napr_db_val_t * key, napr_db_val_t * data)
{
    (void) txn;
    (void) key;
    (void) data;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_cursor_open(napr_db_txn_t * txn, napr_db_cursor_t ** cursor)
{
    (void) txn;
    (void) cursor;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_cursor_close(napr_db_cursor_t * cursor)
{
    (void) cursor;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_cursor_get(napr_db_cursor_t * cursor, const napr_db_val_t * key, napr_db_val_t * data, int operation)
{
    (void) cursor;
    (void) key;
    (void) data;
    (void) operation;
    return APR_ENOTIMPL;
}
