/**
 * @file napr_cache.c
 * @brief Filesystem Hash Cache Implementation
 */

#include "napr_cache.h"
#include "debug.h"

#include <apr_file_io.h>
#include <apr_file_info.h>
#include <apr_strings.h>
#include <apr_thread_mutex.h>
#include <apr_hash.h>

/* Constants */
enum
{
    CACHE_MAPSIZE_GB = 10,
    CACHE_MAPSIZE = (CACHE_MAPSIZE_GB * 1024ULL * 1024ULL * 1024ULL)
};

/**
 * @brief Internal cache structure
 */
struct napr_cache_t
{
    apr_pool_t *pool;                    /**< Main pool for cache lifetime */
    napr_db_env_t *db_env;               /**< Underlying napr_db environment */
    napr_db_txn_t *active_txn;           /**< Current active transaction (if any) */
    apr_file_t *lock_file_handle;        /**< Exclusive lock file handle */
    apr_hash_t *visited_set;             /**< Mark-and-sweep: visited paths */
    apr_thread_mutex_t *visited_mutex;   /**< Protects visited_set */
};

/* ========================================================================
 * Cache Lifecycle
 * ======================================================================== */

apr_status_t napr_cache_open(napr_cache_t **cache_ptr, const char *path, apr_pool_t *pool)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    napr_cache_t *cache = NULL;
    const char *lock_path = NULL;
    apr_pool_t *cache_pool = NULL;

    if (!cache_ptr || !path || !pool) {
        return APR_EINVAL;
    }

    /* Create a sub-pool for the cache lifetime */
    status = apr_pool_create(&cache_pool, pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Allocate cache structure */
    cache = apr_pcalloc(cache_pool, sizeof(*cache));
    if (!cache) {
        apr_pool_destroy(cache_pool);
        return APR_ENOMEM;
    }

    cache->pool = cache_pool;

    /* ====================================================================
     * STEP 1: Process Exclusivity Lock (MUST HAPPEN FIRST - Spec 3.1)
     * ==================================================================== */

    /* Construct lock file path by appending ".lock" */
    lock_path = apr_psprintf(cache_pool, "%s.lock", path);
    if (!lock_path) {
        apr_pool_destroy(cache_pool);
        return APR_ENOMEM;
    }

    /* Open lock file (create if doesn't exist) */
    status = apr_file_open(&cache->lock_file_handle, lock_path, APR_FOPEN_CREATE | APR_FOPEN_READ | APR_FOPEN_WRITE, APR_FPROT_UREAD | APR_FPROT_UWRITE, cache_pool);
    if (status != APR_SUCCESS) {
        apr_pool_destroy(cache_pool);
        return status;
    }

    /* Acquire exclusive, non-blocking lock */
    status = apr_file_lock(cache->lock_file_handle, APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK);
    if (status != APR_SUCCESS) {
        /* Lock failed - another process has the cache open */
        apr_file_close(cache->lock_file_handle);
        apr_pool_destroy(cache_pool);
        return status;          /* Will be APR_EAGAIN or similar */
    }

    /* ====================================================================
     * STEP 2: Initialize napr_db Environment (Spec 7.2.1)
     * ==================================================================== */

    /* Create DB environment */
    status = napr_db_env_create(&cache->db_env, cache_pool);
    if (status != APR_SUCCESS) {
        goto error_cleanup;
    }

    /* Set mapsize to 10 Gigabytes */
    status = napr_db_env_set_mapsize(cache->db_env, CACHE_MAPSIZE);
    if (status != APR_SUCCESS) {
        goto error_cleanup;
    }

    /* Open DB with INTRAPROCESS_LOCK flag for optimization (Spec 3.2) */
    status = napr_db_env_open(cache->db_env, path, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    if (status != APR_SUCCESS) {
        goto error_cleanup;
    }

    /* ====================================================================
     * STEP 3: Initialize Mark-and-Sweep Structures
     * ==================================================================== */

    /* Create mutex for visited set */
    status = apr_thread_mutex_create(&cache->visited_mutex, APR_THREAD_MUTEX_DEFAULT, cache_pool);
    if (status != APR_SUCCESS) {
        goto error_cleanup;
    }

    /* Create hash table for visited paths */
    cache->visited_set = apr_hash_make(cache_pool);
    if (!cache->visited_set) {
        status = APR_ENOMEM;
        goto error_cleanup;
    }

    /* Success */
    *cache_ptr = cache;
    return APR_SUCCESS;

  error_cleanup:
    /* Clean up on error - release lock before returning */
    if (cache->db_env) {
        napr_db_env_close(cache->db_env);
    }
    if (cache->lock_file_handle) {
        apr_file_unlock(cache->lock_file_handle);
        apr_file_close(cache->lock_file_handle);
    }
    apr_pool_destroy(cache_pool);
    return status;
}

apr_status_t napr_cache_close(napr_cache_t *cache)
{
    apr_status_t status = APR_SUCCESS;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status_tmp;

    if (!cache) {
        return APR_EINVAL;
    }

    /* Close napr_db environment */
    if (cache->db_env) {
        status_tmp = napr_db_env_close(cache->db_env);
        if (status_tmp != APR_SUCCESS && status == APR_SUCCESS) {
            status = status_tmp;        /* Preserve first error */
        }
    }

    /* Release and close lock file */
    if (cache->lock_file_handle) {
        status_tmp = apr_file_unlock(cache->lock_file_handle);
        if (status_tmp != APR_SUCCESS && status == APR_SUCCESS) {
            status = status_tmp;
        }

        status_tmp = apr_file_close(cache->lock_file_handle);
        if (status_tmp != APR_SUCCESS && status == APR_SUCCESS) {
            status = status_tmp;
        }
    }

    /* Destroy the pool (also destroys mutex and hash table) */
    if (cache->pool) {
        apr_pool_destroy(cache->pool);
    }

    return status;
}

/* ========================================================================
 * Transaction Management
 * ======================================================================== */

apr_status_t napr_cache_begin_read(napr_cache_t *cache, apr_pool_t *pool)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    if (!cache || !pool) {
        return APR_EINVAL;
    }

    /* Begin read-only transaction on underlying DB */
    status = napr_db_txn_begin(cache->db_env, NAPR_DB_RDONLY, &cache->active_txn);
    if (status != APR_SUCCESS) {
        return status;
    }

    return APR_SUCCESS;
}

apr_status_t napr_cache_begin_write(napr_cache_t *cache, apr_pool_t *pool)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    DEBUG_DBG("[CACHE] Begin write transaction");

    if (!cache || !pool) {
        return APR_EINVAL;
    }

    /* Begin write transaction on underlying DB */
    status = napr_db_txn_begin(cache->db_env, 0, &cache->active_txn);
    if (status != APR_SUCCESS) {
        DEBUG_ERR("[CACHE] Failed to begin write transaction");
        return status;
    }

    DEBUG_DBG("[CACHE] Write transaction started, txn=%p", (void *) cache->active_txn);
    return APR_SUCCESS;
}

apr_status_t napr_cache_end_read(napr_cache_t *cache)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    if (!cache) {
        return APR_EINVAL;
    }

    if (!cache->active_txn) {
        return APR_EINVAL;      /* No active transaction */
    }

    /* Abort read transaction (no changes to commit) */
    status = napr_db_txn_abort(cache->active_txn);
    cache->active_txn = NULL;

    return status;
}

apr_status_t napr_cache_commit_write(napr_cache_t *cache)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    DEBUG_DBG("[CACHE] Committing write transaction");

    if (!cache) {
        return APR_EINVAL;
    }

    if (!cache->active_txn) {
        DEBUG_ERR("[CACHE] No active transaction to commit");
        return APR_EINVAL;      /* No active transaction */
    }

    DEBUG_DBG("[CACHE] Calling napr_db_txn_commit, txn=%p", (void *) cache->active_txn);

    /* Commit write transaction */
    status = napr_db_txn_commit(cache->active_txn);
    cache->active_txn = NULL;

    DEBUG_DBG("[CACHE] Commit result: %s", status == APR_SUCCESS ? "SUCCESS" : "FAILED");
    return status;
}

apr_status_t napr_cache_abort_write(napr_cache_t *cache)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    if (!cache) {
        return APR_EINVAL;
    }

    if (!cache->active_txn) {
        return APR_EINVAL;      /* No active transaction */
    }

    /* Abort write transaction */
    status = napr_db_txn_abort(cache->active_txn);
    cache->active_txn = NULL;

    return status;
}

/* ========================================================================
 * CRUD Operations
 * ======================================================================== */

apr_status_t napr_cache_lookup_in_txn(napr_cache_t *cache, const char *path, const napr_cache_entry_t **entry_ptr)
{
    napr_db_val_t key;
    napr_db_val_t value;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    if (!cache || !path || !entry_ptr) {
        return APR_EINVAL;
    }

    if (!cache->active_txn) {
        return APR_EINVAL;      /* No active transaction */
    }

    /* Initialize output to NULL */
    *entry_ptr = NULL;

    /* Prepare key */
    key.data = (void *) path;
    key.size = strlen(path);

    /* Lookup in database */
    status = napr_db_get(cache->active_txn, &key, &value);
    if (status != APR_SUCCESS) {
        return status;          /* APR_NOTFOUND if key doesn't exist */
    }

    /* CRITICAL VALIDATION: Ensure zero-copy safety */
    if (value.size != sizeof(napr_cache_entry_t)) {
        /* Database corruption or version mismatch */
        return APR_EGENERAL;
    }

    /* Zero-copy: return direct pointer to mmap'd data */
    *entry_ptr = (const napr_cache_entry_t *) value.data;

    return APR_SUCCESS;
}

apr_status_t napr_cache_upsert_in_txn(napr_cache_t *cache, const char *path, const napr_cache_entry_t *entry)
{
    napr_db_val_t key;
    napr_db_val_t value;

    if (!cache || !path || !entry) {
        return APR_EINVAL;
    }

    if (!cache->active_txn) {
        DEBUG_ERR("[CACHE_UPSERT] No active transaction!");
        return APR_EINVAL;      /* No active transaction */
    }

    DEBUG_DBG("[CACHE_UPSERT] path=%s, txn=%p", path, (void *) cache->active_txn);

    /* Prepare key (file path) */
    key.data = (void *) path;
    key.size = strlen(path);

    /* Prepare value (cache entry) */
    value.data = (void *) entry;
    value.size = sizeof(napr_cache_entry_t);

    /* Insert or update in database */
    return napr_db_put(cache->active_txn, &key, &value);
}

/* ========================================================================
 * Mark-and-Sweep Garbage Collection
 * ======================================================================== */

apr_status_t napr_cache_mark_visited(napr_cache_t *cache, const char *path)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    char *duplicated_path = NULL;

    if (!cache || !path) {
        return APR_EINVAL;
    }

    /* Acquire mutex for thread-safe access to visited_set */
    status = apr_thread_mutex_lock(cache->visited_mutex);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* CRITICAL: Duplicate the path into the cache's main pool
     * This ensures the string remains valid even if the caller's pool is destroyed */
    duplicated_path = apr_pstrdup(cache->pool, path);
    if (!duplicated_path) {
        /* Release mutex before returning error */
        apr_thread_mutex_unlock(cache->visited_mutex);
        return APR_ENOMEM;
    }

    /* Insert the duplicated path into visited_set
     * Using the duplicated string as the key ensures proper memory ownership */
    apr_hash_set(cache->visited_set, duplicated_path, APR_HASH_KEY_STRING, duplicated_path);

    /* Release mutex */
    status = apr_thread_mutex_unlock(cache->visited_mutex);
    if (status != APR_SUCCESS) {
        return status;
    }

    return APR_SUCCESS;
}

apr_status_t napr_cache_sweep(napr_cache_t *cache)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    apr_pool_t *sweep_pool = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t key;
    napr_db_val_t data;
    int first_iteration = 1;

    if (!cache) {
        return APR_EINVAL;
    }

    /* ====================================================================
     * STEP 1: Create local pool for transaction/cursor resources
     * ==================================================================== */
    status = apr_pool_create(&sweep_pool, cache->pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* ====================================================================
     * STEP 2: Begin single write transaction
     * ==================================================================== */
    status = napr_cache_begin_write(cache, sweep_pool);
    if (status != APR_SUCCESS) {
        apr_pool_destroy(sweep_pool);
        return status;
    }

    /* ====================================================================
     * STEP 3: Open cursor
     * ==================================================================== */
    status = napr_db_cursor_open(cache->active_txn, &cursor);
    if (status != APR_SUCCESS) {
        goto error_abort;
    }

    /* ====================================================================
     * STEP 4: Iterate entire database using DB_FIRST then DB_NEXT
     * ==================================================================== */
    while (1) {
        /* Get next entry (DB_FIRST on first iteration, DB_NEXT afterwards) */
        if (first_iteration) {
            status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_FIRST);
            first_iteration = 0;
        }
        else {
            status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_NEXT);
        }

        /* End of iteration */
        if (status == APR_NOTFOUND) {
            break;
        }

        /* Handle other errors */
        if (status != APR_SUCCESS) {
            goto error_close_cursor;
        }

        /* ================================================================
         * STEP 5: Check if key exists in visited_set
         * ================================================================ */
        void *found = apr_hash_get(cache->visited_set, key.data, (apr_ssize_t) key.size);

        /* ================================================================
         * STEP 6: If NOT found, delete the entry
         * ================================================================ */
        if (!found) {
            status = napr_db_del(cache->active_txn, &key, NULL);
            if (status != APR_SUCCESS) {
                goto error_close_cursor;
            }
        }
    }

    /* ====================================================================
     * STEP 7: Close cursor
     * ==================================================================== */
    status = napr_db_cursor_close(cursor);
    if (status != APR_SUCCESS) {
        goto error_abort;
    }

    /* ====================================================================
     * STEP 8: Commit transaction
     * ==================================================================== */
    status = napr_cache_commit_write(cache);
    if (status != APR_SUCCESS) {
        apr_pool_destroy(sweep_pool);
        return status;
    }

    /* ====================================================================
     * STEP 10: Clear visited_set
     * ==================================================================== */
    apr_hash_clear(cache->visited_set);

    /* ====================================================================
     * STEP 11: Destroy local pool
     * ==================================================================== */
    apr_pool_destroy(sweep_pool);

    return APR_SUCCESS;

  error_close_cursor:
    /* Close cursor before aborting transaction */
    napr_db_cursor_close(cursor);

  error_abort:
    /* STEP 9: Abort transaction on error */
    napr_cache_abort_write(cache);
    apr_pool_destroy(sweep_pool);
    return status;
}
