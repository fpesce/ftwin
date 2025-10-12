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

#include "ft_hashing.h"
#include "napr_threadpool.h"
#include "ft_file.h"
#include "napr_hash.h"

#include <unistd.h>

#ifdef WIN32
#include <windows.h>
#endif

unsigned int ft_get_cpu_cores(void)
{
#if defined(_SC_NPROCESSORS_ONLN)
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs > 0) {
        return (unsigned int)nprocs;
    }
#elif defined(WIN32)
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#endif
    return 4; /* Default fallback */
}

static void insert_into_checksums_ht(napr_hash_t *ht, ft_file_t *file, apr_pool_t *pool)
{
    apr_uint32_t hash_value;
    napr_list_t *list;

    list = napr_hash_search(ht, &file->hash, sizeof(ft_hash_t), &hash_value);
    if (list == NULL) {
        ft_fsize_t *fsize = apr_palloc(pool, sizeof(ft_fsize_t));
        fsize->val = file->size;
        fsize->file_list = napr_list_make(pool);
        fsize->nb_files = 0;
        memcpy(&fsize->key, &file->hash, sizeof(ft_hash_t));
        list = fsize->file_list;
        napr_hash_set(ht, fsize, hash_value);
    }
    napr_list_enqueue(list, file);
}

static apr_status_t hashing_worker_callback(void *ctx, void *data)
{
    hashing_context_t *h_ctx = (hashing_context_t *)ctx;
    hashing_task_t *task = (hashing_task_t *)data;
    ft_file_t *file = task->file_info;
    apr_pool_t *subpool;
    apr_status_t status;

    if (apr_pool_create(&subpool, h_ctx->pool) != APR_SUCCESS) {
        return APR_EGENERAL;
    }

    status = checksum_file_optimized(file->path, file->size, h_ctx->max_read_size, &file->hash, subpool);

    if (status == APR_SUCCESS) {
        apr_thread_mutex_lock(h_ctx->checksums_mutex);
        insert_into_checksums_ht(h_ctx->checksums_ht, file, h_ctx->pool);
        apr_thread_mutex_unlock(h_ctx->checksums_mutex);

        apr_atomic_inc32(&h_ctx->files_hashed_count);

        apr_thread_mutex_lock(h_ctx->stats_mutex);
        h_ctx->bytes_hashed_total += file->size;
        apr_thread_mutex_unlock(h_ctx->stats_mutex);
    } else {
        /* DEBUG_ERR("Error hashing file: %s", file->path); */
    }

    apr_pool_destroy(subpool);
    return status;
}

apr_status_t ft_conf_process_sizes_multithreaded(ft_conf_t *conf, apr_pool_t *pool)
{
    napr_threadpool_t *threadpool;
    hashing_context_t h_ctx;
    apr_status_t status;
    unsigned long num_threads = conf->num_threads > 0 ? conf->num_threads : ft_get_cpu_cores();

    memset(&h_ctx, 0, sizeof(h_ctx));
    h_ctx.pool = pool;
    h_ctx.checksums_ht = conf->checksums_ht;
    h_ctx.max_read_size = conf->excess_size;

    status = apr_thread_mutex_create(&h_ctx.checksums_mutex, APR_THREAD_MUTEX_DEFAULT, pool);
    if (status != APR_SUCCESS) return status;
    status = apr_thread_mutex_create(&h_ctx.stats_mutex, APR_THREAD_MUTEX_DEFAULT, pool);
    if (status != APR_SUCCESS) return status;

    status = napr_threadpool_init(&threadpool, &h_ctx, num_threads, hashing_worker_callback, pool);
    if (status != APR_SUCCESS) return status;

    napr_hash_index_t *hi;
    for (hi = napr_hash_first(pool, conf->sizes); hi; hi = napr_hash_next(hi)) {
        ft_fsize_t *fsize;
        napr_hash_this(hi, NULL, NULL, (void**)&fsize);

        if (napr_list_count(fsize->file_list) > 1) {
            napr_cell_t *cell;
            for (cell = napr_list_first(fsize->file_list); cell; cell = napr_list_next(cell)) {
                ft_file_t *file = (ft_file_t *)napr_list_get(cell);
                hashing_task_t *task = apr_palloc(pool, sizeof(hashing_task_t));
                task->file_info = file;
                status = napr_threadpool_add(threadpool, task);
                if (status != APR_SUCCESS) {
                    /* Log error */
                }
            }
        }
    }

    status = napr_threadpool_wait(threadpool);
    return status;
}