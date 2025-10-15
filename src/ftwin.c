/**
 * @file ftwin.c
 * @brief Main application logic for ftwin, including argument parsing, file traversal, and duplicate reporting.
 * @ingroup CoreLogic
 */
/*
 *
 * Copyright (C) 2007-2025 Francois Pesce : francois.pesce (at) gmail (dot) com
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

#include "debug.h"
#include "ft_file.h"
#include "ft_system.h"
#include "human_size.h"
#include "napr_threadpool.h"
#include "ft_types.h"
#include "ft_config.h"
#include "ft_traverse.h"
#include "ft_process.h"
#include "ft_archive.h"
#include "ft_report.h"
#include "ft_report_json.h"
#include "ft_image.h"
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

int ftwin_main(int argc, const char **argv)
{
    char errbuf[128];
    ft_conf_t *conf;
    apr_pool_t *pool, *gc_pool;
    int i, first_arg_index;
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
    if (APR_SUCCESS != (status = ft_config_parse_args(conf, argc, argv, &first_arg_index))) {
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
    for (i = first_arg_index; i < argc; i++) {
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
	if (is_option_set(conf->mask, OPTION_PUZZL)) {
#if HAVE_JANSSON
	    if (is_option_set(conf->mask, OPTION_JSON)) {
		fprintf(stderr, "Error: JSON output is currently not supported in image comparison mode (-I).\n");
		apr_terminate();
		return -1;
	    }
#endif
	    /* Step 2: Report the image twins */
	    if (APR_SUCCESS != (status = ft_image_twin_report(conf))) {
		DEBUG_ERR("error calling ft_image_twin_report: %s", apr_strerror(status, errbuf, 128));
		apr_terminate();
		return status;
	    }
	}
	else {
	    /* Step 2: Process the sizes set */
	    if (APR_SUCCESS != (status = ft_process_files(conf))) {
		DEBUG_ERR("error calling ft_process_files: %s", apr_strerror(status, errbuf, 128));
		apr_terminate();
		return -1;
	    }

#if HAVE_JANSSON
	    if (is_option_set(conf->mask, OPTION_JSON)) {
		if (APR_SUCCESS != (status = ft_report_json(conf))) {
		    DEBUG_ERR("error calling ft_report_json: %s", apr_strerror(status, errbuf, 128));
		    apr_terminate();
		    return status;
		}
	    }
	    else {
#endif
		if (APR_SUCCESS != (status = ft_report_duplicates(conf))) {
		    DEBUG_ERR("error calling ft_report_duplicates: %s", apr_strerror(status, errbuf, 128));
		    apr_terminate();
		    return status;
		}
#if HAVE_JANSSON
	    }
#endif
	}
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
