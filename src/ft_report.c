/**
 * @file ft_report.c
 * @brief Text-based duplicate reporting functions.
 * @ingroup Reporting
 */
/*
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

#include "ft_report.h"
#include "ft_report.hh"

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

enum {
    ERROR_BUFFER_SIZE = 128
};

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

#if HAVE_ARCHIVE
static apr_status_t get_file_paths_from_archive(ft_conf_t *conf, ft_chksum_t *chksum1, ft_chksum_t *chksum2,
						char **fpath1, char **fpath2)
{
    if (NULL != chksum1->file->subpath) {
	*fpath1 = ft_archive_untar_file(chksum1->file, conf->pool);
	if (NULL == *fpath1) {
	    DEBUG_ERR("error calling ft_archive_untar_file");
	    return APR_EGENERAL;
	}
    }
    else {
	*fpath1 = chksum1->file->path;
    }

    if (NULL != chksum2->file->subpath) {
	*fpath2 = ft_archive_untar_file(chksum2->file, conf->pool);
	if (NULL == *fpath2) {
	    DEBUG_ERR("error calling ft_archive_untar_file");
	    if (NULL != chksum1->file->subpath) {
		(void) apr_file_remove(*fpath1, conf->pool);
	    }
	    return APR_EGENERAL;
	}
    }
    else {
	*fpath2 = chksum2->file->path;
    }

    return APR_SUCCESS;
}
#endif

static void get_file_paths(ft_conf_t *conf, ft_chksum_t *chksum1, ft_chksum_t *chksum2, char **fpath1, char **fpath2)
{
#if HAVE_ARCHIVE
    if (is_option_set(conf->mask, OPTION_UNTAR)) {
	if (APR_SUCCESS != get_file_paths_from_archive(conf, chksum1, chksum2, fpath1, fpath2)) {
	    *fpath1 = NULL;
	    *fpath2 = NULL;
	}
	return;
    }
#endif
    *fpath1 = chksum1->file->path;
    *fpath2 = chksum2->file->path;
}

static void print_duplicate_info(ft_conf_t *conf, const ft_fsize_t *fsize, const ft_chksum_t *chksum,
				 const report_colors_t *colors)
{
    if (is_option_set(conf->mask, OPTION_SIZED)) {
	const char *human_size = format_human_size(fsize->val, conf->pool);
	(void) printf("%sSize: %s%s\n", colors->size, human_size, colors->reset);
    }
#if HAVE_ARCHIVE
    if (is_option_set(conf->mask, OPTION_UNTAR) && (NULL != chksum->file->subpath)) {
	(void) printf("%s%s%c%s%s%c", colors->path, chksum->file->path, (':' != conf->sep) ? ':' : '|',
		      chksum->file->subpath, colors->reset, conf->sep);
    }
    else
#endif
    {
	(void) printf("%s%s%s%c", colors->path, chksum->file->path, colors->reset, conf->sep);
    }
}

static void compare_and_report_duplicates(ft_conf_t *conf, ft_fsize_t *fsize, apr_size_t idx1, apr_size_t idx2,
					  unsigned char *already_printed, const report_colors_t *colors)
{
    char errbuf[ERROR_BUFFER_SIZE];
    char *fpath1 = NULL;
    char *fpath2 = NULL;
    int compare_result = 0;
    apr_status_t status;

    if (memcmp(&fsize->chksum_array[idx1].hash_value, &fsize->chksum_array[idx2].hash_value, sizeof(ft_hash_t)) == 0) {
	get_file_paths(conf, &fsize->chksum_array[idx1], &fsize->chksum_array[idx2], &fpath1, &fpath2);

	if (!fpath1 || !fpath2) {
	    return;
	}

	status = filecmp(conf->pool, fpath1, fpath2, fsize->val, conf->excess_size, &compare_result);

#if HAVE_ARCHIVE
	if (is_option_set(conf->mask, OPTION_UNTAR)) {
	    if (NULL != fsize->chksum_array[idx1].file->subpath) {
		(void) apr_file_remove(fpath1, conf->pool);
	    }
	    if (NULL != fsize->chksum_array[idx2].file->subpath) {
		(void) apr_file_remove(fpath2, conf->pool);
	    }
	}
#endif

	if (APR_SUCCESS != status) {
	    if (is_option_set(conf->mask, OPTION_VERBO)) {
		(void) fprintf(stderr, "\nskipping %s and %s comparison because: %s\n",
			       fsize->chksum_array[idx1].file->path, fsize->chksum_array[idx2].file->path,
			       apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	    }
	    return;
	}

	if (0 == compare_result) {
	    if (is_option_set(conf->mask, OPTION_DRY_RUN)) {
		(void) fprintf(stderr, "Dry run: would perform action on %s and %s\n",
			       fsize->chksum_array[idx1].file->path, fsize->chksum_array[idx2].file->path);
	    }
	    if (!*already_printed) {
		print_duplicate_info(conf, fsize, &fsize->chksum_array[idx1], colors);
		*already_printed = 1;
	    }
	    else {
		(void) printf("%c", conf->sep);
	    }
#if HAVE_ARCHIVE
	    if (is_option_set(conf->mask, OPTION_UNTAR) && (NULL != fsize->chksum_array[idx2].file->subpath)) {
		(void) printf("%s%s%c%s%s", colors->path, fsize->chksum_array[idx2].file->path,
			      (':' != conf->sep) ? ':' : '|', fsize->chksum_array[idx2].file->subpath, colors->reset);
	    }
	    else
#endif
	    {
		(void) printf("%s%s%s", colors->path, fsize->chksum_array[idx2].file->path, colors->reset);
	    }
	    fsize->chksum_array[idx2].file = NULL;
	    (void) fflush(stdout);
	}
    }
}

static void process_file_group(ft_conf_t *conf, ft_fsize_t *fsize, const report_colors_t *colors)
{
    apr_uint32_t chksum_array_sz = FTWIN_MIN(fsize->nb_files, fsize->nb_checksumed);
    qsort(fsize->chksum_array, chksum_array_sz, sizeof(ft_chksum_t), ft_chksum_cmp);

    for (apr_size_t i = 0; i < fsize->nb_files; i++) {
	if (NULL == fsize->chksum_array[i].file) {
	    continue;
	}
	unsigned char already_printed = 0;
	for (apr_size_t j = i + 1; j < fsize->nb_files; j++) {
	    compare_and_report_duplicates(conf, fsize, i, j, &already_printed, colors);
	}
	if (already_printed) {
	    (void) printf("\n\n");
	}
    }
}

apr_status_t ft_report_duplicates(ft_conf_t *conf)
{
    apr_off_t old_size = -1;
    ft_file_t *file = NULL;
    ft_fsize_t *fsize = NULL;
    apr_uint32_t hash_value = 0;
    int use_color = isatty(STDOUT_FILENO);
    const report_colors_t colors = {
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
	    process_file_group(conf, fsize, &colors);
	}
	else {
	    DEBUG_ERR("inconsistency error found, no size[%" APR_OFF_T_FMT "] in hash for file %s", file->size,
		      file->path);
	    return APR_EGENERAL;
	}
    }

    return APR_SUCCESS;
}