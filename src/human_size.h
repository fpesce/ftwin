#ifndef HUMAN_SIZE_H
#define HUMAN_SIZE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <apr_general.h>

apr_off_t parse_human_size(const char *size_str);
const char *format_human_size(apr_off_t size, apr_pool_t *pool);

#endif /* HUMAN_SIZE_H */
