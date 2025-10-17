/**
 * @file napr_hash.c
 * @brief Implementation of the high-performance hash table.
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

#include "debug.h"
#include "ft_constants.h"
#include "napr_hash.h"

#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"

enum
{
    XXH32_SEED = 0
};

#define hashsize(n) ((apr_size_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)

static const void *str_get_key(const void *opaque)
{
    return opaque;
}

static apr_size_t str_get_key_len(const void *opaque)
{
    const char *str = opaque;
    return strlen(str);
}

static int str_key_cmp(const void *opaque1, const void *opaque2, apr_size_t len)
{
    const char *str1 = opaque1;
    const char *str2 = opaque2;

    return memcmp(str1, str2, len);
}

static apr_uint32_t str_hash(register const void *opaque, register apr_size_t len)
{
    /* Use XXH32 for string hashing - faster and more consistent with file hashing */
    return XXH32(opaque, len, XXH32_SEED);
}

struct napr_hash_index_t
{
    napr_hash_t *hash;
    apr_size_t bucket;
    apr_size_t element;         /* of a bucket */
};

struct napr_hash_t
{
    /* void *table[size][ffactor] */
    void ***table;
    /* apr_size_t table[size] contain the number of element for each bucket */
    apr_size_t *filling_table;
    /* parent pool */
    apr_pool_t *pool;
    /* own pool that will be cleaned if a grow of the table occured */
    apr_pool_t *own_pool;
    /* Function to get the key from the datum */
    get_key_callback_fn_t *get_key;
    /* Function to get the key len from the datum */
    get_key_len_callback_fn_t *get_key_len;
    /* Function to compare two keys */
    key_cmp_callback_fn_t *key_cmp;
    /* hash function */
    hash_callback_fn_t *hash;

    /* the number of element contained in all the buckets of the table */
    apr_size_t nel;
    /* the number of buckets */
    apr_size_t size;
    /* desired density */
    apr_size_t ffactor;
    /* the binary mask to apply to the result of a hash function that return a
     * number < size */
    apr_uint32_t mask;
    /* size of the hash is hashsize(power) as mask is hashmask(power) */
    unsigned char power;
};

napr_hash_t *napr_hash_str_make(apr_pool_t *pool, apr_size_t nel, apr_size_t ffactor)
{
    napr_hash_create_args_t args = {
        .pool = pool,
        .nel = nel,
        .ffactor = ffactor,
        .get_key = str_get_key,
        .get_key_len = str_get_key_len,
        .key_cmp = str_key_cmp,
        .hash = str_hash
    };
    return napr_hash_make_ex(&args);
}

napr_hash_t *napr_hash_make(apr_pool_t *pool, apr_size_t nel, apr_size_t ffactor, get_key_callback_fn_t get_key,
                            get_key_len_callback_fn_t get_key_len, key_cmp_callback_fn_t key_cmp, hash_callback_fn_t hash)
{
    napr_hash_create_args_t args = {
        .pool = pool,
        .nel = nel,
        .ffactor = ffactor,
        .get_key = get_key,
        .get_key_len = get_key_len,
        .key_cmp = key_cmp,
        .hash = hash
    };
    return napr_hash_make_ex(&args);
}

napr_hash_t *napr_hash_make_ex(napr_hash_create_args_t * args)
{
    napr_hash_t *result = NULL;
    apr_status_t status = APR_SUCCESS;

    result = apr_pcalloc(args->pool, sizeof(struct napr_hash_t));
    if (NULL == result) {
        DEBUG_ERR("allocation error");
        return NULL;
    }

    while (hashsize(result->power) < args->nel) {
        result->power++;
    }

    result->size = hashsize(result->power);
    result->mask = hashmask(result->power);
    result->ffactor = args->ffactor;
    result->get_key = args->get_key;
    result->get_key_len = args->get_key_len;
    result->key_cmp = args->key_cmp;
    result->hash = args->hash;
    result->pool = args->pool;

    status = apr_pool_create(&(result->own_pool), args->pool);
    if (APR_SUCCESS != status) {
        char errbuf[ERR_BUF_SIZE];
        DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return NULL;
    }
    /*DEBUG_DBG("readjusting size to %" APR_SIZE_T_FMT " to store %" APR_SIZE_T_FMT " elements", result->size, nel); */
    /*DEBUG_DBG("bit mask will be 0x%x", result->mask); */

    result->table = (void ***) apr_pcalloc(result->own_pool, result->size * sizeof(void **));
    if (NULL == result->table) {
        DEBUG_ERR("allocation error");
        return NULL;
    }

    result->filling_table = apr_pcalloc(result->own_pool, result->size * sizeof(apr_size_t));
    if (NULL == result->filling_table) {
        DEBUG_ERR("allocation error");
        return NULL;
    }

    return result;
}

extern void *napr_hash_search(napr_hash_t *hash, const void *key, apr_size_t key_len, apr_uint32_t *hash_value)
{
    apr_uint32_t key_hash = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_size_t bucket_index;
    apr_size_t nel = 0;
    apr_size_t bucket = 0;

    key_hash = hash->hash(key, key_len);

    if (NULL != hash_value) {
        *hash_value = key_hash;
    }

    bucket = key_hash & hash->mask;
    nel = hash->filling_table[bucket];
    if (0 != nel) {
        for (bucket_index = 0; bucket_index < nel; bucket_index++) {
            /*DEBUG_DBG( "key[%p] bucket[%"APR_SIZE_T_FMT"][%"APR_SIZE_T_FMT"]=[%p]", key, bucket, bucket_index, hash->get_key(hash->table[bucket][bucket_index])); */
            if (key_len == hash->get_key_len(hash->table[bucket][bucket_index])) {
                if (0 == (hash->key_cmp(key, hash->get_key(hash->table[bucket][bucket_index]), key_len))) {
                    return hash->table[bucket][bucket_index];
                }
            }
        }
    }

    return NULL;
}

static inline apr_status_t napr_hash_rebuild(napr_hash_t *hash)
{
    napr_hash_t *tmp = NULL;
    apr_size_t index = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_size_t sub_index;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    napr_hash_create_args_t args = {
        .pool = hash->pool,
        .nel = hashsize(hash->power + 1),
        .ffactor = hash->ffactor,
        .get_key = hash->get_key,
        .get_key_len = hash->get_key_len,
        .key_cmp = hash->key_cmp,
        .hash = hash->hash
    };

    tmp = napr_hash_make_ex(&args);
    if (NULL == tmp) {
        DEBUG_ERR("error calling napr_hash_init");
        return APR_EGENERAL;
    }

    for (index = 0; index < hash->size; index++) {
        for (sub_index = 0; sub_index < hash->filling_table[index]; sub_index++) {
            /*
             * no need to do doublon test here as we take data directly from a
             * hash table
             */
            status = napr_hash_set(tmp, hash->table[index][sub_index], hash->hash(hash->get_key(hash->table[index][sub_index]), hash->get_key_len(hash->table[index][sub_index])));
            if (APR_SUCCESS != status) {
                char errbuf[ERR_BUF_SIZE];
                DEBUG_ERR("error calling napr_hash_set: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
                return status;
            }
        }
    }
    hash->table = tmp->table;
    hash->filling_table = tmp->filling_table;
    hash->nel = tmp->nel;
    hash->size = tmp->size;
    hash->mask = tmp->mask;
    hash->power = tmp->power;
    apr_pool_destroy(hash->own_pool);
    hash->own_pool = tmp->own_pool;

    return APR_SUCCESS;
}

extern void napr_hash_remove(napr_hash_t *hash, void *data, apr_uint32_t hash_value)
{
    apr_size_t nel = 0;
    apr_size_t bucket = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_size_t index;
    apr_size_t key_len = 0;
    const void *key = NULL;

    bucket = hash_value & hash->mask;
    key = hash->get_key(data);
    key_len = hash->get_key_len(data);
    nel = hash->filling_table[bucket];
    if (0 != nel) {
        for (index = 0; index < nel; index++) {
            //DEBUG_DBG( "key[%.*s] bucket[%i]=[%.*s]", key_len, key, index, hash->get_key_len(hash->table[bucket][index]), hash->get_key(hash->table[bucket][index]));
            if (key_len == hash->get_key_len(hash->table[bucket][index])) {
                if (0 == (hash->key_cmp(key, hash->get_key(hash->table[bucket][index]), key_len))) {
                    /* Remove it, by replacing with the last element if present */
                    if (index != nel - 1) {
                        hash->table[bucket][index] = hash->table[bucket][nel - 1];
                        hash->table[bucket][nel - 1] = NULL;
                    }
                    else {
                        hash->table[bucket][index] = NULL;
                    }
                    hash->filling_table[bucket]--;
                    hash->nel--;
                    break;
                }
            }
        }
    }
    else {
        DEBUG_DBG("try to remove something that is not here");
    }
}

extern apr_status_t napr_hash_set(napr_hash_t *hash, void *data, apr_uint32_t hash_value)
{
    apr_size_t nel = 0;
    apr_size_t bucket = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    bucket = hash_value & hash->mask;

    nel = hash->filling_table[bucket];
    if ((0 == nel) && (NULL == hash->table[bucket])) {
        hash->table[bucket] = (void **) apr_pcalloc(hash->own_pool, hash->ffactor * sizeof(void *));
    }
    // DEBUG_DBG( "set data %.*s in bucket %u at nel %u", hash->datum_get_key_len(data), hash->datum_get_key(data), bucket, nel);
    hash->table[bucket][nel] = data;
    hash->filling_table[bucket]++;
    hash->nel++;

    if (hash->ffactor <= hash->filling_table[bucket]) {
        // int i;
        // DEBUG_DBG( "rebuilding hash table, because there's %u element in bucket %u ffactor is %u (total element = %u)", 
        //      hash->filling_table[bucket], bucket, hash->ffactor, hash->nel);
        // for(i = 0; i < hash->ffactor; i++) {
        //     DEBUG_DBG( "%.*s", hash->datum_get_key_len(hash->table[bucket][i]), hash->datum_get_key(hash->table[bucket][i]));
        // }

        status = napr_hash_rebuild(hash);
        if (APR_SUCCESS != status) {
            char errbuf[ERR_BUF_SIZE];
            DEBUG_ERR("error calling napr_hash_rebuild: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
            return status;
        }
    }

    return APR_SUCCESS;
}

apr_pool_t *napr_hash_pool_get(const napr_hash_t *thehash)
{
    return thehash->pool;
}

napr_hash_index_t *napr_hash_first(apr_pool_t *pool, napr_hash_t *hash)
{
    napr_hash_index_t *hash_index = NULL;
    hash_index = apr_palloc(pool, sizeof(struct napr_hash_index_t));
    hash_index->hash = hash;
    hash_index->bucket = 0;
    hash_index->element = 0;

    if (hash->filling_table[0] > 0) {
        return hash_index;
    }

    return napr_hash_next(hash_index);
}

napr_hash_index_t *napr_hash_next(napr_hash_index_t *index)
{
    if ((0 != index->hash->filling_table[index->bucket])
        && (index->element < (index->hash->filling_table[index->bucket] - 1))) {
        index->element++;
        return index;
    }
    index->element = 0;
    for (index->bucket += 1; index->bucket < index->hash->size; index->bucket++) {
        if (0 != index->hash->filling_table[index->bucket]) {
            break;
        }
    }
    if (index->bucket < index->hash->size) {
        return index;
    }

    return NULL;
}

void napr_hash_this(napr_hash_index_t *hash_index, const void **key, apr_size_t *klen, void **val)
{
    if (key) {
        *key = hash_index->hash->get_key(hash_index->hash->table[hash_index->bucket][hash_index->element]);
    }
    if (klen) {
        *klen = hash_index->hash->get_key_len(hash_index->hash->table[hash_index->bucket][hash_index->element]);
    }
    if (val) {
        *val = hash_index->hash->table[hash_index->bucket][hash_index->element];
    }
}
