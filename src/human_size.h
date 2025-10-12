#ifndef HUMAN_SIZE_H
#define HUMAN_SIZE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <apr_general.h>

apr_off_t parse_human_size(const char *size_str);

#endif /* HUMAN_SIZE_H */
