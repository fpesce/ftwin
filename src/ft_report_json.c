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
    return apr_psprintf(pool, "%04d-%02d-%02dT%02d:%02d:%02dZ",
			exploded.tm_year + EPOCH_YEAR_START, exploded.tm_mon + 1, exploded.tm_mday,
			exploded.tm_hour, exploded.tm_min, exploded.tm_sec);
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

apr_status_t ft_report_json(ft_conf_t *conf)
{
    // Variable declarations (mirroring ft_conf_twin_report)
    char errbuf[128];
    apr_off_t old_size = -1;
    ft_file_t *file = NULL;
    ft_fsize_t *fsize = NULL;
    apr_uint32_t hash_value = 0;
    apr_size_t index1 = 0;
    apr_size_t index2 = 0;
    int rv = 0;
    apr_status_t status = APR_SUCCESS;
    apr_uint32_t chksum_array_sz = 0U;

    json_t *root_array = json_array();
    if (!root_array)
	return APR_ENOMEM;

    // Iterate through the heap (logic adapted from ft_conf_twin_report)
    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (file->size == old_size)
	    continue;
	old_size = file->size;

	if (NULL != (fsize = napr_hash_search(conf->sizes, &file->size, sizeof(apr_off_t), &hash_value))) {
	    chksum_array_sz = FTWIN_MIN(fsize->nb_files, fsize->nb_checksumed);
	    qsort(fsize->chksum_array, chksum_array_sz, sizeof(ft_chksum_t), ft_chksum_cmp);

	    for (index1 = 0; index1 < fsize->nb_files; index1++) {
		if (NULL == fsize->chksum_array[index1].file)
		    continue;

		json_t *current_set_obj = NULL;
		json_t *duplicates_array = NULL;

		for (index2 = index1 + 1; index2 < fsize->nb_files; index2++) {
		    if (0 ==
			memcmp(&fsize->chksum_array[index1].hash_value, &fsize->chksum_array[index2].hash_value,
			       sizeof(ft_hash_t))) {

			// --- Comparison Logic (Replicate exactly from ft_conf_twin_report) ---
			char *fpathi = NULL;
			char *fpathj = NULL;
			if (is_option_set(conf->mask, OPTION_UNTAR)) {
			    if (NULL != fsize->chksum_array[index1].file->subpath) {
				fpathi = ft_archive_untar_file(fsize->chksum_array[index1].file, conf->pool);
				if (NULL == fpathi) {
				    DEBUG_ERR("error calling ft_archive_untar_file");
				    return APR_EGENERAL;
				}
			    }
			    else {
				fpathi = fsize->chksum_array[index1].file->path;
			    }
			    if (NULL != fsize->chksum_array[index2].file->subpath) {
				fpathj = ft_archive_untar_file(fsize->chksum_array[index2].file, conf->pool);
				if (NULL == fpathj) {
				    DEBUG_ERR("error calling ft_archive_untar_file");
				    return APR_EGENERAL;
				}
			    }
			    else {
				fpathj = fsize->chksum_array[index2].file->path;
			    }
			}
			else {
			    fpathi = fsize->chksum_array[index1].file->path;
			    fpathj = fsize->chksum_array[index2].file->path;
			}
			status = filecmp(conf->pool, fpathi, fpathj, fsize->val, conf->excess_size, &rv);

			if (is_option_set(conf->mask, OPTION_UNTAR)) {
			    if (NULL != fsize->chksum_array[index1].file->subpath)
				(void) apr_file_remove(fpathi, conf->pool);
			    if (NULL != fsize->chksum_array[index2].file->subpath)
				(void) apr_file_remove(fpathj, conf->pool);
			}
			if (APR_SUCCESS != status) {
			    if (is_option_set(conf->mask, OPTION_VERBO))
				fprintf(stderr, "\nskipping %s and %s comparison because: %s\n",
					fsize->chksum_array[index1].file->path, fsize->chksum_array[index2].file->path,
					apr_strerror(status, errbuf, 128));
			    rv = 1;
			}
			// -------------------------------------------------------------

			if (0 == rv) {
			    if (is_option_set(conf->mask, OPTION_DRY_RUN)) {
				fprintf(stderr, "Dry run: would perform action on %s and %s\n",
					fsize->chksum_array[index1].file->path, fsize->chksum_array[index2].file->path);
			    }

			    // Initialize JSON set if first match for file[index1]
			    if (NULL == current_set_obj) {
				current_set_obj = json_object();
				duplicates_array = json_array();

				// Add metadata
				json_object_set_new(current_set_obj, "size_bytes", json_integer(fsize->val));
				char *hex_hash = ft_hash_to_hex(fsize->chksum_array[index1].hash_value, conf->pool);
				json_object_set_new(current_set_obj, "hash_xxh128", json_string(hex_hash));
				json_object_set_new(current_set_obj, "duplicates", duplicates_array);

				// Add file[index1] details
				json_array_append_new(duplicates_array,
						      create_file_json_object(fsize->chksum_array[index1].file, conf));
			    }

			    // Add file[index2] details
			    json_array_append_new(duplicates_array,
						  create_file_json_object(fsize->chksum_array[index2].file, conf));

			    fsize->chksum_array[index2].file = NULL;	// Mark as processed
			}
		    }
		    else {
			break;	// Hashes differ
		    }
		}
		// If a set was created, append it to the root array
		if (NULL != current_set_obj) {
		    json_array_append_new(root_array, current_set_obj);
		}
	    }
	}
	else {
	    DEBUG_ERR("inconsistency error found, no size[%" APR_OFF_T_FMT "] in hash for file %s", file->size, file->path);
	    return APR_EGENERAL;
	}
    }

    // Dump the JSON output to stdout
    json_dumpf(root_array, stdout, JSON_INDENT(2) | JSON_ENSURE_ASCII);
    printf("\n");
    fflush(stdout);
    // Free the JSON structure
    json_decref(root_array);

    return APR_SUCCESS;
}

#endif /* HAVE_JANSSON */
