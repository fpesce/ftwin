/**
 * @file napr_heap.c
 * @brief Implementation of the generic binary heap.
 * @ingroup DataStructures
 */
/*
 * Copyright (C) 2007 François Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <apr_thread_mutex.h>
#include <string.h>

#include "debug.h"
#include "napr_heap.h"

#define INITIAL_MAX 256
#define NAPR_HEAP_PARENT(position) (((position) - 1) >> 1)
#define NAPR_HEAP_LEFT(position)   (((position) << 1) + 1)
#define NAPR_HEAP_RIGHT(position)  (((position) + 1) << 1)

struct napr_heap_t
{
    apr_pool_t *pool;
    apr_thread_mutex_t *mutex;
    void **tree;
    napr_heap_cmp_callback_fn_t *cmp;
    unsigned int count, max;
    int mutex_set;		/* true (i.e. 1) if napr_heap_make_r has been
				   called instead of non-reentrant function */
};

#ifdef HAVE_APR
/* APR_POOL_IMPLEMENT_ACCESSOR(heap); */
#endif

napr_heap_t *napr_heap_make(apr_pool_t *pool, napr_heap_cmp_callback_fn_t *cmp)
{
    napr_heap_t *heap = NULL;
    apr_pool_t *local_pool;

    if (APR_SUCCESS == (apr_pool_create(&local_pool, pool))) {
        heap = apr_palloc(local_pool, sizeof(napr_heap_t));
        heap->pool = local_pool;
        heap->tree = (void **) apr_pcalloc(local_pool, sizeof(void *) * INITIAL_MAX);
    }

    if (NULL != heap) {
        heap->max = INITIAL_MAX;
        heap->count = 0;
        heap->cmp = cmp;
        heap->mutex_set = 0;
        heap->mutex = NULL;
    }

    return heap;
}

int napr_heap_insert(napr_heap_t *heap, void *datum)
{
    void **tmp;
    unsigned int ipos, ppos;

    if (heap->max <= heap->count) {
        /*
         * reallocation by power of 2:
         */
        unsigned int new_max;

        for (new_max = 1; new_max <= heap->max; new_max *= 2);

        tmp = apr_palloc(heap->pool, new_max * sizeof(void *));
        if (NULL != tmp) {
            memcpy(tmp, (heap->tree), (heap->count) * sizeof(void *));
            memset((tmp + (heap->count)), 0, (new_max - (heap->count + 1)) * sizeof(void *));
            heap->tree = tmp;
            heap->max = new_max;
        }
        else {
            heap->tree = NULL;
            DEBUG_ERR("allocation failed");
            return -1;
        }
    }

    /*
     * insertion of the datum after the last one of the tree...
     */
    heap->tree[heap->count] = datum;

    ipos = heap->count;
    ppos = NAPR_HEAP_PARENT(ipos);

    while (ipos > 0 && (heap->cmp(heap->tree[ppos], heap->tree[ipos]) < 0)) {
	/*
	 * Swap the value ...
	 */
	tmp = heap->tree[ppos];
	heap->tree[ppos] = heap->tree[ipos];
	heap->tree[ipos] = tmp;

	ipos = ppos;
	ppos = NAPR_HEAP_PARENT(ipos);
    }

    heap->count++;

    return 0;
}

void *napr_heap_extract(napr_heap_t *heap)
{
    void *ret = NULL, *tmp;
    unsigned int ipos, rpos, lpos, mpos;

    if ((0 != heap->count) && (NULL != heap->tree)) {
	/* keep the value to return */
	ret = heap->tree[0];
	/* It works for count == 1 too (just think about it) */
	heap->tree[0] = heap->tree[heap->count - 1];
	heap->tree[heap->count - 1] = NULL;
	heap->count--;
	ipos = 0;

	while (1) {
	    lpos = NAPR_HEAP_LEFT(ipos);
	    rpos = NAPR_HEAP_RIGHT(ipos);

	    if (lpos < heap->count) {
		if (heap->cmp(heap->tree[lpos], heap->tree[ipos]) > 0) {
		    mpos = lpos;
		}
		else {
		    mpos = ipos;
		}
		if ((rpos < heap->count) && (heap->cmp(heap->tree[rpos], heap->tree[mpos])) > 0) {
		    mpos = rpos;
		}
	    }
	    else {
		mpos = ipos;
	    }

	    if (mpos != ipos) {
		/*
		 * Swap the choosen children with the current node
		 */
		tmp = heap->tree[mpos];
		heap->tree[mpos] = heap->tree[ipos];
		heap->tree[ipos] = tmp;
		ipos = mpos;
	    }
	    else {
		break;
	    }
	}
    }

    return ret;
}

void *napr_heap_get_nth(const napr_heap_t *heap, unsigned int n)
{
    if ((n < heap->count) && (NULL != heap->tree))
	return heap->tree[n];
    else
	return NULL;
}

unsigned int napr_heap_size(const napr_heap_t *heap)
{
    return heap->count;
}

/*
 * Reentrant versions of previous functions.
 */
napr_heap_t *napr_heap_make_r(apr_pool_t *pool, napr_heap_cmp_callback_fn_t *cmp)
{
    napr_heap_t *heap;

    if (NULL != (heap = napr_heap_make(pool, cmp))) {
        if (APR_SUCCESS != apr_thread_mutex_create(&(heap->mutex), APR_THREAD_MUTEX_DEFAULT, pool))
            heap = NULL;
    }

    if (NULL != heap)
        heap->mutex_set = 1;

    return heap;
}

int napr_heap_insert_r(napr_heap_t *heap, void *datum)
{
    int rc;

    if (1 == heap->mutex_set) {
        if (APR_SUCCESS == (rc = apr_thread_mutex_lock(heap->mutex))) {
            if (0 == (rc = napr_heap_insert(heap, datum)))
                rc = apr_thread_mutex_unlock(heap->mutex);
        }
        else {
            DEBUG_ERR("locking failed");
        }
    }
    else
        rc = -1;

    return rc;
}
