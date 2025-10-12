/*
 *
 * Copyright (C) 2024 François Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FT_FILE_TYPES_H
#define FT_FILE_TYPES_H

#include <apr_general.h>
#include "checksum.h"

#if HAVE_PUZZLE
#include <puzzle.h>
#endif

typedef struct ft_file_t
{
    apr_off_t size;
    char *path;
    ft_hash_t hash;
#if HAVE_ARCHIVE
    char *subpath;
#endif
#if HAVE_PUZZLE
    PuzzleCvec cvec;
    int cvec_ok:1;
#endif
    int prioritized:1;
} ft_file_t;

typedef struct ft_chksum_t
{
    ft_hash_t hash_value;
    ft_file_t *file;
} ft_chksum_t;

#include "napr_list.h"
#include <pcre.h>
#include <apr_user.h>

typedef struct ft_conf_t
{
    apr_off_t minsize;
    apr_off_t maxsize;
    apr_off_t excess_size;	/* Switch off mmap behavior */
#if HAVE_PUZZLE
    double threshold;
#endif
    apr_pool_t *pool;		/* Always needed somewhere ;) */
    struct napr_heap_t *heap;		/* Will holds the files */
    struct napr_hash_t *sizes;		/* will holds the sizes hashed with http://www.burtleburtle.net/bob/hash/integer.html */
    struct napr_hash_t *checksums_ht;
    struct napr_hash_t *gids;		/* will holds the gids hashed with http://www.burtleburtle.net/bob/hash/integer.html */
    struct napr_hash_t *ig_files;
    pcre *ig_regex;
    pcre *wl_regex;
    pcre *ar_regex;		/* archive regex */
    char *p_path;		/* priority path */
    char *username;
    apr_size_t p_path_len;
    apr_uid_t userid;
    apr_gid_t groupid;
    unsigned short int mask;
    char sep;
    unsigned int num_threads;
} ft_conf_t;

typedef struct ft_fsize_t
{
    apr_off_t val;
    napr_list_t *file_list;
    apr_uint32_t nb_files;
    ft_hash_t key;
} ft_fsize_t;

#endif /* FT_FILE_TYPES_H */