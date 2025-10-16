/**
 * @file napr_hash.h
 * @brief A high-performance hash table implementation built on APR.
 * @ingroup DataStructures
 */
/*
 * Copyright (C) 2007 Francois Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NAPR_HASH_H
#define NAPR_HASH_H

#include <apr.h>
#include <apr_pools.h>

/**
 * @brief Opaque hash table structure.
 */
typedef struct napr_hash_t napr_hash_t;

/**
 * @brief Opaque hash table iterator structure.
 */
typedef struct napr_hash_index_t napr_hash_index_t;

/**
 * @brief Callback function to extract the key from a stored data item.
 * @param[in] data A pointer to the stored data item.
 * @return A pointer to the key.
 */
typedef const void *(get_key_callback_fn_t) (const void *);

/**
 * @brief Callback function to get the length of a key.
 * @param[in] data A pointer to the stored data item.
 * @return The length of the key.
 */
typedef apr_size_t (get_key_len_callback_fn_t) (const void *);

/**
 * @brief Callback function to compare two keys.
 * @param[in] key1 The first key.
 * @param[in] key2 The second key.
 * @param[in] len The length to compare (can be ignored if keys are null-terminated).
 * @return An integer less than, equal to, or greater than zero if key1 is found,
 *         respectively, to be less than, to match, or be greater than key2.
 */
typedef int (key_cmp_callback_fn_t) (const void *, const void *, apr_size_t);

/**
 * @brief Callback function to compute the hash value of a key.
 * @param[in] key The key to hash.
 * @param[in] klen A pointer to the key's length.
 * @return The computed hash value.
 */
typedef apr_uint32_t (hash_callback_fn_t) (register const void *, register apr_size_t);

/**
 * @brief Arguments for creating a napr_hash_t.
 *
 * This structure encapsulates all the parameters needed to create a hash table,
 * improving readability and type safety by avoiding long, ambiguous parameter lists.
 */
typedef struct napr_hash_create_args_t
{
    apr_pool_t *pool;                   /**< The pool to use for all allocations. */
    apr_size_t nel;                     /**< The expected number of elements to store. */
    apr_size_t ffactor;                 /**< The desired filling factor (density). */
    get_key_callback_fn_t *get_key;      /**< Function to get the key from a datum. */
    get_key_len_callback_fn_t *get_key_len; /**< Function to get the key length from a datum. */
    key_cmp_callback_fn_t *key_cmp;      /**< Function to compare two keys. */
    hash_callback_fn_t *hash;            /**< The hash function to use. */
} napr_hash_create_args_t;

/**
 * Create a new hash table from a structure of arguments.
 * @param args A pointer to the napr_hash_create_args_t structure.
 * @return A pointer to the new hash table.
 */
napr_hash_t *napr_hash_make_ex(napr_hash_create_args_t * args);

/**
 * @brief Creates a new hash table (deprecated).
 * @deprecated Please use napr_hash_make_ex with napr_hash_create_args_t instead.
 * @see napr_hash_make_ex
 */
napr_hash_t *napr_hash_make(apr_pool_t *pool, apr_size_t nel, apr_size_t ffactor, get_key_callback_fn_t get_key,
                            get_key_len_callback_fn_t get_key_len, key_cmp_callback_fn_t key_cmp, hash_callback_fn_t hash)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((deprecated("Use napr_hash_make_ex instead")))
#endif
    ;

/**
 * @brief Create a hash table optimized for storing C strings as keys.
 * @param[in] pool The pool to allocate the hash table from.
 * @param[in] nel The desired number of pre-allocated buckets.
 * @param[in] ffactor The maximum number of collisions for a given key.
 * @return A pointer to the newly created string-keyed hash table.
 */
     napr_hash_t *napr_hash_str_make(apr_pool_t *pool, apr_size_t nel, apr_size_t ffactor);

/**
 * @brief Searches the hash table for an item.
 * @param[in] hash The hash table to search.
 * @param[in] key The key to search for.
 * @param[in] key_len The length of the key.
 * @param[out] hash_value A pointer to store the computed hash of the key. Can be NULL.
 * @return A pointer to the found data item, or NULL if not found. If an item is not found,
 *         the computed hash value is stored in `hash_value` for efficient subsequent insertion.
 */
     void *napr_hash_search(napr_hash_t *hash, const void *key, apr_size_t key_len, apr_uint32_t *hash_value);

/**
 * @brief Removes an item from the hash table.
 * @param[in] hash The hash table.
 * @param[in] data The data item to remove.
 * @param[in] hash_value The pre-computed hash of the item's key.
 */
     void napr_hash_remove(napr_hash_t *hash, void *data, apr_uint32_t hash_value);

/**
 * @brief Inserts or updates an item in the hash table.
 * @param[in] hash The hash table.
 * @param[in] data The data item to insert.
 * @param[in] hash_value The pre-computed hash of the item's key.
 * @return APR_SUCCESS on success.
 */
     apr_status_t napr_hash_set(napr_hash_t *hash, void *data, apr_uint32_t hash_value);

/**
 * @brief Get a pointer to the pool from which the hash table was allocated.
 * @param[in] thehash The hash table.
 * @return The APR pool.
 */
     apr_pool_t *napr_hash_pool_get(const napr_hash_t *thehash);

/**
 * @brief Start iterating over the entries in a hash table.
 * @param[in] pool The pool to allocate the iterator from. If NULL, a non-thread-safe internal iterator is used.
 * @param[in] hash The hash table to iterate over.
 * @return A pointer to the iterator.
 * @remark There is no restriction on adding or deleting hash entries during
 * an iteration, but the results may be unpredictable unless only deleting the current entry.
 * @code
 * napr_hash_index_t *hi;
 * void *val;
 * for (hi = napr_hash_first(p, ht); hi; hi = napr_hash_next(hi)) {
 *     napr_hash_this(hi, NULL, NULL, &val);
 *     // process val
 * }
 * @endcode
 */
     napr_hash_index_t *napr_hash_first(apr_pool_t *pool, napr_hash_t *hash);

/**
 * @brief Continue iterating over the entries in a hash table.
 * @param[in] index The current iteration state.
 * @return A pointer to the next iteration state, or NULL if there are no more entries.
 */
     napr_hash_index_t *napr_hash_next(napr_hash_index_t *index);

/**
 * @brief Get the current entry's details from the iteration state.
 * @param[in] hash_index The iteration state.
 * @param[out] key Pointer to store the key. Can be NULL.
 * @param[out] klen Pointer to store the key length. Can be NULL.
 * @param[out] val Pointer to store the value (the data item). Can be NULL.
 */
     void napr_hash_this(napr_hash_index_t *hash_index, const void **key, apr_size_t *klen, void **val);

#endif /* NAPR_HASH_H */
