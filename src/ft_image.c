/**
 * @file ft_image.c
 * @brief Image comparison functions using libpuzzle.
 * @ingroup ImageComparison
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

#include "ft_image.h"

#if HAVE_PUZZLE

#include <stdio.h>
#include <puzzle.h>

#include <apr_thread_mutex.h>

#include "config.h"
#include "debug.h"
#include "ft_config.h"
#include "ft_file.h"
#include "napr_threadpool.h"
#include "napr_heap.h"

static const int NB_WORKER = 4;

struct compute_vector_ctx_t
{
    apr_thread_mutex_t *mutex;
    PuzzleContext *contextp;
    ft_conf_t *conf;
    unsigned int heap_size;
    unsigned int nb_processed;
};
typedef struct compute_vector_ctx_t compute_vector_ctx_t;

struct compute_vector_task_t
{
    compute_vector_ctx_t *cv_ctx;
    ft_file_t *file;
};
typedef struct compute_vector_task_t compute_vector_task_t;

static apr_status_t compute_vector(void *ctx, void *data)
{
    char errbuf[ERROR_BUFFER_SIZE];
    compute_vector_ctx_t *cv_ctx = (compute_vector_ctx_t *) ctx;
    compute_vector_task_t *task = (compute_vector_task_t *) data;
    ft_file_t *file = task->file;
    apr_status_t status = APR_SUCCESS;

    memset(errbuf, 0, sizeof(errbuf));
    puzzle_init_cvec(cv_ctx->contextp, &(file->cvec));
    if (0 == puzzle_fill_cvec_from_file(cv_ctx->contextp, &(file->cvec), file->path)) {
	file->cvec_ok |= 0x1;
    }
    else {
	DEBUG_ERR("error calling puzzle_fill_cvec_from_file, ignoring file: %s", file->path);
    }

    status = apr_thread_mutex_lock(cv_ctx->mutex);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_lock: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	return status;
    }
    if (is_option_set(cv_ctx->conf->mask, OPTION_VERBO)) {
	(void) fprintf(stderr, "\rProgress [%u/%u] %d%% ", cv_ctx->nb_processed, cv_ctx->heap_size,
		       (int) ((float) cv_ctx->nb_processed / (float) cv_ctx->heap_size * 100.0F));
    }
    cv_ctx->nb_processed += 1;
    status = apr_thread_mutex_unlock(cv_ctx->mutex);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_unlock: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	return status;
    }

    return APR_SUCCESS;
}

static const int MAX_PUZZLE_WIDTH = 5000;
static const int MAX_PUZZLE_HEIGHT = 5000;
static const int PUZZLE_LAMBDAS = 13;
static const int FIX_FOR_CLUSTERING = 0;
static const int MAX_PERCENTAGE = 100;

static void initialize_puzzle_context(PuzzleContext * context);
static apr_status_t compute_image_vectors(ft_conf_t *conf, PuzzleContext * context);
static void compare_image_vectors(ft_conf_t *conf, PuzzleContext * context);

apr_status_t ft_image_twin_report(ft_conf_t *conf)
{
    PuzzleContext context;
    apr_status_t status = APR_SUCCESS;

    initialize_puzzle_context(&context);

    status = compute_image_vectors(conf, &context);
    if (status != APR_SUCCESS) {
	puzzle_free_context(&context);
	return status;
    }

    compare_image_vectors(conf, &context);

    puzzle_free_context(&context);

    return APR_SUCCESS;
}

static void initialize_puzzle_context(PuzzleContext * context)
{
    puzzle_init_context(context);
    puzzle_set_max_width(context, MAX_PUZZLE_WIDTH);
    puzzle_set_max_height(context, MAX_PUZZLE_HEIGHT);
    puzzle_set_lambdas(context, PUZZLE_LAMBDAS);
}

static apr_status_t compute_image_vectors(ft_conf_t *conf, PuzzleContext * context)
{
    char errbuf[ERROR_BUFFER_SIZE];
    apr_status_t status = APR_SUCCESS;
    napr_threadpool_t *threadpool = NULL;
    compute_vector_ctx_t cv_ctx;
    unsigned int heap_size = napr_heap_size(conf->heap);

    memset(errbuf, 0, sizeof(errbuf));
    cv_ctx.contextp = context;
    cv_ctx.heap_size = heap_size;
    cv_ctx.nb_processed = 0;
    cv_ctx.conf = conf;

    status = apr_thread_mutex_create(&cv_ctx.mutex, APR_THREAD_MUTEX_DEFAULT, conf->pool);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_create: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	return status;
    }

    status = napr_threadpool_init(&threadpool, &cv_ctx, NB_WORKER, compute_vector, conf->pool);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling napr_threadpool_init: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	return status;
    }

    for (unsigned int idx = 0; idx < heap_size; idx++) {
	compute_vector_task_t *task = apr_palloc(conf->pool, sizeof(compute_vector_task_t));
	task->cv_ctx = &cv_ctx;
	task->file = napr_heap_get_nth(conf->heap, idx);
	status = napr_threadpool_add(threadpool, task);
	if (APR_SUCCESS != status) {
	    DEBUG_ERR("error calling napr_threadpool_add: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	    return status;
	}
    }

    napr_threadpool_wait(threadpool);
    status = apr_thread_mutex_destroy(cv_ctx.mutex);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_destroy: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	return status;
    }

    if (is_option_set(conf->mask, OPTION_VERBO)) {
	(void) fprintf(stderr, "\rProgress [%u/%u] %d%% ", heap_size, heap_size, MAX_PERCENTAGE);
	(void) fprintf(stderr, "\n");
    }

    return APR_SUCCESS;
}

static void compare_image_vectors(ft_conf_t *conf, PuzzleContext * context)
{
    unsigned long nb_cmp = napr_heap_size(conf->heap) * (napr_heap_size(conf->heap) - 1) / 2;
    unsigned long cnt_cmp = 0;
    ft_file_t *file = NULL;

    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (!(file->cvec_ok & 0x1)) {
	    continue;
	}

	unsigned char already_printed = 0;
	unsigned int heap_size = napr_heap_size(conf->heap);
	for (unsigned int idx = 0; idx < heap_size; idx++) {
	    ft_file_t *file_cmp = napr_heap_get_nth(conf->heap, idx);
	    if (!(file_cmp->cvec_ok & 0x1)) {
		continue;
	    }

	    double distance =
		puzzle_vector_normalized_distance(context, &(file->cvec), &(file_cmp->cvec), FIX_FOR_CLUSTERING);
	    if (distance < conf->threshold) {
		if (!already_printed) {
		    (void) printf("%s%c", file->path, conf->sep);
		    already_printed = 1;
		}
		else {
		    (void) printf("%c", conf->sep);
		}
		(void) printf("%s", file_cmp->path);
	    }
	    if (is_option_set(conf->mask, OPTION_VERBO)) {
		(void) fprintf(stderr, "\rCompare progress [%10lu/%10lu] %02.2f%% ", cnt_cmp, nb_cmp,
			       (double) ((double) cnt_cmp / (double) nb_cmp * 100.0F));
	    }
	    cnt_cmp++;
	}

	if (already_printed) {
	    (void) printf("\n\n");
	}

	puzzle_free_cvec(context, &(file->cvec));
    }

    if (is_option_set(conf->mask, OPTION_VERBO)) {
	(void) fprintf(stderr, "\rCompare progress [%10lu/%10lu] %02.2f%% ", cnt_cmp, nb_cmp, 100.0);
	(void) fprintf(stderr, "\n");
    }
}

#endif /* HAVE_PUZZLE */
