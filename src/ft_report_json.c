/**
 * @file ft_report_json.c
 * @brief JSON-based duplicate reporting functions.
 * @ingroup Reporting
 */
/*
 * Copyright (C) 2025 Francois Pesce : francois.pesce (at) gmail (dot) com
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

#include "ft_report_json.h"

#if HAVE_JANSSON

#include <stdio.h>
#include <string.h>

#include <apr_strings.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include <jansson.h>

#include "config.h"
#include "debug.h"
#include "ft_archive.h"
#include "ft_config.h"
#include "ft_constants.h"
#include "ft_file.h"
#include "ft_report.h"
#include "napr_hash.h"
#include "napr_heap.h"

#define EPOCH_YEAR_START 1900

/* Formats apr_time_t to ISO 8601 UTC string (YYYY-MM-DDTHH:MM:SSZ). */
static const char *ft_format_time_iso8601_utc(apr_time_t time_value, apr_pool_t *pool)
{
    apr_time_exp_t exploded;
    // Use apr_time_exp_gmt to get the time in UTC (GMT).
    if (apr_time_exp_gmt(&exploded, time_value) != APR_SUCCESS) {
        return apr_pstrdup(pool, "UNKNOWN_TIME");
    }
    return apr_psprintf(pool, "%04d-%02d-%02dT%02d:%02d:%02dZ", exploded.tm_year + EPOCH_YEAR_START, exploded.tm_mon + 1, exploded.tm_mday, exploded.tm_hour, exploded.tm_min, exploded.tm_sec);
}

/* Converts XXH128 hash to a hex string. Assumes XXH128_hash_t has high64/low64 members. */
static char *ft_hash_to_hex(ft_hash_t hash, apr_pool_t *pool)
{
    /* Use APR's format macro for 64-bit hex (expands to PRIx64) with zero-padding */
    return apr_psprintf(pool, "%016" APR_UINT64_T_HEX_FMT "%016" APR_UINT64_T_HEX_FMT, hash.high64, hash.low64);
}

/* Helper to create a JSON object for a file entry */
static json_t *create_file_json_object(ft_file_t *file, ft_conf_t *conf)
{
    json_t *obj = json_object();
    const char *mtime_str = ft_format_time_iso8601_utc(file->mtime, conf->pool);

    json_object_set_new(obj, "path", json_string(file->path));
    if (is_option_set(conf->mask, OPTION_UNTAR)) {
        json_object_set_new(obj, "archive_subpath", file->subpath ? json_string(file->subpath) : json_null());
    }
    json_object_set_new(obj, "mtime_utc", json_string(mtime_str));
    json_object_set_new(obj, "prioritized", json_boolean(file->prioritized));
    return obj;
}

static apr_status_t get_comparison_paths(ft_conf_t *conf, ft_file_t *file1, ft_file_t *file2, char **path1, char **path2)
{
    if (is_option_set(conf->mask, OPTION_UNTAR)) {
        if (file1->subpath) {
            *path1 = ft_archive_untar_file(file1, conf->pool);
            if (!*path1) {
                return APR_EGENERAL;
            }
        }
        else {
            *path1 = file1->path;
        }
        if (file2->subpath) {
            *path2 = ft_archive_untar_file(file2, conf->pool);
            if (!*path2) {
                if (file1->subpath) {
                    (void) apr_file_remove(*path1, conf->pool);
                }
                return APR_EGENERAL;
            }
        }
        else {
            *path2 = file2->path;
        }
    }
    else {
        *path1 = file1->path;
        *path2 = file2->path;
    }
    return APR_SUCCESS;
}

static void cleanup_comparison_paths(ft_conf_t *conf, ft_file_t *file1, ft_file_t *file2, char *path1, char *path2)
{
    if (is_option_set(conf->mask, OPTION_UNTAR)) {
        if (file1->subpath) {
            (void) apr_file_remove(path1, conf->pool);
        }
        if (file2->subpath) {
            (void) apr_file_remove(path2, conf->pool);
        }
    }
}

static apr_status_t perform_file_comparison(ft_conf_t *conf, ft_chksum_t *chksum1, ft_chksum_t *chksum2, apr_off_t file_size, int *result)
{
    char *fpath1 = NULL;
    char *fpath2 = NULL;
    apr_status_t status = APR_SUCCESS;

    ft_file_t *file1 = chksum1->file;
    ft_file_t *file2 = chksum2->file;

    if (get_comparison_paths(conf, file1, file2, &fpath1, &fpath2) != APR_SUCCESS) {
        DEBUG_ERR("Failed to get comparison paths for %s and %s", file1->path, file2->path);
        return APR_EGENERAL;
    }

    status = filecmp(conf->pool, fpath1, fpath2, file_size, conf->excess_size, result);
    cleanup_comparison_paths(conf, file1, file2, fpath1, fpath2);

    if (status != APR_SUCCESS) {
        if (is_option_set(conf->mask, OPTION_VERBO)) {
            char errbuf[ERR_BUF_SIZE];
            (void) fprintf(stderr, "\nskipping %s and %s comparison because: %s\n", file1->path, file2->path, apr_strerror(status, errbuf, sizeof(errbuf)));
        }
        *result = 1;            /* Treat comparison error as "not a duplicate" */
    }

    return APR_SUCCESS;
}

static json_t *create_duplicate_set_json(ft_conf_t *conf, ft_chksum_t *chksum, apr_off_t size)
{
    json_t *set_obj = json_object();
    json_t *duplicates_array = json_array();
    char *hex_hash = ft_hash_to_hex(chksum->hash_value, conf->pool);

    json_object_set_new(set_obj, "size_bytes", json_integer(size));
    json_object_set_new(set_obj, "hash_xxh128", json_string(hex_hash));
    json_object_set_new(set_obj, "duplicates", duplicates_array);
    json_array_append_new(duplicates_array, create_file_json_object(chksum->file, conf));

    return set_obj;
}

static apr_status_t find_and_report_duplicates(ft_conf_t *conf, ft_fsize_t *fsize, json_t *root_array)
{
    apr_status_t status = APR_SUCCESS;
    int result = 0;

    for (size_t i = 0; i < fsize->nb_files; i++) {
        if (fsize->chksum_array[i].file == NULL) {
            continue;
        }

        json_t *current_set_obj = NULL;
        json_t *duplicates_array = NULL;

        for (size_t j = i + 1; j < fsize->nb_files; j++) {
            if (memcmp(&fsize->chksum_array[i].hash_value, &fsize->chksum_array[j].hash_value, sizeof(ft_hash_t)) == 0) {
                status = perform_file_comparison(conf, &fsize->chksum_array[i], &fsize->chksum_array[j], fsize->val, &result);
                if (status != APR_SUCCESS) {
                    return status;
                }

                if (result == 0) {
                    if (current_set_obj == NULL) {
                        current_set_obj = create_duplicate_set_json(conf, &fsize->chksum_array[i], fsize->val);
                        duplicates_array = json_object_get(current_set_obj, "duplicates");
                    }
                    json_array_append_new(duplicates_array, create_file_json_object(fsize->chksum_array[j].file, conf));
                    fsize->chksum_array[j].file = NULL;
                }
            }
            else {
                break;
            }
        }

        if (current_set_obj != NULL) {
            json_array_append_new(root_array, current_set_obj);
        }
    }
    return APR_SUCCESS;
}

static apr_status_t process_file_group(ft_conf_t *conf, ft_file_t *file, json_t *root_array, apr_off_t *old_size)
{
    if (file->size == *old_size) {
        return APR_SUCCESS;
    }
    *old_size = file->size;

    apr_uint32_t hash_value = 0;
    ft_fsize_t *fsize = napr_hash_search(conf->sizes, &file->size, sizeof(apr_off_t), &hash_value);

    if (fsize != NULL) {
        apr_uint32_t chksum_array_sz = FTWIN_MIN(fsize->nb_files, fsize->nb_checksumed);
        qsort(fsize->chksum_array, chksum_array_sz, sizeof(ft_chksum_t), ft_chksum_cmp);
        return find_and_report_duplicates(conf, fsize, root_array);
    }
    else {
        DEBUG_ERR("inconsistency error found, no size[%" APR_OFF_T_FMT "] in hash for file %s", file->size, file->path);
        return APR_EGENERAL;
    }
}

apr_status_t ft_report_json(ft_conf_t *conf)
{
    apr_off_t old_size = -1;
    ft_file_t *file = NULL;
    apr_status_t status = APR_SUCCESS;
    json_t *root_array = json_array();

    if (!root_array) {
        return APR_ENOMEM;
    }

    while ((file = napr_heap_extract(conf->heap)) != NULL) {
        status = process_file_group(conf, file, root_array, &old_size);
        if (status != APR_SUCCESS) {
            json_decref(root_array);
            return status;
        }
    }

    if (json_dumpf(root_array, stdout, JSON_INDENT(2) | JSON_ENSURE_ASCII) != 0) {
        (void) fprintf(stderr, "Error: failed to write JSON to stdout.\n");
        status = APR_EGENERAL;
    }
    printf("\n");
    if (fflush(stdout) != 0) {
        perror("Error flushing stdout");
        status = APR_EGENERAL;
    }
    json_decref(root_array);

    return status;
}

#endif /* HAVE_JANSSON */
