/**
 * @file checksum.h
 * @brief Defines the core checksum type used throughout the application.
 * @ingroup CoreLogic
 */
#ifndef CHECKSUM_H
#define CHECKSUM_H
#include <apr.h>

/* Include the vendored header. Assumes -Isrc is set in CFLAGS/CPPFLAGS */
#include "xxhash.h"

/**
 * @brief The application-wide hash type, defined as a 128-bit XXH3 hash.
 *
 * This type is used to store the result of file content hashing operations.
 */
typedef XXH128_hash_t ft_hash_t;

#endif
