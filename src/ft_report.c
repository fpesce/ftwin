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
					    unsigned char *already_printed, const char *color_size, const char *color_path,
					    const char *color_reset);

apr_status_t ft_report_duplicates(ft_conf_t *conf)
{
    char errbuf[ERROR_BUFFER_SIZE];
    apr_off_t old_size = -1;
    ft_file_t *file = NULL;
    ft_fsize_t *fsize = NULL;
    apr_uint32_t hash_value = 0;
    apr_size_t i = 0;
    apr_size_t j = 0;
    unsigned char already_printed = 0;
    apr_uint32_t chksum_array_sz = 0U;
    int use_color = isatty(STDOUT_FILENO);
    const char *color_size = use_color ? ANSI_COLOR_CYAN ANSI_COLOR_BOLD : "";
    const char *color_path = use_color ? ANSI_COLOR_BLUE ANSI_COLOR_BOLD : "";
    const char *color_reset = use_color ? ANSI_COLOR_RESET : "";

    memset(errbuf, 0, sizeof(errbuf));
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
		if (NULL == fsize->chksum_array[i].file) {
		    continue;
		}
		already_printed = 0;
		for (j = i + 1; j < fsize->nb_files; j++) {
		    if (0 ==
			memcmp(&fsize->chksum_array[i].hash_value, &fsize->chksum_array[j].hash_value, sizeof(ft_hash_t))) {
			if (compare_and_report_pair(conf, fsize, i, j, &already_printed, color_size, color_path,
						    color_reset) != APR_SUCCESS) {
			    return APR_EGENERAL;
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

static apr_status_t compare_and_report_pair(ft_conf_t *conf, ft_fsize_t *fsize, apr_size_t i, apr_size_t j,
					    unsigned char *already_printed, const char *color_size, const char *color_path,
					    const char *color_reset)
{
    char errbuf[ERROR_BUFFER_SIZE];
    char *fpathi = NULL;
    char *fpathj = NULL;
    int rv = 0;
    apr_status_t status = APR_SUCCESS;

    memset(errbuf, 0, sizeof(errbuf));

    if (is_option_set(conf->mask, OPTION_UNTAR)) {
	if (NULL != fsize->chksum_array[i].file->subpath) {
	    fpathi = ft_archive_untar_file(fsize->chksum_array[i].file, conf->pool);
	    if (NULL == fpathi) {
		DEBUG_ERR("error calling ft_archive_untar_file");
		return APR_EGENERAL;
	    }
	}
	else {
	    fpathi = fsize->chksum_array[i].file->path;
	}
	if (NULL != fsize->chksum_array[j].file->subpath) {
	    fpathj = ft_archive_untar_file(fsize->chksum_array[j].file, conf->pool);
	    if (NULL == fpathj) {
		DEBUG_ERR("error calling ft_archive_untar_file");
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

    status = filecmp(conf->pool, fpathi, fpathj, fsize->val, conf->excess_size, &rv);

    if (is_option_set(conf->mask, OPTION_UNTAR)) {
	if (NULL != fsize->chksum_array[i].file->subpath) {
	    apr_file_remove(fpathi, conf->pool);
	}
	if (NULL != fsize->chksum_array[j].file->subpath) {
	    apr_file_remove(fpathj, conf->pool);
	}
    }

    if (APR_SUCCESS != status) {
	if (is_option_set(conf->mask, OPTION_VERBO)) {
	    (void) fprintf(stderr, "\nskipping %s and %s comparison because: %s\n", fsize->chksum_array[i].file->path,
			   fsize->chksum_array[j].file->path, apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	}
	rv = 1;
    }

    if (0 == rv) {
	if (is_option_set(conf->mask, OPTION_DRY_RUN)) {
	    (void) fprintf(stderr, "Dry run: would perform action on %s and %s\n", fsize->chksum_array[i].file->path,
			   fsize->chksum_array[j].file->path);
	}
	if (!*already_printed) {
	    if (is_option_set(conf->mask, OPTION_SIZED)) {
		const char *human_size = format_human_size(fsize->val, conf->pool);
		printf("%sSize: %s%s\n", color_size, human_size, color_reset);
	    }
	    if (is_option_set(conf->mask, OPTION_UNTAR) && (NULL != fsize->chksum_array[i].file->subpath)) {
		printf("%s%s%c%s%s%c", color_path, fsize->chksum_array[i].file->path,
		       (':' != conf->sep) ? ':' : '|', fsize->chksum_array[i].file->subpath, color_reset, conf->sep);
	    }
	    else {
		printf("%s%s%s%c", color_path, fsize->chksum_array[i].file->path, color_reset, conf->sep);
	    }
	    *already_printed = 1;
	}
	else {
	    printf("%c", conf->sep);
	}
	if (is_option_set(conf->mask, OPTION_UNTAR) && (NULL != fsize->chksum_array[j].file->subpath)) {
	    printf("%s%s%c%s%s", color_path, fsize->chksum_array[j].file->path, (':' != conf->sep) ? ':' : '|',
		   fsize->chksum_array[j].file->subpath, color_reset);
	}
	else {
	    printf("%s%s%s", color_path, fsize->chksum_array[j].file->path, color_reset);
	}
	/* mark j as a twin ! */
	fsize->chksum_array[j].file = NULL;
	(void) fflush(stdout);
    }

    return APR_SUCCESS;
}
