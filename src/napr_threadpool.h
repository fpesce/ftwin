/**
 * @file napr_threadpool.h
 * @brief A simple fixed-size thread pool for concurrent task processing.
 * @ingroup DataStructures
 */
/*
 * Copyright (C) 2007 Francois Pesce : francois.pesce (at) gmail (dot) com
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

/**
 * @brief Opaque thread pool structure.
 */
typedef struct napr_threadpool_t napr_threadpool_t;

/**
 * @brief The worker function executed by threads in the pool.
 *
 * This function is called for each data item added to the pool's queue.
 *
 * @param[in] ctx A pointer to the shared context, provided during pool initialization.
 * @param[in] data A pointer to the data item to be processed.
 * @return APR_SUCCESS on successful processing. Any other return value is considered an error.
 */
typedef apr_status_t (threadpool_process_data_callback_fn_t) (void *ctx, void *data);

/**
 * @brief Initializes a thread pool.
 *
 * This creates a pool with a fixed number of worker threads that will process tasks
 * from a queue.
 *
 * @param[out] threadpool A pointer to a `napr_threadpool_t*` that will be allocated and initialized.
 * @param[in] ctx A user-defined context pointer that will be passed to the worker function.
 * @param[in] nb_thread The number of worker threads to create in the pool.
 * @param[in] process_data The callback function that threads will execute to process tasks.
 * @param[in] pool The APR pool to use for allocating the thread pool and its resources.
 * @return APR_SUCCESS on success, or an error code on failure.
 */
apr_status_t napr_threadpool_init(napr_threadpool_t **threadpool, void *ctx, unsigned long nb_thread,
				  threadpool_process_data_callback_fn_t *process_data, apr_pool_t *pool);

/**
 * @brief Adds a task (a data item) to the thread pool's processing queue.
 *
 * A waiting worker thread will pick up and process this item.
 *
 * @param[in] threadpool The thread pool.
 * @param[in] data A pointer to the data item to be processed.
 * @return APR_SUCCESS on success, or an error code if the task could not be added.
 */
apr_status_t napr_threadpool_add(napr_threadpool_t *threadpool, void *data);

/**
 * @brief Waits until all tasks currently in the queue have been processed.
 *
 * This function blocks until the number of pending tasks reaches zero.
 *
 * @param[in] threadpool The thread pool.
 * @return APR_SUCCESS on success.
 */
apr_status_t napr_threadpool_wait(napr_threadpool_t *threadpool);

/**
 * @brief Shuts down the thread pool.
 *
 * This function signals all worker threads to terminate, waits for them to finish
 * their current tasks, and then joins them. No new tasks can be added after this

 * function is called.
 *
 * @param[in] threadpool The thread pool to shut down.
 * @return APR_SUCCESS on success.
 */
apr_status_t napr_threadpool_shutdown(napr_threadpool_t *threadpool);

#endif /* NAPR_THREADPOOL_H */
