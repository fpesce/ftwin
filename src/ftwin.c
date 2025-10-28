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

#include <unistd.h>             /* getegid */
#include <stdio.h>              /* fgetgrent */
#include <sys/stat.h>           /* umask */
#include <grp.h>                /* fgetgrent */

#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_user.h>
#include <apr_env.h>

#include "config.h"

#include "debug.h"
#include "ft_constants.h"
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
#include "napr_cache.h"

int ft_file_cmp(const void *param1, const void *param2);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int should_manage_apr = 1;

void ft_config_set_should_terminate_apr(int should_terminate)
{
    should_manage_apr = should_terminate;
}

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

static apr_status_t run_ftwin_processing(ft_conf_t *conf, int argc, const char **argv, int first_arg_index)
{
    char errbuf[ERR_BUF_SIZE];
    apr_pool_t *gc_pool = NULL;
    apr_status_t status = apr_pool_create(&gc_pool, conf->pool);

    if (APR_SUCCESS != status) {
        DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    for (int arg_index = first_arg_index; arg_index < argc; arg_index++) {
        const char *current_arg = argv[arg_index];
        char *resolved_path = (char *) current_arg;

        if (is_option_set(conf->mask, OPTION_JSON)) {
            status = apr_filepath_merge(&resolved_path, NULL, current_arg, APR_FILEPATH_TRUENAME, gc_pool);
            if (APR_SUCCESS != status) {
                DEBUG_ERR("Error resolving absolute path for argument %s: %s.", current_arg, apr_strerror(status, errbuf, ERR_BUF_SIZE));
                return status;
            }
        }

        status = ft_traverse_path(conf, resolved_path);
        if (APR_SUCCESS != status) {
            DEBUG_ERR("error calling ft_traverse_path: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
            return status;
        }
    }

    if (conf->cache) {
        status = napr_cache_sweep(conf->cache);
        if (status != APR_SUCCESS) {
            DEBUG_ERR("Error during cache sweep: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
            status = APR_SUCCESS;
        }
    }

    if (napr_heap_size(conf->heap) > 0) {
        if (is_option_set(conf->mask, OPTION_PUZZL)) {
            status = ft_image_twin_report(conf);
        }
        else {
            status = ft_process_files(conf);
            if (status == APR_SUCCESS) {
#if HAVE_JANSSON
                if (is_option_set(conf->mask, OPTION_JSON)) {
                    status = ft_report_json(conf);
                }
                else {
#endif
                    status = ft_report_duplicates(conf);
#if HAVE_JANSSON
                }
#endif
            }
        }
    }
    else {
        (void) fprintf(stderr, "Please submit at least two files...\n");
        return APR_EINVAL;
    }

    apr_pool_destroy(gc_pool);
    return status;
}

static apr_status_t initialize_resources(apr_pool_t **pool)
{
    char errbuf[ERR_BUF_SIZE];
    apr_status_t status = APR_SUCCESS;

    if (should_manage_apr) {
        status = apr_initialize();
        if (status != APR_SUCCESS) {
            DEBUG_ERR("error calling apr_initialize: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
            return status;
        }
    }

    status = apr_pool_create(pool, NULL);
    if (status != APR_SUCCESS) {
        DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        if (should_manage_apr) {
            apr_terminate();
        }
    }
    return status;
}

static apr_status_t setup_cache(ft_conf_t *conf, apr_pool_t *pool)
{
    char errbuf[ERR_BUF_SIZE];
    char *home_dir = NULL;
    const char *cache_dir_path = NULL;
    const char *cache_db_path = NULL;
    apr_status_t status = APR_SUCCESS;

    status = apr_env_get(&home_dir, "HOME", pool);
    if (status != APR_SUCCESS) {
        DEBUG_ERR("error getting HOME environment variable: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    cache_dir_path = apr_pstrcat(pool, home_dir, "/.cache/ftwin", NULL);
    if (cache_dir_path == NULL) {
        DEBUG_ERR("error constructing cache directory path");
        return APR_EGENERAL;
    }

    status = apr_dir_make_recursive(cache_dir_path, APR_OS_DEFAULT, pool);
    if (status != APR_SUCCESS && !APR_STATUS_IS_EEXIST(status)) {
        DEBUG_ERR("error creating cache directory %s: %s", cache_dir_path, apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    cache_db_path = apr_pstrcat(pool, cache_dir_path, "/file_cache.db", NULL);
    if (cache_db_path == NULL) {
        DEBUG_ERR("error constructing cache database path");
        return APR_EGENERAL;
    }

    status = napr_cache_open(&conf->cache, cache_db_path, pool);
    if (status != APR_SUCCESS) {
        DEBUG_ERR("error opening cache at %s: %s", cache_db_path, apr_strerror(status, errbuf, ERR_BUF_SIZE));
    }
    return status;
}

static void cleanup_resources(ft_conf_t *conf)
{
    if (conf && conf->cache) {
        apr_status_t close_status = napr_cache_close(conf->cache);
        if (close_status != APR_SUCCESS) {
            char errbuf[ERR_BUF_SIZE];

            DEBUG_ERR("error closing cache: %s", apr_strerror(close_status, errbuf, ERR_BUF_SIZE));
        }
    }

    if (should_manage_apr) {
        apr_terminate();
    }
}

static apr_status_t process_args_and_run(ft_conf_t *conf, int argc, const char **argv)
{
    int first_arg_index = 0;
    apr_status_t status = ft_config_parse_args(conf, argc, argv, &first_arg_index);

    if (status != APR_SUCCESS) {
        return status;
    }

    status = run_ftwin_processing(conf, argc, argv, first_arg_index);
    if (status != APR_SUCCESS) {
        char errbuf[ERR_BUF_SIZE];
        DEBUG_ERR("error during ftwin processing: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
    }
    return status;
}

int ftwin_main(int argc, const char **argv)
{
    ft_conf_t *conf = NULL;
    apr_pool_t *pool = NULL;
    apr_status_t status = initialize_resources(&pool);

    if (status != APR_SUCCESS) {
        return -1;
    }

    conf = ft_config_create(pool);

    status = setup_cache(conf, pool);
    if (status != APR_SUCCESS) {
        cleanup_resources(NULL);
        return -1;
    }

    status = process_args_and_run(conf, argc, argv);
    if (status != APR_SUCCESS) {
        cleanup_resources(conf);
        return -1;
    }

    cleanup_resources(conf);
    return 0;
}

#ifndef FTWIN_TEST_BUILD
int main(int argc, const char **argv)
{
    return ftwin_main(argc, argv);
}
#endif
