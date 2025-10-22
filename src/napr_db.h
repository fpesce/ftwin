/**
 * @file napr_db.h
 * @brief Public API for napr_db - LMDB-style embedded key-value store
 *
 * napr_db is a high-performance, embedded, transactional key-value store
 * built on APR, following the LMDB architecture with:
 * - Memory-mapped I/O and zero-copy reads
 * - B+ Tree indexing with Copy-on-Write transactions
 * - MVCC for lock-free reads
 * - Single-writer/multiple-reader concurrency (SWMR)
 */

#ifndef NAPR_DB_H
#define NAPR_DB_H

#include <apr_pools.h>
#include <apr_errno.h>
#include <apr_file_io.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Generic data value structure for keys and values.
 *
 * Zero-copy semantics: data pointer points directly into the memory map.
 * The data is valid only for the lifetime of the transaction.
 */
    typedef struct napr_db_val_t
    {
        apr_size_t size;
                      /**< Size of data in bytes */
        void *data;   /**< Pointer to data */
    } napr_db_val_t;

/**
 * @brief Opaque handle for database environment.
 *
 * Represents an opened database file with its memory mapping.
 */
    typedef struct napr_db_env_t napr_db_env_t;

/**
 * @brief Opaque handle for a database transaction.
 *
 * Transactions provide ACID guarantees with MVCC snapshot isolation.
 */
    typedef struct napr_db_txn_t napr_db_txn_t;

/**
 * @brief Opaque handle for a database cursor.
 *
 * Cursors allow iteration over key-value pairs.
 */
    typedef struct napr_db_cursor_t napr_db_cursor_t;

/*
 * Environment and transaction flags
 */

/** Open database in read-only mode */
#define NAPR_DB_RDONLY             0x0001

/** Create database if it doesn't exist */
#define NAPR_DB_CREATE             0x0002

/**
 * Use intra-process locking (apr_thread_mutex_t) instead of
 * inter-process locking (apr_proc_mutex_t).
 *
 * This is an optimization for single-process, multi-threaded usage
 * (e.g., napr_cache). Default is inter-process locking.
 */
#define NAPR_DB_INTRAPROCESS_LOCK  0x0004

/*
 * Cursor operations
 */

/** Position cursor at first key */
#define NAPR_DB_FIRST              0

/** Position cursor at last key */
#define NAPR_DB_LAST               1

/** Move cursor to next key */
#define NAPR_DB_NEXT               2

/** Move cursor to previous key */
#define NAPR_DB_PREV               3

/** Position cursor at specified key (exact match) */
#define NAPR_DB_SET                4

/** Position cursor at key >= specified key */
#define NAPR_DB_SET_RANGE          5

/** Return key/data at current cursor position */
#define NAPR_DB_GET_CURRENT        6

/*
 * Environment management
 */

/**
 * @brief Create a database environment handle.
 *
 * @param env Pointer to receive the environment handle
 * @param pool APR pool for allocations
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_env_create(napr_db_env_t ** env, apr_pool_t *pool);

/**
 * @brief Set the memory map size for the database.
 *
 * Must be called before napr_db_env_open(). The size should be set to
 * accommodate the expected database growth.
 *
 * @param env Database environment handle
 * @param size Memory map size in bytes
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_env_set_mapsize(napr_db_env_t * env, apr_size_t size);

/**
 * @brief Open a database environment.
 *
 * Opens and memory-maps the database file.
 *
 * @param env Database environment handle
 * @param path Path to database file
 * @param flags Flags (NAPR_DB_RDONLY, NAPR_DB_CREATE, NAPR_DB_INTRAPROCESS_LOCK)
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_env_open(napr_db_env_t * env, const char *path, unsigned int flags);

/**
 * @brief Close a database environment.
 *
 * Unmaps memory and closes file handles. Any open transactions or cursors
 * must be closed before calling this.
 *
 * @param env Database environment handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_env_close(napr_db_env_t * env);

/*
 * Transaction management
 */

/**
 * @brief Begin a new transaction.
 *
 * Read transactions are lock-free and provide a consistent snapshot.
 * Write transactions are serialized (SWMR).
 *
 * @param env Database environment
 * @param flags Transaction flags (NAPR_DB_RDONLY for read transaction)
 * @param txn Pointer to receive transaction handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_txn_begin(napr_db_env_t * env, unsigned int flags, napr_db_txn_t ** txn);

/**
 * @brief Commit a transaction.
 *
 * For write transactions, atomically commits all changes.
 * For read transactions, simply releases the snapshot.
 *
 * @param txn Transaction handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_txn_commit(napr_db_txn_t * txn);

/**
 * @brief Abort a transaction.
 *
 * Discards all changes and releases resources.
 *
 * @param txn Transaction handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_txn_abort(napr_db_txn_t * txn);

/*
 * Data access operations
 */

/**
 * @brief Get a value by key.
 *
 * Returns a zero-copy pointer to the value in the memory map.
 * The data is valid only for the lifetime of the transaction.
 *
 * @param txn Transaction handle
 * @param key Key to look up
 * @param data Pointer to receive value (output)
 * @return APR_SUCCESS, APR_NOTFOUND, or error code
 */
    apr_status_t napr_db_get(napr_db_txn_t * txn, napr_db_val_t * key, napr_db_val_t * data);

/**
 * @brief Store a key-value pair.
 *
 * Inserts or updates a key-value pair. Only valid in write transactions.
 *
 * @param txn Transaction handle (must be write transaction)
 * @param key Key to store
 * @param data Value to store
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_put(napr_db_txn_t * txn, napr_db_val_t * key, napr_db_val_t * data);

/**
 * @brief Delete a key-value pair.
 *
 * Only valid in write transactions.
 *
 * @param txn Transaction handle (must be write transaction)
 * @param key Key to delete
 * @param data Optional: if non-NULL, only delete if value matches
 * @return APR_SUCCESS, APR_NOTFOUND, or error code
 */
    apr_status_t napr_db_del(napr_db_txn_t * txn, napr_db_val_t * key, napr_db_val_t * data);

/*
 * Cursor operations
 */

/**
 * @brief Open a cursor for iteration.
 *
 * @param txn Transaction handle
 * @param cursor Pointer to receive cursor handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_cursor_open(napr_db_txn_t * txn, napr_db_cursor_t ** cursor);

/**
 * @brief Close a cursor.
 *
 * @param cursor Cursor handle
 * @return APR_SUCCESS or error code
 */
    apr_status_t napr_db_cursor_close(napr_db_cursor_t * cursor);

/**
 * @brief Retrieve key-value pairs using cursor operations.
 *
 * @param cursor Cursor handle
 * @param key Pointer to receive key (output, may be NULL)
 * @param data Pointer to receive value (output, may be NULL)
 * @param operation Cursor operation (NAPR_DB_FIRST, NAPR_DB_NEXT, etc.)
 * @return APR_SUCCESS, APR_NOTFOUND, or error code
 */
    apr_status_t napr_db_cursor_get(napr_db_cursor_t * cursor, napr_db_val_t * key, napr_db_val_t * data, int operation);

#ifdef __cplusplus
}
#endif

#endif                          /* NAPR_DB_H */
