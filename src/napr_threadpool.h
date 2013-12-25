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

#ifndef NAPR_THREADPOOL_H
#define NAPR_THREADPOOL_H

#include <apr_pools.h>

typedef struct napr_threadpool_t napr_threadpool_t;

/** 
 * Function that will be processed by a thread from the pool upon a data added to the pool.
 * @param rec The data added via napr_threadpool_add_data
 * @return APR_SUCCESS if no error occured.
 */
typedef apr_status_t (threadpool_process_data_callback_fn_t) (void *ctx, void *data);

/** 
 * Initialize a threadpool, it's an engine that keeps n threads running on data processing.
 * @param threadpool The addresse of a pointer to the opaque structure to allocate.
 * @param ctx A global context to pass as a first argument to callback function.
 * @param nb_thread The number of thread to allocate to your computation.
 * @param process_data A callback that will be run by a thread on a data added to the pool later.
 * @param pool The apr_pool_t to allocate from.
 * @return APR_SUCCESS if no error occured.
 */
apr_status_t napr_threadpool_init(napr_threadpool_t **threadpool, void *ctx, unsigned long nb_thread,
				  threadpool_process_data_callback_fn_t *process_data, apr_pool_t *pool);

/** 
 * Add data to process to the pool.
 * @param threadpool The opaque threadpool.
 * @param data The data of any type.
 * @return APR_SUCCESS if no error occured.
 */
apr_status_t napr_threadpool_add(napr_threadpool_t *threadpool, void *data);

/** 
 * This function wait for the pool to process all the data that has been submitted.
 * @param threadpool The opaque threadpool.
 * @return APR_SUCCESS if no error occured.
 */
apr_status_t napr_threadpool_wait(napr_threadpool_t *threadpool);

#endif /* NAPR_THREADPOOL_H */
