/*
 * Copyright (C) 2007 François Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>

#include <apr_thread_proc.h>
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>

#include "napr_threadpool.h"
#include "debug.h"

/* Definitions of some basic list methods, see further to find pure threadpool stuff */
typedef struct napr_cell_t napr_cell_t;

struct napr_cell_t
{
    napr_cell_t *next;
    void *data;
};

typedef struct napr_list_t napr_list_t;

struct napr_list_t
{
    apr_pool_t *p;
    napr_cell_t *head;
    napr_cell_t *tail;
    unsigned long nb_cells;
};

static inline napr_list_t *napr_list_make(apr_pool_t *p)
{
    apr_pool_t *local_pool;
    napr_list_t *napr_list;

    apr_pool_create(&local_pool, p);
    napr_list = apr_palloc(local_pool, sizeof(struct napr_list_t));

    if (napr_list != NULL) {
	napr_list->p = local_pool;
	napr_list->head = NULL;
	napr_list->tail = NULL;
	napr_list->nb_cells = 0UL;
    }

    return napr_list;
}

static int napr_list_cons(napr_list_t *napr_list, void *element)
{
    napr_cell_t *cell;
    int rc; /** return code. */

    if (NULL != (cell = (napr_cell_t *) apr_palloc(napr_list->p, sizeof(struct napr_cell_t)))) {
	cell->data = element;
	cell->next = napr_list->head;
	napr_list->head = cell;
	napr_list->nb_cells += 1;

	if (napr_list->nb_cells == 1)
	    napr_list->tail = napr_list->head;

	rc = 0;
    }
    else {
	rc = -1;
    }

    return rc;
}

static inline void napr_list_cdr(napr_list_t *napr_list)
{
    void *cell;

    cell = napr_list->head->next;
    napr_list->nb_cells -= 1UL;
    napr_list->head = cell;
}

static inline void napr_list_delete(napr_list_t *napr_list)
{
    while (napr_list->head != NULL)
	napr_list_cdr(napr_list);

    apr_pool_destroy(napr_list->p);
}

static inline int napr_list_enqueue(napr_list_t *napr_list, void *element)
{
    napr_cell_t *cell;
    int rc = 0;

    if (0 != napr_list->nb_cells) {
	if (NULL != (cell = (napr_cell_t *) apr_palloc(napr_list->p, sizeof(struct napr_cell_t)))) {
	    napr_list->nb_cells += 1UL;
	    cell->data = element;
	    cell->next = NULL;
	    napr_list->tail->next = cell;
	    napr_list->tail = cell;
	}
	else
	    rc = -1;
    }
    else {
	rc = napr_list_cons(napr_list, element);
    }

    return rc;
}

static inline napr_cell_t *napr_list_first(napr_list_t *napr_list)
{
    return napr_list->head;
}

static inline unsigned long napr_list_size(napr_list_t *napr_list)
{
    return napr_list->nb_cells;
}

static inline napr_cell_t *napr_list_next(napr_cell_t *cell)
{
    return cell->next;
}

static inline void *napr_list_get(napr_cell_t *cell)
{
    return cell->data;
}

/* The threadpool structures and engine */
struct napr_threadpool_t
{
    apr_thread_t **thread;

    void *ctx;
    /* This mutex protects everything below in writing and reading */
    apr_thread_mutex_t *threadpool_mutex;
    apr_thread_cond_t *threadpool_update;
    unsigned long nb_thread;
    unsigned long nb_waiting;
    napr_list_t *list;
    threadpool_process_data_callback_fn_t *process_data;
    apr_pool_t *pool;

    /*
     * run == 0x1 means we are currently processing data
     * run == 0x0 means we are creating the pool, OR (exclusive) we were
     *        processing data and encountered a end of list and all thread are
     *        waiting (no more data).
     * Algorithm : 
     * run &= 0x0;
     *     create the thread pool loops :
     *         code in the loop say :
     *         run at 0x0, we can be nb_thread == nb_waiting in a "wait" mode.
     *         This doesn't matter.
     *
     * external caller function add all data to process, then call the napr_threadpool_wait:
     *     napr_threadpool_add_data:
     *         fill list.
     *     napr_threadpool_wait:
     *         run |= 0x1;
     *
     *         code in the loop say :
     *         run at 0x1, if we are at nb_thread == nb_waiting I'd better warn
     *         the caller of the napr_threadpool_wait function that the processing
     *         is ended.
     */

    unsigned int run:1;
    unsigned int ended:1;
};

static void *APR_THREAD_FUNC napr_threadpool_loop(apr_thread_t *thd, void *rec);

extern apr_status_t napr_threadpool_init(napr_threadpool_t **threadpool, void *ctx, unsigned long nb_thread,
					 threadpool_process_data_callback_fn_t *process_data, apr_pool_t *pool)
{
    char errbuf[128];
    apr_pool_t *local_pool;
    unsigned long l;
    apr_status_t status;

    apr_pool_create(&local_pool, pool);
    (*threadpool) = apr_palloc(local_pool, sizeof(struct napr_threadpool_t));
    (*threadpool)->pool = local_pool;

    if (APR_SUCCESS !=
	(status =
	 apr_thread_mutex_create(&((*threadpool)->threadpool_mutex), APR_THREAD_MUTEX_DEFAULT, (*threadpool)->pool))) {
	DEBUG_ERR("error calling apr_thread_mutex_create: %s", apr_strerror(status, errbuf, 128));
	return status;
    }
    if (APR_SUCCESS != (status = apr_thread_cond_create(&((*threadpool)->threadpool_update), (*threadpool)->pool))) {
	DEBUG_ERR("error calling apr_thread_cond_create: %s", apr_strerror(status, errbuf, 128));
	return status;
    }
    (*threadpool)->thread = apr_palloc((*threadpool)->pool, nb_thread * sizeof(apr_thread_mutex_t *));
    (*threadpool)->ctx = ctx;
    (*threadpool)->nb_thread = nb_thread;
    (*threadpool)->nb_waiting = 0UL;
    (*threadpool)->list = napr_list_make((*threadpool)->pool);
    (*threadpool)->process_data = process_data;
    (*threadpool)->run &= 0x0;
    (*threadpool)->ended &= 0x0;

    for (l = 0; l < nb_thread; l++) {
	if (APR_SUCCESS !=
	    (status =
	     apr_thread_create(&((*threadpool)->thread[l]), NULL, napr_threadpool_loop, (*threadpool),
			       (*threadpool)->pool))) {
	    DEBUG_ERR("error calling apr_thread_create: %s", apr_strerror(status, errbuf, 128));
	    return status;
	}
    }

    return APR_SUCCESS;
}

extern apr_status_t napr_threadpool_add(napr_threadpool_t *threadpool, void *data)
{
    char errbuf[128];
    apr_status_t status;

    if (APR_SUCCESS != (status = apr_thread_mutex_lock(threadpool->threadpool_mutex))) {
	DEBUG_ERR("error calling apr_thread_mutex_lock: %s", apr_strerror(status, errbuf, 128));
	return status;
    }
    threadpool->ended &= 0x0;
    napr_list_enqueue(threadpool->list, data);
    if (APR_SUCCESS != (status = apr_thread_mutex_unlock(threadpool->threadpool_mutex))) {
	DEBUG_ERR("error calling apr_thread_mutex_unlock: %s", apr_strerror(status, errbuf, 128));
	return status;
    }
    if (APR_SUCCESS != (status = apr_thread_cond_signal(threadpool->threadpool_update))) {
	DEBUG_ERR("error calling apr_thread_cond_signal: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    return APR_SUCCESS;
}

extern apr_status_t napr_threadpool_wait(napr_threadpool_t *threadpool)
{
    char errbuf[128];
    apr_status_t status;
    int list_size = 0;

    /* DEBUG_DBG("Called"); */
    if (APR_SUCCESS != (status = apr_thread_mutex_lock(threadpool->threadpool_mutex))) {
	DEBUG_ERR("error calling apr_thread_mutex_lock: %s", apr_strerror(status, errbuf, 128));
	return status;
    }
    list_size = napr_list_size(threadpool->list);
    if ((0 != list_size) || (threadpool->nb_waiting != threadpool->nb_thread)) {
	/* DEBUG_DBG("After lock before wait"); */
	/*
	 * Because the caller of this function has added all data we are now
	 * running, the line after, (cond_wait), make us wait to the end of
	 * processing.
	 */
	threadpool->run |= 0x1;

	if (APR_SUCCESS != (status = apr_thread_cond_wait(threadpool->threadpool_update, threadpool->threadpool_mutex))) {
	    DEBUG_ERR("error calling apr_thread_cond_wait: %s", apr_strerror(status, errbuf, 128));
	    return status;
	}
	/* DEBUG_DBG("Awake"); */
	/* Because all data have been processed, we are no more running  */
	threadpool->run &= 0x0;
    }
    /*
     * garbage collecting under lock protection to avoid list manipulation
     * during this freeing.
     * We are doing this garbage collecting because if not, we will re-using
     * the same pool to very-often alloc cell_t structures in list.
     * This could lead to a memory leak if not regularly cleaned.
     */
    napr_list_delete(threadpool->list);
    threadpool->list = napr_list_make(threadpool->pool);
    if (APR_SUCCESS != (status = apr_thread_mutex_unlock(threadpool->threadpool_mutex))) {
	DEBUG_ERR("error calling apr_thread_mutex_unlock: %s", apr_strerror(status, errbuf, 128));
	return status;
    }

    return APR_SUCCESS;
}

static void *APR_THREAD_FUNC napr_threadpool_loop(apr_thread_t *thd, void *rec)
{
    char errbuf[128];
    napr_threadpool_t *threadpool = rec;
    apr_status_t status;

    /* lock the mutex, to access the list exclusively. */
    if (APR_SUCCESS != (status = apr_thread_mutex_lock(threadpool->threadpool_mutex))) {
	DEBUG_ERR("error calling apr_thread_mutex_lock: %s", apr_strerror(status, errbuf, 128));
	return NULL;
    }

    /* do forever.... */
    while (1) {
	/* DEBUG_DBG("list_size: %lu", napr_list_size(threadpool->list)); */
	if ((0 < napr_list_size(threadpool->list)) && !(threadpool->ended & 0x1)) {
	    napr_cell_t *cell;
	    void *data;

	    cell = napr_list_first(threadpool->list);
	    data = napr_list_get(cell);
	    napr_list_cdr(threadpool->list);
	    if (data) {
		/*
		 * unlock mutex - because other threads would be able to handle
		 * other data waiting in the queue paralelly.
		 */
		if (APR_SUCCESS != (status = apr_thread_mutex_unlock(threadpool->threadpool_mutex))) {
		    DEBUG_ERR("error calling apr_thread_mutex_unlock: %s", apr_strerror(status, errbuf, 128));
		    return NULL;
		}
		threadpool->process_data(threadpool->ctx, data);
		if (APR_SUCCESS != (status = apr_thread_mutex_lock(threadpool->threadpool_mutex))) {
		    DEBUG_ERR("error calling apr_thread_mutex_lock: %s", apr_strerror(status, errbuf, 128));
		    return NULL;
		}
	    }
	}
	else {			/* The waiting else */
	    threadpool->nb_waiting += 1UL;

	    /* DEBUG_DBG("run: %i waiting: %lu / thread: %lu", (threadpool->run & 0x1) ? 1 : 0, threadpool->nb_waiting, threadpool->nb_thread); */
	    /* Should not broadcast if it's already ended, because it may lead to an infinite loop */
	    if (!(threadpool->ended & 0x1) && (threadpool->run & 0x1) && (threadpool->nb_waiting == threadpool->nb_thread)) {
		threadpool->ended |= 0x1;
		if (APR_SUCCESS != (status = apr_thread_cond_broadcast(threadpool->threadpool_update))) {
		    DEBUG_ERR("error calling apr_thread_cond_signal: %s", apr_strerror(status, errbuf, 128));
		    return NULL;
		}
	    }

	    /*
	     * wait for a new data. note the mutex will be unlocked in
	     * apr_thread_cond_wait(), thus allowing other threads access to data
	     * list.
	     */
	    if (APR_SUCCESS != (status = apr_thread_cond_wait(threadpool->threadpool_update, threadpool->threadpool_mutex))) {
		DEBUG_ERR("error calling apr_thread_cond_wait: %s", apr_strerror(status, errbuf, 128));
		return NULL;
	    }
	    /*
	     * after we return from apr_thread_cond_wait, the mutex is locked
	     * again, so we don't need to lock it ourselves
	     */
	    threadpool->nb_waiting -= 1UL;
	}
    }
}


