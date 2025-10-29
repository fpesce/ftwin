/**
 * @file test_task_corruption.c
 * @brief Minimal test to reproduce task index corruption bug
 *
 * This test attempts to reproduce the memory corruption where task->index
 * gets overwritten with string data (e.g., "ltd6") during parallel hashing
 * operations with many files.
 *
 * Compile:
 *   gcc -g -o test_task_corruption test_task_corruption.c \
 *       -I../src $(apr-1-config --cflags --includes --libs) \
 *       -lapr-1 -laprutil-1 -lpthread
 *
 * Run with valgrind:
 *   valgrind --leak-check=full --track-origins=yes ./test_task_corruption
 */

#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <apr_thread_mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Simulate the structures from ft_types.h */

typedef struct
{
    uint64_t low64;
    uint64_t high64;
} ft_hash_t;

typedef struct ft_file_t
{
    char *path;
    int64_t size;
    int64_t mtime;
    int64_t ctime;
    int prioritized;
    int cvec_ok;
    ft_hash_t cached_hash;
    int is_cache_hit;
} ft_file_t;

typedef struct ft_chksum_t
{
    ft_hash_t hash_value;
    ft_file_t *file;
} ft_chksum_t;

typedef struct ft_fsize_t
{
    int64_t val;
    ft_chksum_t *chksum_array;
    uint32_t nb_checksumed;
    uint32_t nb_files;
} ft_fsize_t;

typedef struct hashing_task_t
{
    ft_fsize_t *fsize;
    uint32_t index;
} hashing_task_t;

typedef struct
{
    apr_pool_t *pool;
    apr_thread_mutex_t *mutex;
    ft_fsize_t *fsize;
    int num_tasks;
    volatile int corruption_detected;
} test_context_t;

/* Validation magic number to detect corruption */
#define TASK_MAGIC 0xDEADBEEF

typedef struct validated_task_t
{
    uint32_t magic_before;
    hashing_task_t task;
    uint32_t magic_after;
} validated_task_t;

static void *APR_THREAD_FUNC worker_thread(apr_thread_t *thread, void *data)
{
    validated_task_t *vtask = (validated_task_t *) data;
    test_context_t *ctx = vtask->task.fsize ? NULL : NULL;      /* Simulate accessing fsize */

    /* Simulate some work */
    for (volatile int i = 0; i < 1000; i++) {
        /* busy wait */
    }

    /* Validate task integrity */
    if (vtask->magic_before != TASK_MAGIC || vtask->magic_after != TASK_MAGIC) {
        fprintf(stderr, "ERROR: Magic number corruption detected!\n");
        fprintf(stderr, "  magic_before = 0x%08x (expected 0x%08x)\n", vtask->magic_before, TASK_MAGIC);
        fprintf(stderr, "  magic_after  = 0x%08x (expected 0x%08x)\n", vtask->magic_after, TASK_MAGIC);
        return NULL;
    }

    /* Check for string data in index field */
    if (vtask->task.index > 100000) {
        fprintf(stderr, "ERROR: Task index corruption detected!\n");
        fprintf(stderr, "  task->index = %u (0x%08x)\n", vtask->task.index, vtask->task.index);

        /* Try to interpret as ASCII */
        char ascii[5] = { 0 };
        memcpy(ascii, &vtask->task.index, 4);
        fprintf(stderr, "  As ASCII: '%c%c%c%c'\n", ascii[0], ascii[1], ascii[2], ascii[3]);

        return NULL;
    }

    /* Simulate accessing the file through the task */
    ft_fsize_t *fsize = vtask->task.fsize;
    uint32_t idx = vtask->task.index;

    if (!fsize || idx >= fsize->nb_files) {
        fprintf(stderr, "ERROR: Invalid task access: fsize=%p, index=%u, nb_files=%u\n", (void *) fsize, idx, fsize ? fsize->nb_files : 0);
        return NULL;
    }

    ft_file_t *file = fsize->chksum_array[idx].file;
    if (!file) {
        fprintf(stderr, "ERROR: NULL file pointer at index %u\n", idx);
        return NULL;
    }

    /* Success - task was valid */
    return (void *) 1;
}

int main(int argc, char **argv)
{
    apr_status_t status;
    apr_pool_t *root_pool, *conf_pool, *gc_pool;
    test_context_t ctx = { 0 };
    const int NUM_FILES = 10000;
    const int NUM_THREADS = 24;

    printf("Task Corruption Test\n");
    printf("=====================\n\n");
    printf("Creating %d files with %d worker threads...\n", NUM_FILES, NUM_THREADS);

    /* Initialize APR */
    status = apr_initialize();
    if (status != APR_SUCCESS) {
        fprintf(stderr, "Failed to initialize APR\n");
        return 1;
    }
    atexit(apr_terminate);

    /* Create pools mimicking ftwin's structure */
    status = apr_pool_create(&root_pool, NULL);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "Failed to create root pool\n");
        return 1;
    }

    status = apr_pool_create(&conf_pool, root_pool);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "Failed to create conf pool\n");
        return 1;
    }

    status = apr_pool_create(&gc_pool, conf_pool);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "Failed to create gc pool\n");
        return 1;
    }

    /* Create fsize structure */
    ft_fsize_t *fsize = apr_pcalloc(conf_pool, sizeof(ft_fsize_t));
    fsize->val = 1024;
    fsize->nb_files = NUM_FILES;
    fsize->nb_checksumed = NUM_FILES;
    fsize->chksum_array = apr_pcalloc(conf_pool, NUM_FILES * sizeof(ft_chksum_t));

    /* Create fake files with realistic paths */
    printf("Allocating %d file structures...\n", NUM_FILES);
    for (int i = 0; i < NUM_FILES; i++) {
        ft_file_t *file = apr_pcalloc(conf_pool, sizeof(ft_file_t));

        /* Create long paths similar to Go modules */
        file->path = apr_psprintf(conf_pool, "/home/ubuntu/go/pkg/mod/github.com/yuin/goldmark@v1.7.8/" "util/html5entities_ltdot_ltimes_ltri_test_%05d.go", i);

        file->size = 1024;
        file->mtime = 1234567890;
        file->ctime = 1234567890;
        file->is_cache_hit = 0;

        fsize->chksum_array[i].file = file;
    }

    /* Create tasks from gc_pool */
    printf("Allocating %d task structures from gc_pool...\n", NUM_FILES);
    validated_task_t **tasks = apr_palloc(gc_pool, NUM_FILES * sizeof(validated_task_t *));

    for (int i = 0; i < NUM_FILES; i++) {
        validated_task_t *vtask = apr_palloc(gc_pool, sizeof(validated_task_t));

        vtask->magic_before = TASK_MAGIC;
        vtask->task.fsize = fsize;
        vtask->task.index = i;
        vtask->magic_after = TASK_MAGIC;

        tasks[i] = vtask;
    }

    /* Launch worker threads */
    printf("Launching %d worker threads...\n", NUM_THREADS);
    apr_thread_t **threads = apr_palloc(gc_pool, NUM_THREADS * sizeof(apr_thread_t *));
    apr_threadattr_t *attr;

    status = apr_threadattr_create(&attr, gc_pool);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "Failed to create thread attributes\n");
        return 1;
    }

    /* Launch threads processing tasks in batches */
    int tasks_per_thread = NUM_FILES / NUM_THREADS;
    for (int t = 0; t < NUM_THREADS; t++) {
        int start_idx = t * tasks_per_thread;
        int end_idx = (t == NUM_THREADS - 1) ? NUM_FILES : start_idx + tasks_per_thread;

        /* Pass first task of this batch to thread */
        status = apr_thread_create(&threads[t], attr, worker_thread, tasks[start_idx], gc_pool);
        if (status != APR_SUCCESS) {
            fprintf(stderr, "Failed to create thread %d\n", t);
            return 1;
        }
    }

    /* Wait for all threads */
    printf("Waiting for threads to complete...\n");
    int failures = 0;
    for (int t = 0; t < NUM_THREADS; t++) {
        apr_status_t retval;
        void *thread_result;
        status = apr_thread_join(&retval, threads[t]);
        if (status != APR_SUCCESS) {
            fprintf(stderr, "Failed to join thread %d\n", t);
            failures++;
        }
    }

    /* Validate all tasks after thread completion */
    printf("\nValidating all %d tasks for corruption...\n", NUM_FILES);
    int corrupted = 0;
    for (int i = 0; i < NUM_FILES; i++) {
        validated_task_t *vtask = tasks[i];

        if (vtask->magic_before != TASK_MAGIC || vtask->magic_after != TASK_MAGIC) {
            fprintf(stderr, "Task %d: Magic number corruption\n", i);
            corrupted++;
        }
        else if (vtask->task.index != (uint32_t) i) {
            fprintf(stderr, "Task %d: Index corruption (expected %d, got %u)\n", i, i, vtask->task.index);

            /* Check if it looks like string data */
            if (vtask->task.index > 0x20202020 && vtask->task.index < 0x7F7F7F7F) {
                char ascii[5] = { 0 };
                memcpy(ascii, &vtask->task.index, 4);
                fprintf(stderr, "  Looks like ASCII: '%c%c%c%c'\n", ascii[0], ascii[1], ascii[2], ascii[3]);
            }
            corrupted++;
        }
    }

    /* Cleanup */
    apr_pool_destroy(gc_pool);
    apr_pool_destroy(conf_pool);
    apr_pool_destroy(root_pool);

    /* Report results */
    printf("\n");
    printf("Test Results:\n");
    printf("=============\n");
    printf("Total files:      %d\n", NUM_FILES);
    printf("Worker threads:   %d\n", NUM_THREADS);
    printf("Thread failures:  %d\n", failures);
    printf("Corrupted tasks:  %d\n", corrupted);

    if (corrupted > 0 || failures > 0) {
        printf("\n*** CORRUPTION DETECTED ***\n");
        return 1;
    }
    else {
        printf("\n*** All tasks validated successfully ***\n");
        return 0;
    }
}
