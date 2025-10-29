#include <apr_thread_mutex.h>
#include <apr_strings.h>

#include "debug.h"
#include "ft_config.h"
#include "ft_constants.h"
#include "ft_process.h"
#include "ft_system.h"
#include "ft_types.h"
#include "napr_threadpool.h"
#include "napr_hash.h"
#include <apr_file_io.h>
#include "ft_file.h"
#include "napr_cache.h"

int ft_file_cmp(const void *param1, const void *param2);

#include <archive.h>
#include "ft_archive.h"

/* Forward declarations for hashing_worker_callback helpers */
static char *prepare_filepath(hashing_context_t *h_ctx, ft_file_t *file, apr_pool_t *subpool);
static apr_status_t collect_hashing_result(hashing_context_t *h_ctx, ft_file_t *file, ft_hash_t hash_value);
static apr_status_t update_hashing_stats(hashing_context_t *h_ctx);
static void handle_hashing_success(hashing_context_t *h_ctx, ft_file_t *file, ft_hash_t hash_value);
static void handle_hashing_error(hashing_context_t *h_ctx, ft_file_t *file, apr_status_t status);

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static apr_status_t hashing_worker_callback(void *hashing_ctx, void *task_data)
{
    hashing_context_t *h_ctx = (hashing_context_t *) hashing_ctx;
    hashing_task_t *task = (hashing_task_t *) task_data;
    ft_fsize_t *fsize = task->fsize;
    ft_file_t *file = fsize->chksum_array[task->index].file;
    apr_pool_t *subpool = NULL;
    apr_status_t status = apr_pool_create(&subpool, h_ctx->pool);

    if (APR_SUCCESS != status) {
        char errbuf[ERR_BUF_SIZE];
        DEBUG_ERR("error creating subpool: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    char *filepath = prepare_filepath(h_ctx, file, subpool);
    if (filepath == NULL) {
        apr_pool_destroy(subpool);
        return APR_EGENERAL;
    }

    ft_hash_t hash_value;
    status = checksum_file(filepath, file->size, h_ctx->conf->excess_size, &hash_value, subpool);

    if (is_option_set(h_ctx->conf->mask, OPTION_UNTAR) && (NULL != file->subpath)) {
        (void) apr_file_remove(filepath, subpool);
    }

    if (APR_SUCCESS == status) {
        fsize->chksum_array[task->index].hash_value = hash_value;
        handle_hashing_success(h_ctx, file, hash_value);
    }
    else {
        handle_hashing_error(h_ctx, file, status);
    }

    apr_pool_destroy(subpool);
    return status;
}

static char *prepare_filepath(hashing_context_t *h_ctx, ft_file_t *file, apr_pool_t *subpool)
{
    if (is_option_set(h_ctx->conf->mask, OPTION_UNTAR) && (NULL != file->subpath)) {
        char *filepath = ft_archive_untar_file(file, subpool);
        if (NULL == filepath) {
            DEBUG_ERR("error calling ft_archive_untar_file");
        }
        return filepath;
    }
    return file->path;
}

static apr_status_t collect_hashing_result(hashing_context_t *h_ctx, ft_file_t *file, ft_hash_t hash_value)
{
    char errbuf[ERR_BUF_SIZE];
    apr_status_t lock_status = apr_thread_mutex_lock(h_ctx->results_mutex);

    if (APR_SUCCESS != lock_status) {
        DEBUG_ERR("error locking results mutex: %s", apr_strerror(lock_status, errbuf, ERR_BUF_SIZE));
        return lock_status;
    }

    hashing_result_t *result = apr_palloc(h_ctx->pool, sizeof(hashing_result_t));
    if (result == NULL) {
        DEBUG_ERR("error allocating cache result structure");
        (void) apr_thread_mutex_unlock(h_ctx->results_mutex);
        return APR_ENOMEM;
    }

    result->filename = apr_pstrdup(h_ctx->pool, file->path);
    if (result->filename == NULL) {
        DEBUG_ERR("error duplicating filename for cache result");
        (void) apr_thread_mutex_unlock(h_ctx->results_mutex);
        return APR_ENOMEM;
    }

    result->mtime = file->mtime;
    result->ctime = file->ctime;
    result->size = file->size;
    result->hash = hash_value;
    APR_ARRAY_PUSH(h_ctx->results, hashing_result_t *) = result;

    lock_status = apr_thread_mutex_unlock(h_ctx->results_mutex);
    if (APR_SUCCESS != lock_status) {
        DEBUG_ERR("error unlocking results mutex: %s", apr_strerror(lock_status, errbuf, ERR_BUF_SIZE));
    }

    return lock_status;
}

static apr_status_t update_hashing_stats(hashing_context_t *h_ctx)
{
    char errbuf[ERR_BUF_SIZE];
    apr_status_t lock_status = apr_thread_mutex_lock(h_ctx->stats_mutex);

    if (APR_SUCCESS != lock_status) {
        DEBUG_ERR("error locking stats mutex: %s", apr_strerror(lock_status, errbuf, ERR_BUF_SIZE));
        return lock_status;
    }

    h_ctx->files_processed++;

    if (is_option_set(h_ctx->conf->mask, OPTION_VERBO)) {
        (void) fprintf(stderr, "\rProgress [%" APR_SIZE_T_FMT "/%" APR_SIZE_T_FMT "] %d%% ",
                       h_ctx->files_processed, h_ctx->total_files, (int) ((float) h_ctx->files_processed / (float) h_ctx->total_files * 100.0F));
    }

    lock_status = apr_thread_mutex_unlock(h_ctx->stats_mutex);
    if (APR_SUCCESS != lock_status) {
        DEBUG_ERR("error unlocking stats mutex: %s", apr_strerror(lock_status, errbuf, ERR_BUF_SIZE));
    }

    return lock_status;
}

static void handle_hashing_success(hashing_context_t *h_ctx, ft_file_t *file, ft_hash_t hash_value)
{
    (void) collect_hashing_result(h_ctx, file, hash_value);
    (void) update_hashing_stats(h_ctx);
}

static void handle_hashing_error(hashing_context_t *h_ctx, ft_file_t *file, apr_status_t status)
{
    if (is_option_set(h_ctx->conf->mask, OPTION_VERBO)) {
        char errbuf[ERR_BUF_SIZE];

        (void) fprintf(stderr, "\nskipping %s because: %s\n", file->path, apr_strerror(status, errbuf, ERR_BUF_SIZE));
    }
}

/* Forward declarations for helper functions */
static apr_status_t categorize_files(ft_conf_t *conf, napr_heap_t *tmp_heap, apr_size_t *total_hash_tasks);
static apr_status_t dispatch_hashing_tasks(ft_conf_t *conf, apr_pool_t *gc_pool, apr_size_t total_hash_tasks, hashing_context_t *h_ctx);
static apr_status_t collect_hashing_results(ft_conf_t *conf, napr_heap_t *tmp_heap, apr_pool_t *gc_pool);
static apr_status_t update_cache_with_results(ft_conf_t *conf, hashing_context_t *h_ctx);

apr_status_t ft_process_files(ft_conf_t *conf)
{
    napr_heap_t *tmp_heap = NULL;
    apr_pool_t *gc_pool = NULL;
    apr_status_t status = APR_SUCCESS;
    apr_size_t total_hash_tasks = 0;
    hashing_context_t h_ctx;
    char errbuf[ERR_BUF_SIZE];

    memset(&h_ctx, 0, sizeof(h_ctx));

    if (is_option_set(conf->mask, OPTION_VERBO)) {
        (void) fprintf(stderr, "Referencing files and sizes:\n");
    }

    status = apr_pool_create(&gc_pool, conf->pool);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    tmp_heap = napr_heap_make(conf->pool, ft_file_cmp);

    status = categorize_files(conf, tmp_heap, &total_hash_tasks);
    if (status != APR_SUCCESS) {
        apr_pool_destroy(gc_pool);
        return status;
    }

    if (total_hash_tasks > 0) {
        /* Initialize hashing context */
        h_ctx.conf = conf;
        h_ctx.pool = gc_pool;
        h_ctx.files_processed = 0;
        h_ctx.total_files = total_hash_tasks;

        status = apr_thread_mutex_create(&h_ctx.stats_mutex, APR_THREAD_MUTEX_DEFAULT, gc_pool);
        if (APR_SUCCESS != status) {
            DEBUG_ERR("error calling apr_thread_mutex_create: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
            apr_pool_destroy(gc_pool);
            return status;
        }

        h_ctx.results = apr_array_make(gc_pool, (int) total_hash_tasks, sizeof(hashing_result_t *));
        status = apr_thread_mutex_create(&h_ctx.results_mutex, APR_THREAD_MUTEX_DEFAULT, gc_pool);
        if (APR_SUCCESS != status) {
            DEBUG_ERR("error calling apr_thread_mutex_create for results_mutex: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
            apr_thread_mutex_destroy(h_ctx.stats_mutex);
            apr_pool_destroy(gc_pool);
            return status;
        }

        status = dispatch_hashing_tasks(conf, gc_pool, total_hash_tasks, &h_ctx);
        if (status != APR_SUCCESS) {
            apr_thread_mutex_destroy(h_ctx.results_mutex);
            apr_thread_mutex_destroy(h_ctx.stats_mutex);
            apr_pool_destroy(gc_pool);
            return status;
        }

        if (h_ctx.results && h_ctx.results->nelts > 0) {
            status = apr_thread_mutex_lock(h_ctx.results_mutex);
            if (status != APR_SUCCESS) {
                DEBUG_ERR("Error locking results mutex: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
            }
            else {
                apr_status_t update_status = update_cache_with_results(conf, &h_ctx);
                if (update_status != APR_SUCCESS) {
                    DEBUG_ERR("error calling update_cache_with_results: %s", apr_strerror(update_status, errbuf, ERR_BUF_SIZE));
                }

                status = apr_thread_mutex_unlock(h_ctx.results_mutex);
                if (status != APR_SUCCESS) {
                    DEBUG_ERR("Error unlocking results mutex: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
                }
            }
        }

        status = apr_thread_mutex_destroy(h_ctx.results_mutex);
        if (APR_SUCCESS != status) {
            DEBUG_ERR("error calling apr_thread_mutex_destroy for results_mutex: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        }

        status = collect_hashing_results(conf, tmp_heap, gc_pool);
        if (status != APR_SUCCESS) {
            apr_pool_destroy(gc_pool);
            return status;
        }
    }

    apr_pool_destroy(gc_pool);
    conf->heap = tmp_heap;

    return APR_SUCCESS;
}

static apr_status_t update_cache_with_results(ft_conf_t *conf, hashing_context_t *h_ctx)
{
    char errbuf[ERR_BUF_SIZE];
    apr_pool_t *update_pool = NULL;

    DEBUG_DBG("[FT_PROCESS] update_cache_with_results: Entry, results->nelts=%d", h_ctx->results ? h_ctx->results->nelts : 0);

    apr_status_t status = apr_pool_create(&update_pool, conf->pool);

    if (APR_SUCCESS != status) {
        DEBUG_ERR("error creating update pool: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    status = napr_cache_begin_write(conf->cache, update_pool);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("Failed to begin cache update transaction: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        apr_pool_destroy(update_pool);
        return status;
    }

    for (int i = 0; i < h_ctx->results->nelts; i++) {
        hashing_result_t *result = APR_ARRAY_IDX(h_ctx->results, i, hashing_result_t *);
        napr_cache_entry_t entry;
        entry.mtime = result->mtime;
        entry.ctime = result->ctime;
        entry.size = result->size;
        entry.hash = result->hash;
        status = napr_cache_upsert_in_txn(conf->cache, result->filename, &entry);
        if (APR_SUCCESS != status) {
            DEBUG_ERR("Failed to update cache for %s", result->filename);
            napr_cache_abort_write(conf->cache);
            apr_pool_destroy(update_pool);
            return status;
        }
    }

    DEBUG_DBG("[FT_PROCESS] About to commit cache updates, results->nelts=%d", h_ctx->results->nelts);
    status = napr_cache_commit_write(conf->cache);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("Failed to commit cache updates: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
    }
    DEBUG_DBG("[FT_PROCESS] Cache commit completed");

    apr_pool_destroy(update_pool);
    return status;
}

static apr_status_t categorize_files(ft_conf_t *conf, napr_heap_t *tmp_heap, apr_size_t *total_hash_tasks)
{
    ft_file_t *file = NULL;
    ft_fsize_t *fsize = NULL;
    apr_uint32_t hash_value = 0;

    while (NULL != (file = napr_heap_extract(conf->heap))) {
        fsize = napr_hash_search(conf->sizes, &file->size, sizeof(apr_off_t), &hash_value);
        if (NULL != fsize) {
            if (1 == fsize->nb_files) {
                napr_hash_remove(conf->sizes, fsize, hash_value);
            }
            else {
                if (NULL == fsize->chksum_array) {
                    fsize->chksum_array = apr_palloc(conf->pool, fsize->nb_files * sizeof(struct ft_chksum_t));
                }

                fsize->chksum_array[fsize->nb_checksumed].file = file;

                if (file->is_cache_hit) {
                    /* Cache hit: use cached hash and insert into result heap */
                    fsize->chksum_array[fsize->nb_checksumed].hash_value = file->cached_hash;
                    fsize->nb_checksumed++;
                    napr_heap_insert(tmp_heap, file);
                }
                else {
                    /* Cache miss: proceed with existing logic */
                    if (((2 == fsize->nb_files) || (0 == fsize->val)) && !is_option_set(conf->mask, OPTION_JSON)) {
                        memset(&fsize->chksum_array[fsize->nb_checksumed].hash_value, 0, sizeof(ft_hash_t));
                        fsize->nb_checksumed++;
                        napr_heap_insert(tmp_heap, file);
                    }
                    else {
                        (*total_hash_tasks)++;
                        fsize->nb_checksumed++;
                    }
                }
            }
        }
        else {
            DEBUG_ERR("inconsistency error found, no size[%" APR_OFF_T_FMT "] in hash for file %s", file->size, file->path);
            return APR_EGENERAL;
        }
    }
    return APR_SUCCESS;
}

static apr_status_t dispatch_hashing_tasks(ft_conf_t *conf, apr_pool_t *gc_pool, apr_size_t total_hash_tasks, hashing_context_t *h_ctx)
{
    char errbuf[ERR_BUF_SIZE];
    ft_fsize_t *fsize = NULL;
    napr_threadpool_t *threadpool = NULL;
    apr_status_t status = 0;

    status = napr_threadpool_init(&threadpool, h_ctx, conf->num_threads, hashing_worker_callback, gc_pool);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("error calling napr_threadpool_init: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
        return status;
    }

    for (napr_hash_index_t *hash_index = napr_hash_first(gc_pool, conf->sizes); hash_index; hash_index = napr_hash_next(hash_index)) {
        napr_hash_this(hash_index, NULL, NULL, (void **) &fsize);

        int should_hash = (fsize->nb_files > 2) && (0 != fsize->val);
        if (is_option_set(conf->mask, OPTION_JSON)) {
            should_hash = (fsize->nb_files >= 2);
        }

        if (should_hash) {
            for (apr_uint32_t idx = 0; idx < fsize->nb_files; idx++) {
                if (NULL != fsize->chksum_array[idx].file) {
                    hashing_task_t *task = apr_palloc(conf->pool, sizeof(hashing_task_t));
                    task->fsize = fsize;
                    task->index = idx;

                    status = napr_threadpool_add(threadpool, task);
                    if (APR_SUCCESS != status) {
                        DEBUG_ERR("error calling napr_threadpool_add: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
                        napr_threadpool_wait(threadpool);
                        return status;
                    }
                }
            }
        }
    }

    napr_threadpool_wait(threadpool);

    status = apr_thread_mutex_destroy(h_ctx->stats_mutex);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("error calling apr_thread_mutex_destroy: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
    }

    if (is_option_set(conf->mask, OPTION_VERBO)) {
        (void) fprintf(stderr, "\n");
    }

    status = napr_threadpool_shutdown(threadpool);
    if (APR_SUCCESS != status) {
        DEBUG_ERR("error calling napr_threadpool_shutdown: %s", apr_strerror(status, errbuf, ERR_BUF_SIZE));
    }

    return APR_SUCCESS;
}

static apr_status_t collect_hashing_results(ft_conf_t *conf, napr_heap_t *tmp_heap, apr_pool_t *gc_pool)
{
    ft_fsize_t *fsize = NULL;

    for (napr_hash_index_t *hash_index = napr_hash_first(gc_pool, conf->sizes); hash_index; hash_index = napr_hash_next(hash_index)) {
        napr_hash_this(hash_index, NULL, NULL, (void **) &fsize);

        if (fsize->chksum_array != NULL) {
            for (apr_uint32_t idx = 0; idx < fsize->nb_checksumed; idx++) {
                /* Only insert files that were cache misses (cache hits were already inserted in categorize_files) */
                if (fsize->chksum_array[idx].file != NULL && !fsize->chksum_array[idx].file->is_cache_hit) {
                    napr_heap_insert(tmp_heap, fsize->chksum_array[idx].file);
                }
            }
        }
    }

    return APR_SUCCESS;
}
