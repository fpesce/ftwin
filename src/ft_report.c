/**
 * @file ft_report.c
 * @brief Text-based duplicate reporting functions.
 * @ingroup Reporting
 */
/*
 * Copyright (C) 2007 Francois Pesce : francois.pesce (at) gmail (dot) com
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

#include "ft_report.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <apr_strings.h>
#include <apr_file_io.h>

#include "config.h"
#include "debug.h"
#include "ft_archive.h"
#include "ft_config.h"
#include "ft_file.h"
#include "human_size.h"
#include "napr_hash.h"
#include "napr_heap.h"

int ft_chksum_cmp(const void *chksum1, const void *chksum2)
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

/* Forward declaration for helper function */
static apr_status_t compare_and_report_pair(ft_conf_t *conf, ft_fsize_t *fsize, apr_size_t i, apr_size_t j,
					    unsigned char *already_printed, const reporting_colors_t *colors);

/**
 * @brief Processes a single file to find and report its duplicates.
 *
 * This helper function is called by ft_report_duplicates. It iterates through
 * the checksum array for a given file size, compares subsequent files with
 * the same hash, and calls compare_and_report_pair to verify and report
 * true duplicates. This simplifies the main reporting loop by encapsulating
 * the logic for handling a single file's potential duplicates.
 *
 * @return APR_SUCCESS on success, or an error status if reporting fails.
 */
static apr_status_t
process_and_report_duplicates_for_file(ft_conf_t *conf, ft_fsize_t *fsize, apr_size_t i, const reporting_colors_t *colors)
{
    unsigned char already_printed = 0;
    apr_size_t j;

    if (NULL == fsize->chksum_array[i].file) {
	return APR_SUCCESS;	/* Already processed as a duplicate */
    }

    for (j = i + 1; j < fsize->nb_files; j++) {
	/* If hashes match, perform a full comparison */
	if (0 == memcmp(&fsize->chksum_array[i].hash_value, &fsize->chksum_array[j].hash_value, sizeof(ft_hash_t))) {
	    if (compare_and_report_pair(conf, fsize, i, j, &already_printed, colors) != APR_SUCCESS) {
		return APR_EGENERAL;
	    }
	}
	else {
	    /* Hashes are ordered, so we can break early */
	    break;
	}
    }

    if (already_printed) {
	printf("\n\n");
    }

    return APR_SUCCESS;
}

apr_status_t ft_report_duplicates(ft_conf_t *conf)
{
    apr_off_t old_size = -1;
    ft_file_t *file = NULL;
    ft_fsize_t *fsize = NULL;
    apr_uint32_t hash_value = 0;
    apr_size_t i = 0;
    apr_uint32_t chksum_array_sz = 0U;
    int use_color = isatty(STDOUT_FILENO);
    const reporting_colors_t colors = {
	use_color ? ANSI_COLOR_CYAN ANSI_COLOR_BOLD : "",
	use_color ? ANSI_COLOR_BLUE ANSI_COLOR_BOLD : "",
	use_color ? ANSI_COLOR_RESET : ""
    };

    if (is_option_set(conf->mask, OPTION_VERBO)) {
	(void) fprintf(stderr, "Reporting duplicate files:\n");
    }

    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (file->size == old_size) {
	    continue;
	}
	old_size = file->size;

	fsize = napr_hash_search(conf->sizes, &file->size, sizeof(apr_off_t), &hash_value);
	if (NULL != fsize) {
	    chksum_array_sz = FTWIN_MIN(fsize->nb_files, fsize->nb_checksumed);
	    qsort(fsize->chksum_array, chksum_array_sz, sizeof(ft_chksum_t), ft_chksum_cmp);

	    for (i = 0; i < fsize->nb_files; i++) {
		if (process_and_report_duplicates_for_file(conf, fsize, i, &colors) != APR_SUCCESS) {
		    return APR_EGENERAL;
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

/**
 * @brief Gets the file paths for comparison, handling archive extraction if needed.
 * @return APR_SUCCESS on success, or an error status if extraction fails.
 */
static apr_status_t get_comparison_paths(ft_conf_t *conf, ft_file_t *file_i, ft_file_t *file_j, char **fpathi, char **fpathj)
{
    if (is_option_set(conf->mask, OPTION_UNTAR)) {
	if (file_i->subpath) {
	    *fpathi = ft_archive_untar_file(file_i, conf->pool);
	    if (!*fpathi) {
		return APR_EGENERAL;
	    }
	}
	else {
	    *fpathi = file_i->path;
	}
	if (file_j->subpath) {
	    *fpathj = ft_archive_untar_file(file_j, conf->pool);
	    if (!*fpathj) {
		if (file_i->subpath) {
		    (void) apr_file_remove(*fpathi, conf->pool);
		}
		return APR_EGENERAL;
	    }
	}
	else {
	    *fpathj = file_j->path;
	}
    }
    else {
	*fpathi = file_i->path;
	*fpathj = file_j->path;
    }
    return APR_SUCCESS;
}

/**
 * @brief Cleans up temporary files created during archive extraction.
 */
static void cleanup_comparison_paths(ft_conf_t *conf, ft_file_t *file_i, ft_file_t *file_j, char *fpathi, char *fpathj)
{
    if (is_option_set(conf->mask, OPTION_UNTAR)) {
	if (file_i->subpath) {
	    (void) apr_file_remove(fpathi, conf->pool);
	}
	if (file_j->subpath) {
	    (void) apr_file_remove(fpathj, conf->pool);
	}
    }
}

/**
 * @brief Formats and prints the output for a duplicate file entry.
 */
static void format_and_print_duplicate(ft_conf_t *conf, const ft_file_t *file, const reporting_colors_t *colors)
{
    if (is_option_set(conf->mask, OPTION_UNTAR) && file->subpath) {
	printf("%s%s%c%s%s", colors->path, file->path, (':' != conf->sep) ? ':' : '|', file->subpath, colors->reset);
    }
    else {
	printf("%s%s%s", colors->path, file->path, colors->reset);
    }
}

static apr_status_t compare_and_report_pair(ft_conf_t *conf, ft_fsize_t *fsize, apr_size_t i, apr_size_t j,
					    unsigned char *already_printed, const reporting_colors_t *colors)
{
    char *fpathi = NULL;
    char *fpathj = NULL;
    int rv = 0;
    apr_status_t status;

    ft_file_t *file_i = fsize->chksum_array[i].file;
    ft_file_t *file_j = fsize->chksum_array[j].file;

    if (get_comparison_paths(conf, file_i, file_j, &fpathi, &fpathj) != APR_SUCCESS) {
	DEBUG_ERR("Failed to get comparison paths for %s and %s", file_i->path, file_j->path);
	return APR_EGENERAL;
    }

    status = filecmp(conf->pool, fpathi, fpathj, fsize->val, conf->excess_size, &rv);
    cleanup_comparison_paths(conf, file_i, file_j, fpathi, fpathj);

    if (status != APR_SUCCESS) {
	if (is_option_set(conf->mask, OPTION_VERBO)) {
	    char errbuf[ERROR_BUFFER_SIZE];
	    (void) fprintf(stderr, "\nskipping %s and %s comparison because: %s\n", file_i->path, file_j->path,
			   apr_strerror(status, errbuf, sizeof(errbuf)));
	}
	return APR_SUCCESS;	/* Continue processing other pairs */
    }

    if (rv == 0) {
	if (is_option_set(conf->mask, OPTION_DRY_RUN)) {
	    fprintf(stderr, "Dry run: would report %s and %s as duplicates.\n", file_i->path, file_j->path);
	}

	if (!*already_printed) {
	    if (is_option_set(conf->mask, OPTION_SIZED)) {
		const char *human_size = format_human_size(fsize->val, conf->pool);
		printf("%sSize: %s%s\n", colors->size, human_size, colors->reset);
	    }
	    format_and_print_duplicate(conf, file_i, colors);
	    *already_printed = 1;
	}

	printf("%c", conf->sep);
	format_and_print_duplicate(conf, file_j, colors);

	fsize->chksum_array[j].file = NULL;	/* Mark as a twin */
	fflush(stdout);
    }

    return APR_SUCCESS;
}
