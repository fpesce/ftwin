#include "human_size.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

apr_off_t parse_human_size(const char *size_str)
{
    char *endptr;
    double size = strtod(size_str, &endptr);
    apr_off_t multiplier = 1;

    if (endptr == size_str) {
        return -1; // Not a valid number
    }

    while (*endptr && isspace((unsigned char)*endptr)) {
        endptr++;
    }

    if (*endptr) {
        switch (toupper((unsigned char)*endptr)) {
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
            return -1; // Invalid suffix
        }
    }

    return (apr_off_t)(size * multiplier);
}
