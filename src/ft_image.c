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

apr_status_t ft_image_twin_report(ft_conf_t *conf)
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

#endif /* HAVE_PUZZLE */
