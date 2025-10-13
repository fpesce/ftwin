/*
 * Copyright (C) 2025 François Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FT_IGNORE_H
#define FT_IGNORE_H

#include <apr_pools.h>
#include <apr_tables.h>
#include <pcre.h>

/* Flags for ignore patterns */
#define FT_IGNORE_NEGATE    0x01	/* Starts with '!' */
#define FT_IGNORE_DIR_ONLY  0x02	/* Ends with '/' */

typedef struct
{
    pcre *regex;
    const char *pattern_str;	/* Original pattern for debugging */
    unsigned int flags;
} ft_ignore_pattern_t;

/* Hierarchical context structure */
typedef struct ft_ignore_context_t
{
    struct ft_ignore_context_t *parent;	/* Parent directory's context */
    apr_array_header_t *patterns;	/* Array of ft_ignore_pattern_t* defined at this level */
    const char *base_dir;	/* Absolute path this context is anchored to */
    apr_size_t base_dir_len;
    apr_pool_t *pool;
} ft_ignore_context_t;

/* Match result codes */
typedef enum
{
    FT_IGNORE_MATCH_NONE,
    FT_IGNORE_MATCH_IGNORED,
    FT_IGNORE_MATCH_WHITELISTED
} ft_ignore_match_result_t;

/* API Prototypes */
ft_ignore_context_t *ft_ignore_context_create(apr_pool_t *pool, ft_ignore_context_t * parent, const char *base_dir);
apr_status_t ft_ignore_load_file(ft_ignore_context_t * ctx, const char *filepath);
apr_status_t ft_ignore_add_pattern_str(ft_ignore_context_t * ctx, const char *pattern_str);
ft_ignore_match_result_t ft_ignore_match(ft_ignore_context_t * ctx, const char *fullpath, int is_dir);

#endif /* FT_IGNORE_H */
