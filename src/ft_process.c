#include <apr_thread_mutex.h>

#include "debug.h"
#include "ft_config.h"
#include "ft_process.h"
#include "ft_system.h"
#include "ft_types.h"
#include "napr_threadpool.h"
#include "napr_hash.h"
#include <apr_file_io.h>
#include "ft_file.h"

int ft_file_cmp(const void *param1, const void *param2);

#if HAVE_ARCHIVE
#include <archive.h>
#include "ft_archive.h"		/* This will be created later */
#endif

static apr_status_t hashing_worker_callback(void *ctx, void *data)
{
    char errbuf[ERROR_BUFFER_SIZE];
    hashing_context_t *h_ctx = (hashing_context_t *) ctx;
    hashing_task_t *task = (hashing_task_t *) data;
    ft_fsize_t *fsize = task->fsize;
    ft_file_t *file = fsize->chksum_array[task->index].file;
    apr_pool_t *subpool = NULL;
    apr_status_t status = APR_SUCCESS;
    char *filepath = NULL;

    memset(errbuf, 0, sizeof(errbuf));
    status = apr_pool_create(&subpool, h_ctx->pool);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	return status;
    }

#if HAVE_ARCHIVE
    if (is_option_set(h_ctx->conf->mask, OPTION_UNTAR) && (NULL != file->subpath)) {
	filepath = ft_archive_untar_file(file, subpool);
	if (NULL == filepath) {
	    DEBUG_ERR("error calling ft_archive_untar_file");
	    apr_pool_destroy(subpool);
	    return APR_EGENERAL;
	}
    }
    else {
	filepath = file->path;
    }
#else
    filepath = file->path;
#endif

    status = checksum_file(filepath, file->size, h_ctx->conf->excess_size,
			   &fsize->chksum_array[task->index].hash_value, subpool);

#if HAVE_ARCHIVE
    if (is_option_set(h_ctx->conf->mask, OPTION_UNTAR) && (NULL != file->subpath)) {
	apr_file_remove(filepath, subpool);
    }
#endif

    if (APR_SUCCESS == status) {
	apr_status_t lock_status = APR_SUCCESS;

	lock_status = apr_thread_mutex_lock(h_ctx->stats_mutex);
	if (APR_SUCCESS == lock_status) {
	    h_ctx->files_processed++;

	    if (is_option_set(h_ctx->conf->mask, OPTION_VERBO)) {
		(void) fprintf(stderr, "\rProgress [%" APR_SIZE_T_FMT "/%" APR_SIZE_T_FMT "] %d%% ",
			       h_ctx->files_processed, h_ctx->total_files,
			       (int) ((float) h_ctx->files_processed / (float) h_ctx->total_files * 100.0f));
	    }

	    apr_thread_mutex_unlock(h_ctx->stats_mutex);
	}
    }
    else {
	if (is_option_set(h_ctx->conf->mask, OPTION_VERBO)) {
	    (void) fprintf(stderr, "\nskipping %s because: %s\n", file->path, apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	}
    }

    apr_pool_destroy(subpool);

    return status;
}

/* Forward declarations for helper functions */
static apr_status_t categorize_files(ft_conf_t *conf, napr_heap_t *tmp_heap, apr_size_t *total_hash_tasks);
static apr_status_t dispatch_hashing_tasks(ft_conf_t *conf, apr_pool_t *gc_pool, apr_size_t total_hash_tasks);
static apr_status_t collect_hashing_results(ft_conf_t *conf, napr_heap_t *tmp_heap, apr_pool_t *gc_pool);

apr_status_t ft_process_files(ft_conf_t *conf)
{
    napr_heap_t *tmp_heap = NULL;
    apr_pool_t *gc_pool = NULL;
    apr_status_t status = APR_SUCCESS;
    apr_size_t total_hash_tasks = 0;

    if (is_option_set(conf->mask, OPTION_VERBO)) {
	(void) fprintf(stderr, "Referencing files and sizes:\n");
    }

    if (APR_SUCCESS != (status = apr_pool_create(&gc_pool, conf->pool))) {
	char errbuf[ERROR_BUFFER_SIZE];
	DEBUG_ERR("error calling apr_pool_create: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	return status;
    }

    tmp_heap = napr_heap_make(conf->pool, ft_file_cmp);

    status = categorize_files(conf, tmp_heap, &total_hash_tasks);
    if (status != APR_SUCCESS) {
	apr_pool_destroy(gc_pool);
	return status;
    }

    if (total_hash_tasks > 0) {
	status = dispatch_hashing_tasks(conf, gc_pool, total_hash_tasks);
	if (status != APR_SUCCESS) {
	    apr_pool_destroy(gc_pool);
	    return status;
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

static apr_status_t categorize_files(ft_conf_t *conf, napr_heap_t *tmp_heap, apr_size_t *total_hash_tasks)
{
    ft_file_t *file;
    ft_fsize_t *fsize;
    apr_uint32_t hash_value;

    while (NULL != (file = napr_heap_extract(conf->heap))) {
	if (NULL != (fsize = napr_hash_search(conf->sizes, &file->size, sizeof(apr_off_t), &hash_value))) {
	    if (1 == fsize->nb_files) {
		napr_hash_remove(conf->sizes, fsize, hash_value);
	    }
	    else {
		if (NULL == fsize->chksum_array)
		    fsize->chksum_array = apr_palloc(conf->pool, fsize->nb_files * sizeof(struct ft_chksum_t));

		fsize->chksum_array[fsize->nb_checksumed].file = file;

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
	else {
	    DEBUG_ERR("inconsistency error found, no size[%" APR_OFF_T_FMT "] in hash for file %s", file->size, file->path);
	    return APR_EGENERAL;
	}
    }
    return APR_SUCCESS;
}

static apr_status_t dispatch_hashing_tasks(ft_conf_t *conf, apr_pool_t *gc_pool, apr_size_t total_hash_tasks)
{
    char errbuf[ERROR_BUFFER_SIZE];
    ft_fsize_t *fsize;
    napr_threadpool_t *threadpool = NULL;
    hashing_context_t h_ctx;
    apr_status_t status;

    h_ctx.conf = conf;
    h_ctx.pool = gc_pool;
    h_ctx.files_processed = 0;
    h_ctx.total_files = total_hash_tasks;

    status = apr_thread_mutex_create(&h_ctx.stats_mutex, APR_THREAD_MUTEX_DEFAULT, gc_pool);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_create: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	return status;
    }

    status = napr_threadpool_init(&threadpool, &h_ctx, conf->num_threads, hashing_worker_callback, gc_pool);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling napr_threadpool_init: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
	apr_thread_mutex_destroy(h_ctx.stats_mutex);
	return status;
    }

    for (napr_hash_index_t *hash_index = napr_hash_first(gc_pool, conf->sizes); hash_index;
	 hash_index = napr_hash_next(hash_index)) {
	napr_hash_this(hash_index, NULL, NULL, (void **) &fsize);

	int should_hash = (fsize->nb_files > 2) && (0 != fsize->val);
	if (is_option_set(conf->mask, OPTION_JSON)) {
	    should_hash = (fsize->nb_files >= 2);
	}

	if (should_hash) {
	    for (apr_uint32_t idx = 0; idx < fsize->nb_files; idx++) {
		if (NULL != fsize->chksum_array[idx].file) {
		    hashing_task_t *task = apr_palloc(gc_pool, sizeof(hashing_task_t));
		    task->fsize = fsize;
		    task->index = idx;

		    status = napr_threadpool_add(threadpool, task);
		    if (APR_SUCCESS != status) {
			DEBUG_ERR("error calling napr_threadpool_add: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
			napr_threadpool_wait(threadpool);
			apr_thread_mutex_destroy(h_ctx.stats_mutex);
			return status;
		    }
		}
	    }
	}
    }

    napr_threadpool_wait(threadpool);

    status = apr_thread_mutex_destroy(h_ctx.stats_mutex);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling apr_thread_mutex_destroy: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
    }

    if (is_option_set(conf->mask, OPTION_VERBO)) {
	(void) fprintf(stderr, "\n");
    }

    status = napr_threadpool_shutdown(threadpool);
    if (APR_SUCCESS != status) {
	DEBUG_ERR("error calling napr_threadpool_shutdown: %s", apr_strerror(status, errbuf, ERROR_BUFFER_SIZE));
    }

    return APR_SUCCESS;
}

static apr_status_t collect_hashing_results(ft_conf_t *conf, napr_heap_t *tmp_heap, apr_pool_t *gc_pool)
{
    ft_fsize_t *fsize;

    for (napr_hash_index_t *hash_index = napr_hash_first(gc_pool, conf->sizes); hash_index;
	 hash_index = napr_hash_next(hash_index)) {
	napr_hash_this(hash_index, NULL, NULL, (void **) &fsize);

	int should_insert = (fsize->nb_files > 2) && (0 != fsize->val);
	if (is_option_set(conf->mask, OPTION_JSON)) {
	    should_insert = (fsize->nb_files >= 2);
	}

	if (should_insert) {
	    for (apr_uint32_t idx = 0; idx < fsize->nb_files; idx++) {
		if (NULL != fsize->chksum_array[idx].file) {
		    napr_heap_insert(tmp_heap, fsize->chksum_array[idx].file);
		}
	    }
	}
    }

    return APR_SUCCESS;
}
