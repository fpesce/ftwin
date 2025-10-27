/**
 * @file napr_cache.h
 * @brief Filesystem Hash Cache - Persistent cache for file metadata and hashes
 *
 * `napr_cache` is a high-performance, persistent cache built on `napr_db`.
 * It stores filesystem metadata (mtime, ctime, size) and XXH128 hashes,
 * optimized for duplicate file finding by avoiding redundant hash computations.
 *
 * PLATFORM DEPENDENCY WARNING:
 * =========================
 * The cache file format stores binary structures directly using zero-copy
 * semantics. This means the cache files are PLATFORM-DEPENDENT and should
 * NOT be shared between:
 * - Different CPU architectures (x86_64, ARM, etc.)
 * - Different operating systems
 * - Systems with different structure padding/alignment
 * - 32-bit vs 64-bit systems
 *
 * Cache files should be considered local to the machine that created them.
 */

#ifndef NAPR_CACHE_H
#define NAPR_CACHE_H

#include <apr_pools.h>
#include <apr_errno.h>
#include <apr_time.h>
#include <apr_file_io.h>

#include "napr_db.h"
#include <xxhash.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Cache entry structure (40 bytes)
 *
 * This structure is stored directly in the memory-mapped database file
 * without serialization (zero-copy). The exact size must be 40 bytes.
 *
 * CRITICAL: Any changes to this structure will break backward compatibility
 * and require cache invalidation.
 */
    typedef struct napr_cache_entry_t
    {
        apr_time_t mtime;  /**< File modification time */
        apr_time_t ctime;  /**< File status change time */
        apr_off_t size;    /**< File size in bytes */
        XXH128_hash_t hash;/**< XXH128 hash of file content */
    } napr_cache_entry_t;

/**
 * @brief Opaque cache handle
 */
    typedef struct napr_cache_t napr_cache_t;

/**
 * @brief Open or create a cache file
 *
 * This function:
 * - Acquires an exclusive process lock on the cache file
 * - Initializes the underlying napr_db with 10GB mapsize
 * - Uses intra-process locking for optimal performance
 * - Initializes mark-and-sweep structures
 *
 * Only one process can access a cache file at a time.
 *
 * @param cache_ptr Output pointer to receive cache handle
 * @param path Absolute path to cache file
 * @param pool APR pool for allocations
 * @return APR_SUCCESS or error code (APR_EBUSY if already locked)
 */
    apr_status_t napr_cache_open(napr_cache_t **cache_ptr, const char *path, apr_pool_t *pool);

/**
 * @brief Close the cache and release all resources
 *
 * @param cache Cache handle to close
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_cache_close(napr_cache_t *cache);

/**
 * @brief Begin a read transaction
 *
 * @param cache Cache handle
 * @param pool APR pool for transaction allocations
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_cache_begin_read(napr_cache_t *cache, apr_pool_t *pool);

/**
 * @brief Begin a write transaction
 *
 * @param cache Cache handle
 * @param pool APR pool for transaction allocations
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_cache_begin_write(napr_cache_t *cache, apr_pool_t *pool);

/**
 * @brief End a read transaction
 *
 * @param cache Cache handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_cache_end_read(napr_cache_t *cache);

/**
 * @brief Commit a write transaction
 *
 * @param cache Cache handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_cache_commit_write(napr_cache_t *cache);

/**
 * @brief Abort a write transaction
 *
 * @param cache Cache handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_cache_abort_write(napr_cache_t *cache);

/**
 * @brief Lookup a cache entry within a transaction
 *
 * This function returns a zero-copy pointer directly into the memory map.
 * The returned pointer is only valid until the transaction ends.
 *
 * @param cache Cache handle
 * @param path Absolute file path (key)
 * @param entry_ptr Output pointer to receive entry (zero-copy)
 * @return APR_SUCCESS, APR_NOTFOUND, or error code
 */
    apr_status_t napr_cache_lookup_in_txn(napr_cache_t *cache, const char *path, const napr_cache_entry_t **entry_ptr);

/**
 * @brief Insert or update a cache entry within a transaction
 *
 * @param cache Cache handle
 * @param path Absolute file path (key)
 * @param entry Entry data to store
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_cache_upsert_in_txn(napr_cache_t *cache, const char *path, const napr_cache_entry_t *entry);

/**
 * @brief Mark a path as visited (thread-safe)
 *
 * Used during the mark phase of mark-and-sweep garbage collection.
 * Multiple threads can call this concurrently. The path is duplicated
 * into the cache's pool for safe storage.
 *
 * @param cache Cache handle
 * @param path Absolute file path to mark
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_cache_mark_visited(napr_cache_t *cache, const char *path);

/**
 * @brief Remove all cache entries not marked as visited
 *
 * Used during the sweep phase of mark-and-sweep garbage collection.
 * This function:
 * - Opens a single write transaction
 * - Iterates all entries using a cursor
 * - Deletes entries not found in the visited set
 * - Clears the visited set after sweep
 *
 * This function is NOT thread-safe and should be called by a single thread
 * after all marking is complete.
 *
 * @param cache Cache handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_cache_sweep(napr_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif                          /* NAPR_CACHE_H */
