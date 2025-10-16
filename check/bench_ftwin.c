#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <apr_general.h>
#include <apr_pools.h>
#include <apr_time.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include "checksum.h"
#include "ft_file.h"
#include "ft_config.h"

#ifdef FTWIN_TEST_BUILD
#include "ftwin.h"
#endif

static const size_t BUFFER_SIZE = 1024L * 1024L;        // 1MB
static const size_t FILE_SIZE = 10L * 1024L * 1024L;    // 10MB
static const size_t EXCESS_SIZE = 16L * 1024L * 1024L;  // 16MB, larger than file to test small file path
static const int ITERATIONS = 100;
static const size_t BENCH_FILE_SIZE = 50L * 1024L * 1024L;      // 50MB for parallel benchmarks
static const int NUM_BENCH_FILES = 12;  // Number of files for parallel benchmark

static void run_hash_benchmark(apr_pool_t *pool);
static void run_checksum_file_benchmark(apr_pool_t *pool);

#ifdef FTWIN_TEST_BUILD
static void run_parallel_hashing_benchmark(apr_pool_t *pool);
static void create_bench_files(apr_pool_t *pool, const char *directory_path, int number_of_files, size_t size_of_each_file);
static void cleanup_bench_files(const char *dir, apr_pool_t *pool);
#endif

int main(int argc, const char *const *argv)
{
    apr_pool_t *pool = NULL;

    (void) argc;
    (void) argv;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    (void) printf("[\n");

    run_hash_benchmark(pool);
    (void) printf(",\n");
    run_checksum_file_benchmark(pool);

#ifdef FTWIN_TEST_BUILD
    (void) printf(",\n");
    run_parallel_hashing_benchmark(pool);
#endif

    (void) printf("\n]\n");

    apr_pool_destroy(pool);
    apr_terminate();

    return 0;
}

static void run_hash_benchmark(apr_pool_t *pool)
{
    unsigned char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        (void) fprintf(stderr, "Failed to allocate buffer for hash benchmark.\n");
        return;
    }

    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
        buffer[i] = (unsigned char) (i % CHAR_MAX_VAL);
    }

    apr_time_t start_time = apr_time_now();

    volatile ft_hash_t hash_result;
    for (int i = 0; i < ITERATIONS; ++i) {
        hash_result = XXH3_128bits(buffer, BUFFER_SIZE);
    }
    (void) hash_result;         // Suppress unused variable warning while preventing optimization

    apr_time_t end_time = apr_time_now();
    apr_time_t total_time = end_time - start_time;
    double throughput_mb_s = (double) (BUFFER_SIZE * ITERATIONS) / (double) total_time * (double) MICROSECONDS_PER_SECOND / (double) (KIBIBYTE * KIBIBYTE);

    (void) printf("  {\n");
    (void) printf("    \"name\": \"hash_throughput\",\n");
    (void) printf("    \"unit\": \"MB/s\",\n");
    (void) printf("    \"value\": %.2f\n", throughput_mb_s);
    (void) printf("  }");

    free(buffer);
}

static void run_checksum_file_benchmark(apr_pool_t *pool)
{
    apr_file_t *file = NULL;
    const char *filename = NULL;
    char template[] = "bench_ftwin.XXXXXX";
    apr_status_t status = apr_file_mktemp(&file, template, APR_CREATE | APR_READ | APR_WRITE | APR_TRUNCATE | APR_BINARY, pool);

    if (status != APR_SUCCESS) {
        (void) fprintf(stderr, "Failed to create temp file for checksum benchmark.\n");
        return;
    }

    apr_file_name_get(&filename, file);

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        (void) fprintf(stderr, "Failed to allocate buffer for file writing.\n");
        (void) apr_file_close(file);
        return;
    }
    for (size_t i = 0; i < FILE_SIZE / BUFFER_SIZE; ++i) {
        apr_size_t bytes_written = BUFFER_SIZE;
        apr_file_write(file, buffer, &bytes_written);
    }
    free(buffer);
    (void) apr_file_close(file);

    ft_hash_t hash_result;
    memset(&hash_result, 0, sizeof(hash_result));

    apr_time_t start_time = apr_time_now();

    for (int i = 0; i < ITERATIONS; ++i) {
        checksum_file(filename, (apr_off_t) FILE_SIZE, (apr_off_t) EXCESS_SIZE, &hash_result, pool);
    }

    apr_time_t end_time = apr_time_now();
    apr_time_t total_time = end_time - start_time;
    double throughput_mb_s = (double) (FILE_SIZE * ITERATIONS) / (double) total_time * (double) MICROSECONDS_PER_SECOND / (double) (KIBIBYTE * KIBIBYTE);

    (void) printf("  {\n");
    (void) printf("    \"name\": \"checksum_file_throughput\",\n");
    (void) printf("    \"unit\": \"MB/s\",\n");
    (void) printf("    \"value\": %.2f\n", throughput_mb_s);
    (void) printf("  }");

    // Clean up the temporary file
    (void) apr_file_remove(filename, pool);
}

#ifdef FTWIN_TEST_BUILD

static apr_status_t recursive_delete(const char *path, apr_pool_t *pool)
{
    apr_dir_t *dir = NULL;
    apr_finfo_t finfo;
    apr_status_t status = apr_dir_open(&dir, path, pool);

    if (status != APR_SUCCESS) {
        return status;
    }

    while (apr_dir_read(&finfo, APR_FINFO_DIRENT | APR_FINFO_TYPE, dir) == APR_SUCCESS) {
        if (strcmp(finfo.name, ".") == 0 || strcmp(finfo.name, "..") == 0) {
            continue;
        }

        char *new_path = apr_pstrcat(pool, path, "/", finfo.name, NULL);
        if (finfo.filetype == APR_DIR) {
            status = recursive_delete(new_path, pool);
            if (status != APR_SUCCESS) {
                (void) apr_dir_close(dir);
                return status;
            }
        }
        else {
            status = apr_file_remove(new_path, pool);
            if (status != APR_SUCCESS) {
                (void) apr_dir_close(dir);
                return status;
            }
        }
    }

    (void) apr_dir_close(dir);
    return apr_dir_remove(path, pool);
}

static void create_bench_files(apr_pool_t *pool, const char *directory_path, int number_of_files, size_t size_of_each_file)
{
    (void) apr_dir_make_recursive(directory_path, APR_OS_DEFAULT, pool);

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        (void) fprintf(stderr, "Failed to allocate buffer for file creation.\n");
        return;
    }

    /* Fill buffer with pattern */
    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
        buffer[i] = (char) (i % CHAR_MAX_VAL);
    }

    /* Create base files and duplicates */
    for (int i = 0; i < number_of_files / 3; i++) {
        char *filename = apr_pstrcat(pool, directory_path, "/base", apr_itoa(pool, i), ".dat", NULL);
        apr_file_t *file_handle = NULL;
        if (apr_file_open(&file_handle, filename, APR_CREATE | APR_WRITE | APR_BINARY, APR_OS_DEFAULT, pool) == APR_SUCCESS) {
            for (size_t j = 0; j < size_of_each_file / BUFFER_SIZE; j++) {
                apr_size_t bytes_to_write = BUFFER_SIZE;
                (void) apr_file_write(file_handle, buffer, &bytes_to_write);
            }
            (void) apr_file_close(file_handle);

            /* Create 2 duplicates of each base file */
            for (int k = 1; k <= 2; k++) {
                char *dupname = apr_pstrcat(pool, directory_path, "/dup", apr_itoa(pool, i), "_",
                                            apr_itoa(pool, k), ".dat", NULL);
                (void) apr_file_copy(filename, dupname, APR_OS_DEFAULT, pool);
            }
        }
    }

    free(buffer);
}

static void cleanup_bench_files(const char *dir, apr_pool_t *pool)
{
    (void) recursive_delete(dir, pool);
}

static void run_parallel_hashing_benchmark(apr_pool_t *pool)
{
    const char *bench_dir = "/tmp/ftwin_bench";
    const unsigned int thread_counts[] = { 1, 2, 4, 8, 12, 16, 24 };
    const int num_thread_configs = sizeof(thread_counts) / sizeof(unsigned int);

    (void) pool;

    (void) fflush(stdout);
    (void) fflush(stderr);
    (void) fprintf(stderr, "Creating benchmark files...\n");
    create_bench_files(pool, bench_dir, NUM_BENCH_FILES, BENCH_FILE_SIZE);

    for (int thread_idx = 0; thread_idx < num_thread_configs; thread_idx++) {
        unsigned int num_threads = thread_counts[thread_idx];

        (void) fflush(stdout);
        (void) fflush(stderr);
        int stdout_save = dup(STDOUT_FILENO);
        int stderr_save = dup(STDERR_FILENO);
        int dev_null_fd = open("/dev/null", O_WRONLY);
        dup2(dev_null_fd, STDOUT_FILENO);
        (void) dup2(dev_null_fd, STDERR_FILENO);
        apr_time_t start_time = apr_time_now();
        char threads_str[DEFAULT_SMALL_BUFFER_SIZE];
        (void) snprintf(threads_str, sizeof(threads_str), "%u", num_threads);
        const char *argv[] = { "ftwin", "-j", threads_str, bench_dir };
        (void) ftwin_main(4, argv);
        apr_time_t end_time = apr_time_now();
        (void) dup2(stdout_save, STDOUT_FILENO);
        (void) dup2(stderr_save, STDERR_FILENO);
        (void) close(dev_null_fd);
        (void) close(stdout_save);
        (void) close(stderr_save);

        apr_time_t total_time = end_time - start_time;
        double time_seconds = (double) total_time / (double) MICROSECONDS_PER_SECOND;
        double total_mb = (double) (NUM_BENCH_FILES * BENCH_FILE_SIZE) / (double) (KIBIBYTE * KIBIBYTE);
        double throughput_mb_s = total_mb / time_seconds;

        if (thread_idx > 0) {
            (void) printf(",\n");
        }
        (void) printf("  {\n");
        (void) printf("    \"name\": \"parallel_hashing (%u threads)\",\n", num_threads);
        (void) printf("    \"unit\": \"MB/s\",\n");
        (void) printf("    \"value\": %.2f,\n", throughput_mb_s);
        (void) printf("    \"extra\": \"time_seconds=%.3f\"\n", time_seconds);
        (void) printf("  }");

        (void) fflush(stdout);
    }

    cleanup_bench_files(bench_dir, pool);
    (void) fprintf(stderr, "Benchmark complete.\n");
}
#endif
