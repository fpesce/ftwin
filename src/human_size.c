/**
 * @file human_size.c
 * @brief Implementation of human-readable size parsing and formatting.
 * @ingroup Utilities
 */
#include "human_size.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <apr_strings.h>

static const double BYTES_IN_KIBIBYTE = 1024.0;

const char *format_human_size(apr_off_t size, apr_pool_t *pool)
{
    const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
    int unit_index = 0;
    double readable_size = (double) size;

    while (readable_size >= BYTES_IN_KIBIBYTE && unit_index < (sizeof(units) / sizeof(units[0]) - 1)) {
        readable_size /= BYTES_IN_KIBIBYTE;
        unit_index++;
    }

    if (unit_index == 0) {
        return apr_psprintf(pool, "%d %s", (int) readable_size, units[unit_index]);
    }
    else {
        return apr_psprintf(pool, "%.1f %s", readable_size, units[unit_index]);
    }
}

apr_off_t parse_human_size(const char *size_str)
{
    char *endptr;
    double size = strtod(size_str, &endptr);
    apr_off_t multiplier = 1;

    if (endptr == size_str) {
        return -1;              // Not a valid number
    }

    while (*endptr && isspace((unsigned char) *endptr)) {
        endptr++;
    }

    if (*endptr) {
        switch (toupper((unsigned char) *endptr)) {
        case 'T':
            multiplier = 1024LL * 1024LL * 1024LL * 1024LL;
            break;
        case 'G':
            multiplier = 1024LL * 1024LL * 1024LL;
            break;
        case 'M':
            multiplier = 1024LL * 1024LL;
            break;
        case 'K':
            multiplier = 1024LL;
            break;
        default:
            return -1;          // Invalid suffix
        }
    }

    return (apr_off_t) (size * multiplier);
}
