#ifndef KEY_HASH_H
#define KEY_HASH_H

#include <apr.h>
#include <apr_general.h>
#include <sys/types.h>

/* Correct 64-bit apr_off_t callbacks */
apr_size_t ft_fsize_get_key_len(const void *data);
apr_uint32_t apr_off_t_key_hash(const void *key, apr_size_t klen);
int apr_off_t_key_cmp(const void *key1, const void *key2, apr_size_t len);

/* Correct gid_t callbacks */
apr_size_t ft_gid_get_key_len(const void *data);
apr_uint32_t gid_t_key_hash(const void *key, apr_size_t klen);
int gid_t_key_cmp(const void *key1, const void *key2, apr_size_t len);

#endif /* KEY_HASH_H */
