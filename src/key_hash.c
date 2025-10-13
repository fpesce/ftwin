#include "key_hash.h"
#include <stdint.h>

/* --- Buggy 32-bit functions (kept for testing the bug) --- */
apr_uint32_t apr_uint32_key_hash(const void *key, apr_size_t klen)
{
    apr_uint32_t i = *(apr_uint32_t *) key;
    i = (i + 0x7ed55d16) + (i << 12);
    i = (i ^ 0xc761c23c) ^ (i >> 19);
    i = (i + 0x165667b1) + (i << 5);
    i = (i + 0xd3a2646c) ^ (i << 9);
    i = (i + 0xfd7046c5) + (i << 3);
    i = (i ^ 0xb55a4f09) ^ (i >> 16);
    return i;
}

int apr_uint32_key_cmp(const void *key1, const void *key2, apr_size_t len)
{
    apr_uint32_t i1 = *(apr_uint32_t *) key1;
    apr_uint32_t i2 = *(apr_uint32_t *) key2;
    return (i1 == i2) ? 0 : 1;
}


/* --- Correct apr_off_t (64-bit) Callbacks --- */

apr_size_t ft_fsize_get_key_len(const void *data)
{
    return sizeof(apr_off_t);
}

apr_uint32_t apr_off_t_key_hash(const void *key, apr_size_t klen)
{
    apr_off_t i = *(apr_off_t *) key;
    i = (~i) + (i << 18); /* i = (i << 18) - i - 1; */
    i = i ^ (i >> 31);
    i = i * 21; /* i = (i + (i << 2)) + (i << 4); */
    i = i ^ (i >> 11);
    i = i + (i << 6);
    i = i ^ (i >> 22);
    return (apr_uint32_t)i;
}

int apr_off_t_key_cmp(const void *key1, const void *key2, apr_size_t len)
{
    apr_off_t i1 = *(apr_off_t *) key1;
    apr_off_t i2 = *(apr_off_t *) key2;
    if (i1 == i2) return 0;
    if (i1 < i2) return -1;
    return 1;
}

/* --- Correct gid_t Callbacks --- */

apr_size_t ft_gid_get_key_len(const void *data)
{
    return sizeof(gid_t);
}

apr_uint32_t gid_t_key_hash(const void *key, apr_size_t klen)
{
    gid_t i = *(gid_t *) key;
    i = (i + 0x7ed55d16) + (i << 12);
    i = (i ^ 0xc761c23c) ^ (i >> 19);
    i = (i + 0x165667b1) + (i << 5);
    i = (i + 0xd3a2646c) ^ (i << 9);
    i = (i + 0xfd7046c5) + (i << 3);
    i = (i ^ 0xb55a4f09) ^ (i >> 16);
    return (apr_uint32_t)i;
}

int gid_t_key_cmp(const void *key1, const void *key2, apr_size_t len)
{
    gid_t i1 = *(gid_t *) key1;
    gid_t i2 = *(gid_t *) key2;
    if (i1 == i2) return 0;
    if (i1 < i2) return -1;
    return 1;
}