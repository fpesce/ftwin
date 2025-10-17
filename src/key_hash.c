#include "key_hash.h"
#include <stdint.h>

#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"

/* --- Correct apr_off_t (64-bit) Callbacks --- */

apr_size_t ft_fsize_get_key_len(const void *data)
{
    return sizeof(apr_off_t);
}

apr_uint32_t apr_off_t_key_hash(const void *key, apr_size_t klen)
{
    /* Use XXH32 to hash the 64-bit file size key */
    return XXH32(key, sizeof(apr_off_t), 0);
}

int apr_off_t_key_cmp(const void *key1, const void *key2, apr_size_t len)
{
    apr_off_t key1_value = *(const apr_off_t *) key1;
    apr_off_t key2_value = *(const apr_off_t *) key2;
    if (key1_value == key2_value) {
        return 0;
    }
    if (key1_value < key2_value) {
        return -1;
    }
    return 1;
}

/* --- Correct gid_t Callbacks --- */

apr_size_t ft_gid_get_key_len(const void *data)
{
    return sizeof(gid_t);
}

apr_uint32_t gid_t_key_hash(const void *key, apr_size_t klen)
{
    /* Use XXH32 to hash the gid_t key */
    return XXH32(key, sizeof(gid_t), 0);
}

int gid_t_key_cmp(const void *key1, const void *key2, apr_size_t len)
{
    gid_t key1_value = *(const gid_t *) key1;
    gid_t key2_value = *(const gid_t *) key2;
    if (key1_value == key2_value) {
        return 0;
    }
    if (key1_value < key2_value) {
        return -1;
    }
    return 1;
}
