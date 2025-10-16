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
#include "ft_constants.h"
#include "ft_file.h"
#include "human_size.h"
#include "napr_hash.h"
#include "napr_heap.h"

int ft_chksum_cmp(const void *chksum1, const void *chksum2)
{
    const ft_chksum_t *chk1 = chksum1;
    const ft_chksum_t *chk2 = chksum2;
    int result = 0;

    result = memcmp(&chk1->hash_value, &chk2->hash_value, sizeof(ft_hash_t));

    if (0 == result) {
	return chk1->file->prioritized - chk2->file->prioritized;
    }

    return result;
}

/* Forward declaration for helper function */
static apr_status_t compare_and_report_pair(ft_conf_t *conf, ft_fsize_t *fsize, apr_size_t index1, apr_size_t index2,
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
process_and_report_duplicates_for_file(ft_conf_t *conf, ft_fsize_t *fsize, apr_size_t index,
				       const reporting_colors_t *colors)
{
    unsigned char already_printed = 0;
    apr_size_t j_index = 0;

    if (NULL == fsize->chksum_array[index].file) {
	return APR_SUCCESS;	/* Already processed as a duplicate */
    }

    for (j_index = index + 1; j_index < fsize->nb_files; j_index++) {
	/* If hashes match, perform a full comparison */
	if (0 == memcmp(&fsize->chksum_array[index].hash_value, &fsize->chksum_array[j_index].hash_value, sizeof(ft_hash_t))) {
	    if (compare_and_report_pair(conf, fsize, index, j_index, &already_printed, colors) != APR_SUCCESS) {
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
    apr_size_t index = 0;
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

	    for (index = 0; index < fsize->nb_files; index++) {
		if (process_and_report_duplicates_for_file(conf, fsize, index, &colors) != APR_SUCCESS) {
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

/**
 * @brief Cleans up temporary files created during archive extraction.
 */
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

static apr_status_t compare_and_report_pair(ft_conf_t *conf, ft_fsize_t *fsize, apr_size_t index1, apr_size_t index2,
					    unsigned char *already_printed, const reporting_colors_t *colors)
{
    char *fpathi = NULL;
    char *fpathj = NULL;
    int return_value = 0;
    apr_status_t status = APR_SUCCESS;

    ft_file_t *file1 = fsize->chksum_array[index1].file;
    ft_file_t *file2 = fsize->chksum_array[index2].file;

    if (get_comparison_paths(conf, file1, file2, &fpathi, &fpathj) != APR_SUCCESS) {
	DEBUG_ERR("Failed to get comparison paths for %s and %s", file1->path, file2->path);
	return APR_EGENERAL;
    }

    status = filecmp(conf->pool, fpathi, fpathj, fsize->val, conf->excess_size, &return_value);
    cleanup_comparison_paths(conf, file1, file2, fpathi, fpathj);

    if (status != APR_SUCCESS) {
	if (is_option_set(conf->mask, OPTION_VERBO)) {
	    char errbuf[ERROR_BUFFER_SIZE];
	    (void) fprintf(stderr, "\nskipping %s and %s comparison because: %s\n", file1->path, file2->path,
			   apr_strerror(status, errbuf, sizeof(errbuf)));
	}
	return APR_SUCCESS;	/* Continue processing other pairs */
    }

    if (return_value == 0) {
	if (is_option_set(conf->mask, OPTION_DRY_RUN)) {
	    fprintf(stderr, "Dry run: would report %s and %s as duplicates.\n", file1->path, file2->path);
	}

	if (!*already_printed) {
	    if (is_option_set(conf->mask, OPTION_SIZED)) {
		const char *human_size = format_human_size(fsize->val, conf->pool);
		printf("%sSize: %s%s\n", colors->size, human_size, colors->reset);
	    }
	    format_and_print_duplicate(conf, file1, colors);
	    *already_printed = 1;
	}

	printf("%c", conf->sep);
	format_and_print_duplicate(conf, file2, colors);

	fsize->chksum_array[index2].file = NULL;	/* Mark as a twin */
	if (fflush(stdout) != 0) {
	    perror("Error flushing stdout");
	    /* Optionally, return an error status */
	    return APR_EGENERAL;
	}
    }

    return APR_SUCCESS;
}
