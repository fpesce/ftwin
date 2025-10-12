/*
 *
 * Copyright (C) 2024 François Pesce : francois.pesce (at) gmail (dot) com
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

#ifndef FT_HASHING_H
#define FT_HASHING_H

#include <apr_thread_mutex.h>
#include <apr_atomic.h>

#include "ft_file_types.h"
#include "napr_hash.h"

/**
 * @brief Shared context for hashing operations.
 *
 * This structure holds data that is shared across all worker threads in the
 * hashing thread pool. It includes the results hash table, synchronization
 * primitives, and configuration settings.
 */
typedef struct {
    napr_hash_t *checksums_ht;
    apr_thread_mutex_t *checksums_mutex;
    apr_pool_t *pool;
    apr_off_t max_read_size;

    volatile apr_uint32_t files_hashed_count;

    apr_thread_mutex_t *stats_mutex;
    apr_off_t bytes_hashed_total;
} hashing_context_t;

/**
 * @brief Task structure for a single hashing operation.
 *
 * This structure is used to pass information about a file to be hashed to a
 * worker thread.
 */
typedef struct {
    ft_file_t *file_info;
} hashing_task_t;

/**
 * @brief Get the number of available CPU cores.
 *
 * @return The number of CPU cores.
 */
unsigned int ft_get_cpu_cores(void);

/**
 * @brief Processes file sizes to identify potential duplicates and checksums them
 *        using a thread pool.
 *
 * @param conf The configuration structure.
 * @param pool The memory pool.
 * @return APR_SUCCESS on success, or an error code on failure.
 */
apr_status_t ft_conf_process_sizes_multithreaded(ft_conf_t *conf, apr_pool_t *pool);

#endif /* FT_HASHING_H */