/* src/checksum.h */
#ifndef CHECKSUM_H
#define CHECKSUM_H
#include <apr.h>

/* Include the vendored header. Assumes -Isrc is set in CFLAGS/CPPFLAGS */
#include "xxhash.h"

/* Define the application-wide hash type (128-bit) */
typedef XXH128_hash_t ft_hash_t;

/* Remove legacy definitions: typedef ub1, #define HASHSTATE, hash(), hash2() */
#endif