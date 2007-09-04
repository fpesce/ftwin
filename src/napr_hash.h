#ifndef NAPR_HASH_H
#define NAPR_HASH_H

#include <apr.h>
#include <apr_pools.h>
typedef struct napr_hash_t napr_hash_t;
typedef struct napr_hash_index_t napr_hash_index_t;

typedef const void *(get_key_callback_fn_t) (const void *);
typedef apr_size_t (get_key_len_callback_fn_t) (const void *);
typedef int (key_cmp_callback_fn_t) (const void *, const void *, apr_size_t);
typedef apr_uint32_t (hash_callback_fn_t) (register const void *, register apr_size_t);
typedef apr_status_t (function_callback_fn_t) (const void *, void *);

/** 
 * Create a hash table with a custom hash function.
 * @param pool The pool to allocate the hash table out of
 * @param nel The desired number of preallocated buckets (the true size will be
 *	      readjusted to a power of 2).
 * @param ffactor The maximum number of collision for a given key (if too low,
 *                structure will make the whole table grow hugely) correct
 *                value around 5-10.
 * @param get_key A custom "extract key from data" function.
 * @param get_key_len A custom "extract len of the key from data" function.
 * @param key_cmp A custom cmp function.
 * @param hash A custom hash function.
 * 
 * @return The hash table just created.
 */
napr_hash_t *napr_hash_make(apr_pool_t *pool, apr_size_t nel, apr_size_t ffactor, get_key_callback_fn_t get_key,
			    get_key_len_callback_fn_t get_key_len, key_cmp_callback_fn_t key_cmp, hash_callback_fn_t hash);

/** 
 * Create an hash table to store strings.
 * @param pool The pool to allocate the hash table out of
 * @param nel The desired number of preallocated buckets (the true size will be
 *	      readjusted to a power of 2).
 * @param ffactor The maximum number of collision for a given key (if too low,
 *                structure will make the whole table grow hugely) correct
 *                value around 5-10.
 * @return The hash table just created.
 */
napr_hash_t *napr_hash_str_make(apr_pool_t *pool, apr_size_t nel, apr_size_t ffactor);

/** 
 * searches the hash table for an item with the same key than provided as
 * parameter.
 * @param hash The hash table your working on.
 * @param key The key to hash.
 * @param key_len The length of the key.
 * @param hash_value A pointer that will be filled with the hash(key) result.
 * @return If a matching key is found, returns a pointer to the datum, else
 *         store the hash value in hash, to not re-hash datum if this is a
 *         napr_hash_search  before a  napr_hash_set.
 * @remark hash_value may be null.
 */
void *napr_hash_search(napr_hash_t *hash, const void *key, apr_size_t key_len, apr_uint32_t *hash_value);

void napr_hash_remove(napr_hash_t *hash, void *data, apr_uint32_t hash_value);
apr_status_t napr_hash_set(napr_hash_t *hash, void *data, apr_uint32_t hash_value);
apr_status_t napr_hash_apply_function(const napr_hash_t *hash, function_callback_fn_t function, void *param);
apr_size_t napr_hash_get_size(const napr_hash_t *hash);
apr_size_t napr_hash_get_nel(const napr_hash_t *hash);

/**
 * Get a pointer to the pool which the hash table was created in.
 */
apr_pool_t *napr_hash_pool_get(const napr_hash_t *thehash);

/**
 * Start iterating over the entries in a hash table.
 * @param p The pool to allocate the apr_hash_index_t iterator. If this
 *          pool is NULL, then an internal, non-thread-safe iterator is used.
 * @param ht The hash table
 * @remark  There is no restriction on adding or deleting hash entries during
 * an iteration (although the results may be unpredictable unless all you do
 * is delete the current entry) and multiple iterations can be in
 * progress at the same time.
 *          * @example
 */
/**
 * <PRE>
 * 
 * int sum_values(apr_pool_t *p, apr_hash_t *ht)
 * {
 *     apr_hash_index_t *hi;
 *     void *val;
 *     int sum = 0;
 *     for (hi = apr_hash_first(p, ht); hi; hi = apr_hash_next(hi)) {
 *         apr_hash_this(hi, NULL, NULL, &val);
 *         sum += *(int *)val;
 *     }
 *     return sum;
 * }
 * </PRE> .
 */
napr_hash_index_t *napr_hash_first(apr_pool_t *pool, napr_hash_t *hash);

/**
 * Continue iterating over the entries in a hash table.
 * @param hi The iteration state
 * @return a pointer to the updated iteration state.  NULL if there are no more  
 *         entries.
 */
napr_hash_index_t *napr_hash_next(napr_hash_index_t *index);

/**
 * Get the current entry's details from the iteration state.
 * @param hi The iteration state
 * @param key Return pointer for the pointer to the key.
 * @param klen Return pointer for the key length.
 * @param val Return pointer for the associated value.
 * @remark The return pointers should point to a variable that will be set to the
 *         corresponding data, or they may be NULL if the data isn't interesting.
 */
void apr_hash_this(napr_hash_index_t *hi, const void **key, apr_size_t *klen, void **val);

#endif /* NAPR_HASH_H */
