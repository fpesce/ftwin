/**
 * @file ftwin.c
 * @brief Main application logic for ftwin, including argument parsing, file traversal, and duplicate reporting.
 * @ingroup CoreLogic
 */
/*
 *
 * Copyright (C) 2007 François Pesce : francois.pesce (at) gmail (dot) com
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

#include <unistd.h>		/* getegid */
#include <stdio.h>		/* fgetgrent */
#include <sys/stat.h>		/* umask */
#include <grp.h>		/* fgetgrent */

#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_user.h>

#include "config.h"

#if HAVE_ARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif

#include "debug.h"
#include "ft_file.h"
#include "ft_system.h"
#include "human_size.h"
#include "napr_threadpool.h"
#include "ft_types.h"
#include "ft_config.h"
#include "ft_traverse.h"
#include "ft_process.h"
#include "key_hash.h"

int ft_file_cmp(const void *param1, const void *param2);

const void *ft_fsize_get_key(const void *opaque)
{
    const ft_fsize_t *fsize = opaque;

    return &(fsize->val);
}

const void *ft_gids_get_key(const void *opaque)
{
    const ft_gid_t *gid = opaque;

    return &(gid->val);
}

#if HAVE_ARCHIVE
static int copy_data(struct archive *ar, struct archive *aw)
{
    const void *buff;
    off_t offset;
    size_t size;
    int rv;

    for (;;) {
	rv = archive_read_data_block(ar, &buff, &size, &offset);
	if (rv == ARCHIVE_EOF) {
	    return ARCHIVE_OK;
	}
	if (rv != ARCHIVE_OK) {
	    DEBUG_ERR("error calling archive_read_data_block(): %s", archive_error_string(ar));
	    return rv;
	}
	rv = archive_write_data_block(aw, buff, size, offset);
	if (rv != ARCHIVE_OK) {
	    DEBUG_ERR("error calling archive_write_data_block(): %s", archive_error_string(aw));
	    return rv;
	}
    }
}

static char *ft_untar_file(ft_file_t *file, apr_pool_t *p)
{
    struct archive *a = NULL;
    struct archive *ext = NULL;
    struct archive_entry *entry = NULL;
    char *tmpfile = NULL;
    int rv;

    a = archive_read_new();
    if (NULL == a) {
	DEBUG_ERR("error calling archive_read_new()");
	return NULL;
    }
    rv = archive_read_support_filter_all(a);
    if (0 != rv) {
	DEBUG_ERR("error calling archive_read_support_filter_all(): %s", archive_error_string(a));
	return NULL;
    }
    rv = archive_read_support_format_all(a);
    if (0 != rv) {
	DEBUG_ERR("error calling archive_read_support_format_all(): %s", archive_error_string(a));
	return NULL;
    }
    rv = archive_read_open_filename(a, file->path, 10240);
    if (0 != rv) {
	DEBUG_ERR("error calling archive_read_open_filename(%s): %s", file->path, archive_error_string(a));
	return NULL;
    }

    ext = archive_write_disk_new();
    if (NULL == ext) {
	DEBUG_ERR("error calling archive_write_disk_new()");
	return NULL;
    }

    for (;;) {
	rv = archive_read_next_header(a, &entry);
	if (rv == ARCHIVE_EOF) {
	    DEBUG_ERR("subpath [%s] not found in archive [%s]", file->subpath, file->path);
	    return NULL;
	}
	if (rv != ARCHIVE_OK) {
	    DEBUG_ERR("error in archive (%s): %s", file->path, archive_error_string(a));
	    return NULL;
	}

	if (!strcmp(file->subpath, archive_entry_pathname(entry))) {
	    mode_t current_mode = umask(S_IRWXG | S_IRWXO);

	    /*
	     * All I want is only a temporary filename, but gcc outputs me an
	     * ugly warning if I use tempnam...
	     * tmpfile = tempnam("/tmp/", "ftwin");
	     */
	    tmpfile = apr_pstrdup(p, "/tmp/ftwinXXXXXX");
	    rv = mkstemp(tmpfile);
	    if (rv < 0) {
		DEBUG_ERR("error creating tmpfile %s", tmpfile);
		return NULL;
	    }
	    umask(current_mode);
	    close(rv);

	    archive_entry_copy_pathname(entry, tmpfile);

	    rv = archive_write_header(ext, entry);
	    if (rv == ARCHIVE_OK) {
		rv = copy_data(a, ext);
		if (rv != ARCHIVE_OK) {
		    DEBUG_ERR("error while copying data from archive (%s)", file->path);
		    apr_file_remove(tmpfile, p);
		    return NULL;
		}
	    }
	    else {
		DEBUG_ERR("error in archive (%s): %s", file->path, archive_error_string(a));
		apr_file_remove(tmpfile, p);
		return NULL;
	    }

	    break;
	}
    }

    archive_write_free(ext);
    archive_read_free(a);

    return tmpfile;
}
#endif

static int chksum_cmp(const void *chksum1, const void *chksum2)
{
    const ft_chksum_t *chk1 = chksum1;
    const ft_chksum_t *chk2 = chksum2;
    int i;

    i = memcmp(&chk1->hash_value, &chk2->hash_value, sizeof(ft_hash_t));

    if (0 == i) {
	return chk1->file->prioritized - chk2->file->prioritized;
    }

    return i;
}

#if HAVE_PUZZLE

#define NB_WORKER 4

struct compute_vector_ctx_t
{
    apr_thread_mutex_t *mutex;
    PuzzleContext *contextp;
    ft_conf_t *conf;
    int heap_size, nb_processed;
};
typedef struct compute_vector_ctx_t compute_vector_ctx_t;

static apr_status_t compute_vector(void *ctx, void *data)
{
    char errbuf[128];
    compute_vector_ctx_t *cv_ctx = ctx;
    ft_file_t *file = data;
    apr_status_t status;

    puzzle_init_cvec(cv_ctx->contextp, &(file->cvec));
    if (0 == puzzle_fill_cvec_from_file(cv_ctx->contextp, &(file->cvec), file->path)) {
	file->cvec_ok |= 0x1;
    }
    else {
	DEBUG_ERR("error calling puzzle_fill_cvec_from_file, ignoring file: %s", file->path);
    }

    status = apr_thread_mutex_lock(cv_ctx->mutex);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_lock: %s", apr_strerror(status, errbuf, 128));
	return status;
    }
    if (is_option_set(cv_ctx->conf->mask, OPTION_VERBO)) {
	fprintf(stderr, "\rProgress [%i/%i] %d%% ", cv_ctx->nb_processed, cv_ctx->heap_size,
		(int) ((float) cv_ctx->nb_processed / (float) cv_ctx->heap_size * 100.0));
    }
    cv_ctx->nb_processed += 1;
    status = apr_thread_mutex_unlock(cv_ctx->mutex);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_unlock: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    return APR_SUCCESS;
}

static apr_status_t ft_conf_image_twin_report(ft_conf_t *conf)
{
    char errbuf[128];
    PuzzleContext context;
    compute_vector_ctx_t cv_ctx;
    double d;
    ft_file_t *file, *file_cmp;
    napr_threadpool_t *threadpool;
    unsigned long nb_cmp, cnt_cmp;
    int i, heap_size;
    apr_status_t status;
    unsigned char already_printed;

    puzzle_init_context(&context);
    puzzle_set_max_width(&context, 5000);
    puzzle_set_max_height(&context, 5000);
    puzzle_set_lambdas(&context, 13);

    cv_ctx.contextp = &context;
    cv_ctx.heap_size = heap_size = napr_heap_size(conf->heap);
    cv_ctx.nb_processed = 0;
    cv_ctx.conf = conf;
    status = apr_thread_mutex_create(&(cv_ctx.mutex), APR_THREAD_MUTEX_DEFAULT, conf->pool);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_create: %s", apr_strerror(status, errbuf, 128));
	return status;
    }
    status = napr_threadpool_init(&threadpool, &cv_ctx, NB_WORKER, compute_vector, conf->pool);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling napr_threadpool_init: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    for (i = 0; i < heap_size; i++) {

	file = napr_heap_get_nth(conf->heap, i);
	status = napr_threadpool_add(threadpool, file);
	if (APR_SUCCESS != status) {
	    DEBUG_ERR("error calling napr_threadpool_add: %s", apr_strerror(status, errbuf, 128));
	    return status;
	}
    }
    napr_threadpool_wait(threadpool);
    status = apr_thread_mutex_destroy(cv_ctx.mutex);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_destroy: %s", apr_strerror(status, errbuf, 128));
	return status;
    }
    if (is_option_set(conf->mask, OPTION_VERBO)) {
	fprintf(stderr, "\rProgress [%i/%i] %d%% ", i, heap_size, (int) ((float) i / (float) heap_size * 100.0));
	fprintf(stderr, "\n");
    }

    nb_cmp = napr_heap_size(conf->heap) * (napr_heap_size(conf->heap) - 1) / 2;
    cnt_cmp = 0;
    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (!(file->cvec_ok & 0x1))
	    continue;
	already_printed = 0;
	heap_size = napr_heap_size(conf->heap);
	for (i = 0; i < heap_size; i++) {
	    file_cmp = napr_heap_get_nth(conf->heap, i);
	    if (!(file_cmp->cvec_ok & 0x1))
		continue;

	    d = puzzle_vector_normalized_distance(&context, &(file->cvec), &(file_cmp->cvec), 0);
	    if (d < conf->threshold) {
		if (!already_printed) {
		    printf("%s%c", file->path, conf->sep);
		    already_printed = 1;
		}
		else {
		    printf("%c", conf->sep);
		}
		printf("%s", file_cmp->path);
	    }
	    if (is_option_set(conf->mask, OPTION_VERBO)) {
		fprintf(stderr, "\rCompare progress [%10lu/%10lu] %02.2f%% ", cnt_cmp, nb_cmp,
			(double) ((double) cnt_cmp / (double) nb_cmp * 100.0));
	    }
	    cnt_cmp++;
	}

	if (already_printed)
	    printf("\n\n");

	puzzle_free_cvec(&context, &(file->cvec));
    }
    if (is_option_set(conf->mask, OPTION_VERBO)) {
	fprintf(stderr, "\rCompare progress [%10lu/%10lu] %02.2f%% ", cnt_cmp, nb_cmp,
		(double) ((double) cnt_cmp / (double) nb_cmp * 100.0));
	fprintf(stderr, "\n");
    }

    puzzle_free_context(&context);

    return APR_SUCCESS;
}
#endif

#if HAVE_JANSSON
/* Formats apr_time_t to ISO 8601 UTC string (YYYY-MM-DDTHH:MM:SSZ). */
static const char *ft_format_time_iso8601_utc(apr_time_t t, apr_pool_t *pool)
{
    apr_time_exp_t exploded;
    // Use apr_time_exp_gmt to get the time in UTC (GMT).
    if (apr_time_exp_gmt(&exploded, t) != APR_SUCCESS) {
	return apr_pstrdup(pool, "UNKNOWN_TIME");
    }
    return apr_psprintf(pool, "%04d-%02d-%02dT%02d:%02d:%02dZ",
			exploded.tm_year + 1900, exploded.tm_mon + 1, exploded.tm_mday,
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
#if HAVE_ARCHIVE
    if (is_option_set(conf->mask, OPTION_UNTAR)) {
	json_object_set_new(obj, "archive_subpath", file->subpath ? json_string(file->subpath) : json_null());
    }
#endif
    json_object_set_new(obj, "mtime_utc", json_string(mtime_str));
    json_object_set_new(obj, "prioritized", json_boolean(file->prioritized));
    return obj;
}

static apr_status_t ft_conf_json_report(ft_conf_t *conf)
{
    // Variable declarations (mirroring ft_conf_twin_report)
    char errbuf[128];
    apr_off_t old_size = -1;
    ft_file_t *file;
    ft_fsize_t *fsize;
    apr_uint32_t hash_value;
    apr_size_t i, j;
    int rv;
    apr_status_t status;
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
	    qsort(fsize->chksum_array, chksum_array_sz, sizeof(ft_chksum_t), chksum_cmp);

	    for (i = 0; i < fsize->nb_files; i++) {
		if (NULL == fsize->chksum_array[i].file)
		    continue;

		json_t *current_set_obj = NULL;
		json_t *duplicates_array = NULL;

		for (j = i + 1; j < fsize->nb_files; j++) {
		    if (0 ==
			memcmp(&fsize->chksum_array[i].hash_value, &fsize->chksum_array[j].hash_value, sizeof(ft_hash_t))) {

			// --- Comparison Logic (Replicate exactly from ft_conf_twin_report) ---
			char *fpathi, *fpathj;
#if HAVE_ARCHIVE
			if (is_option_set(conf->mask, OPTION_UNTAR)) {
			    if (NULL != fsize->chksum_array[i].file->subpath) {
				fpathi = ft_untar_file(fsize->chksum_array[i].file, conf->pool);
				if (NULL == fpathi) {
				    DEBUG_ERR("error calling ft_untar_file");
				    return APR_EGENERAL;
				}
			    }
			    else {
				fpathi = fsize->chksum_array[i].file->path;
			    }
			    if (NULL != fsize->chksum_array[j].file->subpath) {
				fpathj = ft_untar_file(fsize->chksum_array[j].file, conf->pool);
				if (NULL == fpathj) {
				    DEBUG_ERR("error calling ft_untar_file");
				    return APR_EGENERAL;
				}
			    }
			    else {
				fpathj = fsize->chksum_array[j].file->path;
			    }
			}
			else {
			    fpathi = fsize->chksum_array[i].file->path;
			    fpathj = fsize->chksum_array[j].file->path;
			}
#else
			fpathi = fsize->chksum_array[i].file->path;
			fpathj = fsize->chksum_array[j].file->path;
#endif
			status = filecmp(conf->pool, fpathi, fpathj, fsize->val, conf->excess_size, &rv);

#if HAVE_ARCHIVE
			if (is_option_set(conf->mask, OPTION_UNTAR)) {
			    if (NULL != fsize->chksum_array[i].file->subpath)
				apr_file_remove(fpathi, conf->pool);
			    if (NULL != fsize->chksum_array[j].file->subpath)
				apr_file_remove(fpathj, conf->pool);
			}
#endif
			if (APR_SUCCESS != status) {
			    if (is_option_set(conf->mask, OPTION_VERBO))
				fprintf(stderr, "\nskipping %s and %s comparison because: %s\n",
					fsize->chksum_array[i].file->path, fsize->chksum_array[j].file->path,
					apr_strerror(status, errbuf, 128));
			    rv = 1;
			}
			// -------------------------------------------------------------

			if (0 == rv) {
			    if (is_option_set(conf->mask, OPTION_DRY_RUN)) {
				fprintf(stderr, "Dry run: would perform action on %s and %s\n",
					fsize->chksum_array[i].file->path, fsize->chksum_array[j].file->path);
			    }

			    // Initialize JSON set if first match for file[i]
			    if (NULL == current_set_obj) {
				current_set_obj = json_object();
				duplicates_array = json_array();

				// Add metadata
				json_object_set_new(current_set_obj, "size_bytes", json_integer(fsize->val));
				char *hex_hash = ft_hash_to_hex(fsize->chksum_array[i].hash_value, conf->pool);
				json_object_set_new(current_set_obj, "hash_xxh128", json_string(hex_hash));
				json_object_set_new(current_set_obj, "duplicates", duplicates_array);

				// Add file[i] details
				json_array_append_new(duplicates_array,
						      create_file_json_object(fsize->chksum_array[i].file, conf));
			    }

			    // Add file[j] details
			    json_array_append_new(duplicates_array,
						  create_file_json_object(fsize->chksum_array[j].file, conf));

			    fsize->chksum_array[j].file = NULL;	// Mark as processed
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
#endif

static apr_status_t ft_conf_twin_report(ft_conf_t *conf)
{
    char errbuf[128];
    apr_off_t old_size = -1;
    ft_file_t *file;
    ft_fsize_t *fsize;
    apr_uint32_t hash_value;
    apr_size_t i, j;
    int rv;
    apr_status_t status;
    unsigned char already_printed;
    apr_uint32_t chksum_array_sz = 0U;
    int use_color = isatty(STDOUT_FILENO);
    const char *color_size = use_color ? ANSI_COLOR_CYAN ANSI_COLOR_BOLD : "";
    const char *color_path = use_color ? ANSI_COLOR_BLUE ANSI_COLOR_BOLD : "";
    const char *color_reset = use_color ? ANSI_COLOR_RESET : "";

    if (is_option_set(conf->mask, OPTION_VERBO))
	fprintf(stderr, "Reporting duplicate files:\n");

    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (file->size == old_size)
	    continue;

	old_size = file->size;
	if (NULL != (fsize = napr_hash_search(conf->sizes, &file->size, sizeof(apr_off_t), &hash_value))) {
	    chksum_array_sz = FTWIN_MIN(fsize->nb_files, fsize->nb_checksumed);
	    qsort(fsize->chksum_array, chksum_array_sz, sizeof(ft_chksum_t), chksum_cmp);
	    for (i = 0; i < fsize->nb_files; i++) {
		if (NULL == fsize->chksum_array[i].file)
		    continue;
		already_printed = 0;
		for (j = i + 1; j < fsize->nb_files; j++) {
		    if (0 ==
			memcmp(&fsize->chksum_array[i].hash_value, &fsize->chksum_array[j].hash_value, sizeof(ft_hash_t))) {
			char *fpathi, *fpathj;
#if HAVE_ARCHIVE
			if (is_option_set(conf->mask, OPTION_UNTAR)) {
			    if (NULL != fsize->chksum_array[i].file->subpath) {
				fpathi = ft_untar_file(fsize->chksum_array[i].file, conf->pool);
				if (NULL == fpathi) {
				    DEBUG_ERR("error calling ft_untar_file");
				    return APR_EGENERAL;
				}
			    }
			    else {
				fpathi = fsize->chksum_array[i].file->path;
			    }
			    if (NULL != fsize->chksum_array[j].file->subpath) {
				fpathj = ft_untar_file(fsize->chksum_array[j].file, conf->pool);
				if (NULL == fpathj) {
				    DEBUG_ERR("error calling ft_untar_file");
				    return APR_EGENERAL;
				}
			    }
			    else {
				fpathj = fsize->chksum_array[j].file->path;
			    }
			}
			else {
			    fpathi = fsize->chksum_array[i].file->path;
			    fpathj = fsize->chksum_array[j].file->path;
			}
#else
			fpathi = fsize->chksum_array[i].file->path;
			fpathj = fsize->chksum_array[j].file->path;
#endif
			status = filecmp(conf->pool, fpathi, fpathj, fsize->val, conf->excess_size, &rv);
#if HAVE_ARCHIVE
			if (is_option_set(conf->mask, OPTION_UNTAR)) {
			    if (NULL != fsize->chksum_array[i].file->subpath)
				apr_file_remove(fpathi, conf->pool);
			    if (NULL != fsize->chksum_array[j].file->subpath)
				apr_file_remove(fpathj, conf->pool);
			}
#endif
			/*
			 * no return status if != APR_SUCCESS , because : 
			 * Fault-check has been removed in case files disappear
			 * between collecting and comparing or special files (like
			 * device or /proc) are tried to access
			 */
			if (APR_SUCCESS != status) {
			    if (is_option_set(conf->mask, OPTION_VERBO))
				fprintf(stderr, "\nskipping %s and %s comparison because: %s\n",
					fsize->chksum_array[i].file->path, fsize->chksum_array[j].file->path,
					apr_strerror(status, errbuf, 128));
			    rv = 1;
			}

			if (0 == rv) {
			    if (is_option_set(conf->mask, OPTION_DRY_RUN)) {
				fprintf(stderr, "Dry run: would perform action on %s and %s\n",
					fsize->chksum_array[i].file->path, fsize->chksum_array[j].file->path);
			    }
			    if (!already_printed) {
				if (is_option_set(conf->mask, OPTION_SIZED)) {
				    const char *human_size = format_human_size(fsize->val, conf->pool);
				    printf("%sSize: %s%s\n", color_size, human_size, color_reset);
				}
#if HAVE_ARCHIVE
				if (is_option_set(conf->mask, OPTION_UNTAR)
				    && (NULL != fsize->chksum_array[i].file->subpath))
				    printf("%s%s%c%s%s%c", color_path, fsize->chksum_array[i].file->path,
					   (':' != conf->sep) ? ':' : '|', fsize->chksum_array[i].file->subpath,
					   color_reset, conf->sep);
				else
#endif
				    printf("%s%s%s%c", color_path, fsize->chksum_array[i].file->path, color_reset,
					   conf->sep);
				already_printed = 1;
			    }
			    else {
				printf("%c", conf->sep);
			    }
#if HAVE_ARCHIVE
			    if (is_option_set(conf->mask, OPTION_UNTAR) && (NULL != fsize->chksum_array[j].file->subpath))
				printf("%s%s%c%s%s", color_path, fsize->chksum_array[j].file->path,
				       (':' != conf->sep) ? ':' : '|', fsize->chksum_array[j].file->subpath, color_reset);
			    else
#endif
				printf("%s%s%s", color_path, fsize->chksum_array[j].file->path, color_reset);
			    /* mark j as a twin ! */
			    fsize->chksum_array[j].file = NULL;
			    fflush(stdout);
			}
		    }
		    else {
			/* hash are ordered, so at first mismatch we check the next */
			break;
		    }
		}
		if (already_printed) {
		    printf("\n\n");
		}
	    }
	}
	else {
	    DEBUG_ERR("inconsistency error found, no size[%" APR_OFF_T_FMT "] in hash for file %s", file->size, file->path);
	    return APR_EGENERAL;
	}
    }

    return APR_SUCCESS;
}

int ftwin_main(int argc, const char **argv)
{
    char errbuf[128];
    ft_conf_t *conf;
    apr_pool_t *pool, *gc_pool;
    int i;
    apr_status_t status;

    if (APR_SUCCESS != (status = apr_initialize())) {
	DEBUG_ERR("error calling apr_initialize: %s", apr_strerror(status, errbuf, 128));
	return -1;
    }

    if (APR_SUCCESS != (status = apr_pool_create(&pool, NULL))) {
	DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }

    conf = ft_config_create(pool);
    if (APR_SUCCESS != (status = ft_config_parse_args(conf, argc, argv))) {
        /* Error message is printed inside the function */
        apr_terminate();
        return -1;
    }

    /* Step 1 : Browse the file */
    if (APR_SUCCESS != (status = apr_pool_create(&gc_pool, pool))) {
	DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, 128));
	apr_terminate();
	return -1;
    }
    for (i = 1; i < argc; i++) {
	const char *current_arg = argv[i];
	char *resolved_path = (char *) current_arg;

	// Requirement: JSON output must contain absolute paths.
	if (is_option_set(conf->mask, OPTION_JSON)) {
	    // Use apr_filepath_merge with NULL rootpath to resolve the absolute path.
	    status = apr_filepath_merge(&resolved_path, NULL, current_arg, APR_FILEPATH_TRUENAME, gc_pool);
	    if (APR_SUCCESS != status) {
		DEBUG_ERR("Error resolving absolute path for argument %s: %s.", current_arg,
			  apr_strerror(status, errbuf, 128));
		apr_terminate();
		return -1;	// Fail if path resolution fails for JSON mode
	    }
	}

	if (APR_SUCCESS != (status = ft_traverse_path(conf, resolved_path))) {
	    DEBUG_ERR("error calling ft_traverse_path: %s", apr_strerror(status, errbuf, 128));
	    apr_terminate();
	    return -1;
	}
    }

    if (0 < napr_heap_size(conf->heap)) {
#if HAVE_PUZZLE
	if (is_option_set(conf->mask, OPTION_PUZZL)) {
#if HAVE_JANSSON
	    if (is_option_set(conf->mask, OPTION_JSON)) {
		fprintf(stderr, "Error: JSON output is currently not supported in image comparison mode (-I).\n");
		apr_terminate();
		return -1;
	    }
#endif
	    /* Step 2: Report the image twins */
	    if (APR_SUCCESS != (status = ft_conf_image_twin_report(conf))) {
		DEBUG_ERR("error calling ft_conf_image_twin_report: %s", apr_strerror(status, errbuf, 128));
		apr_terminate();
		return status;
	    }
	}
	else {
#endif
	    /* Step 2: Process the sizes set */
	    if (APR_SUCCESS != (status = ft_process_files(conf))) {
		DEBUG_ERR("error calling ft_process_files: %s", apr_strerror(status, errbuf, 128));
		apr_terminate();
		return -1;
	    }

#if HAVE_JANSSON
	    if (is_option_set(conf->mask, OPTION_JSON)) {
		if (APR_SUCCESS != (status = ft_conf_json_report(conf))) {
		    DEBUG_ERR("error calling ft_conf_json_report: %s", apr_strerror(status, errbuf, 128));
		    apr_terminate();
		    return status;
		}
	    }
	    else {
#endif
		if (APR_SUCCESS != (status = ft_conf_twin_report(conf))) {
		    DEBUG_ERR("error calling ft_conf_twin_report: %s", apr_strerror(status, errbuf, 128));
		    apr_terminate();
		    return status;
		}
#if HAVE_JANSSON
	    }
#endif
#if HAVE_PUZZLE
	}
#endif
    }
    else {
	fprintf(stderr, "Please submit at least two files...\n");
	return -1;
    }

    apr_terminate();

    return 0;
}

#ifndef FTWIN_TEST_BUILD
int main(int argc, const char **argv)
{
    return ftwin_main(argc, argv);
}
#endif
