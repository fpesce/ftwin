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

const char *format_human_size(apr_off_t size, apr_pool_t *pool)
{
    const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
    int i = 0;
    double readable_size = (double) size;

    while (readable_size >= 1024.0 && i < (sizeof(units) / sizeof(units[0]) - 1)) {
	readable_size /= 1024.0;
	i++;
    }

    if (i == 0) {
	return apr_psprintf(pool, "%d %s", (int) readable_size, units[i]);
    }
    else {
	return apr_psprintf(pool, "%.1f %s", readable_size, units[i]);
    }
}

apr_off_t parse_human_size(const char *size_str)
{
    char *endptr;
    double size = strtod(size_str, &endptr);
    apr_off_t multiplier = 1;

    if (endptr == size_str) {
	return -1;		// Not a valid number
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
	    return -1;		// Invalid suffix
	}
    }

    return (apr_off_t) (size * multiplier);
}
