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
    napr_db_env_t *e = apr_pcalloc(pool, sizeof(napr_db_env_t));
    if (!e) {
        return APR_ENOMEM;
    }

    /* Initialize fields */
    e->pool = pool;
    e->mapsize = 0;
    e->flags = 0;
    e->file = NULL;
    e->mmap = NULL;
    e->map_addr = NULL;
    e->meta0 = NULL;
    e->meta1 = NULL;
    e->live_meta = NULL;

    *env = e;
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
    apr_status_t status0, status1;

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
    apr_status_t status;
    apr_int32_t file_flags;
    int is_new_file = 0;

    if (!env || !path) {
        return APR_EINVAL;
    }

    if (env->mapsize == 0) {
        return APR_EINVAL;      /* Must call set_mapsize first */
    }

    env->flags = flags;

    /* Determine file flags */
    if (flags & NAPR_DB_RDONLY) {
        file_flags = APR_READ | APR_BINARY;
    }
    else {
        file_flags = APR_READ | APR_WRITE | APR_BINARY;
    }

    /* Check if file exists */
    apr_finfo_t finfo;
    status = apr_stat(&finfo, path, APR_FINFO_SIZE, env->pool);

    if (status == APR_SUCCESS) {
        /* File exists - open it */
        is_new_file = 0;
        status = apr_file_open(&env->file, path, file_flags, APR_OS_DEFAULT, env->pool);
        if (status != APR_SUCCESS) {
            return status;
        }
    }
    else if (status == APR_ENOENT) {
        /* File doesn't exist */
        if (!(flags & NAPR_DB_CREATE)) {
            return APR_ENOENT;  /* CREATE flag not set */
        }

        /* Create new file */
        is_new_file = 1;
        file_flags |= APR_CREATE | APR_TRUNCATE;
        status = apr_file_open(&env->file, path, file_flags, APR_FPROT_UREAD | APR_FPROT_UWRITE, env->pool);
        if (status != APR_SUCCESS) {
            return status;
        }

        /* Ensure file is at least 2 pages (for 2 meta pages) */
        apr_off_t min_size = 2 * PAGE_SIZE;
        status = apr_file_trunc(env->file, min_size);
        if (status != APR_SUCCESS) {
            apr_file_close(env->file);
            env->file = NULL;
            return status;
        }
    }
    else {
        /* Other error */
        return status;
    }

    /* Memory map the file */
    status = apr_mmap_create(&env->mmap, env->file, 0, env->mapsize, APR_MMAP_READ | ((flags & NAPR_DB_RDONLY) ? 0 : APR_MMAP_WRITE), env->pool);
    if (status != APR_SUCCESS) {
        apr_file_close(env->file);
        env->file = NULL;
        return status;
    }

    /* Get base address of memory map */
    status = apr_mmap_offset(&env->map_addr, env->mmap, 0);
    if (status != APR_SUCCESS) {
        apr_mmap_delete(env->mmap);
        env->mmap = NULL;
        apr_file_close(env->file);
        env->file = NULL;
        return status;
    }

    /* Set up meta page pointers */
    env->meta0 = (DB_MetaPage *) env->map_addr;
    env->meta1 = (DB_MetaPage *) ((char *) env->map_addr + PAGE_SIZE);

    if (is_new_file) {
        /* Initialize meta pages for new database */
        init_meta_page(env->meta0, 0);
        init_meta_page(env->meta1, 1);

        /* Ensure durability - sync to disk */
        status = apr_file_sync(env->file);
        if (status != APR_SUCCESS) {
            apr_mmap_delete(env->mmap);
            env->mmap = NULL;
            env->map_addr = NULL;
            apr_file_close(env->file);
            env->file = NULL;
            return status;
        }

        /* Select live meta page (will be meta1 since it has higher TXNID) */
        env->live_meta = env->meta1;
    }
    else {
        /* Existing database - validate and select live meta page */
        status = select_live_meta(env);
        if (status != APR_SUCCESS) {
            apr_mmap_delete(env->mmap);
            env->mmap = NULL;
            env->map_addr = NULL;
            apr_file_close(env->file);
            env->file = NULL;
            return status;
        }
    }

    return APR_SUCCESS;
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

/* Stub implementations for functions declared in API but not yet implemented */

apr_status_t napr_db_txn_begin(napr_db_env_t * env, unsigned int flags, napr_db_txn_t ** txn)
{
    (void) env;
    (void) flags;
    (void) txn;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_txn_commit(napr_db_txn_t * txn)
{
    (void) txn;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_txn_abort(napr_db_txn_t * txn)
{
    (void) txn;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_get(napr_db_txn_t * txn, napr_db_val_t * key, napr_db_val_t * data)
{
    (void) txn;
    (void) key;
    (void) data;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_put(napr_db_txn_t * txn, napr_db_val_t * key, napr_db_val_t * data)
{
    (void) txn;
    (void) key;
    (void) data;
    return APR_ENOTIMPL;
}

apr_status_t napr_db_del(napr_db_txn_t * txn, napr_db_val_t * key, napr_db_val_t * data)
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

apr_status_t napr_db_cursor_get(napr_db_cursor_t * cursor, napr_db_val_t * key, napr_db_val_t * data, int op)
{
    (void) cursor;
    (void) key;
    (void) data;
    (void) op;
    return APR_ENOTIMPL;
}
