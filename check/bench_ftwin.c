#include <stdio.h>
#include <stdlib.h>
#include <apr_general.h>
#include <apr_pools.h>
#include <apr_time.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include "ftwin.h"
#include "ft_system.h"
#include "checksum.h"
#include "ft_file.h"

#define BUFFER_SIZE (1024 * 1024) // 1MB
#define FILE_SIZE (10 * 1024 * 1024) // 10MB
#define EXCESS_SIZE (16 * 1024 * 1024) // 16MB, larger than file to test small file path
#define ITERATIONS 10
#define NUM_FILES 100

static void run_hash_benchmark(apr_pool_t *pool);
static void run_checksum_file_benchmark(apr_pool_t *pool);
static void run_parallel_hashing_benchmark(apr_pool_t *pool, unsigned int num_threads);
static void create_dummy_files(apr_pool_t *pool, const char *dir, int num_files, apr_size_t file_size);
static void cleanup_dummy_files(apr_pool_t *pool, const char *dir, int num_files);

int main(int argc, const char *const *argv)
{
    apr_pool_t *pool;
    unsigned int num_cores = ft_get_cpu_cores();

    apr_initialize();
    apr_pool_create(&pool, NULL);

    printf("[\n");

    run_hash_benchmark(pool);
    printf(",\n");
    run_checksum_file_benchmark(pool);
    printf(",\n");
    run_parallel_hashing_benchmark(pool, 1);
    printf(",\n");
    run_parallel_hashing_benchmark(pool, num_cores);

    printf("\n]\n");

    apr_pool_destroy(pool);
    apr_terminate();

    return 0;
}

static void run_hash_benchmark(apr_pool_t *pool)
{
    unsigned char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
	fprintf(stderr, "Failed to allocate buffer for hash benchmark.\n");
	return;
    }

    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
	buffer[i] = (unsigned char) (i % 256);
    }

    apr_time_t start_time = apr_time_now();

    volatile ft_hash_t hash_result;
    for (int i = 0; i < ITERATIONS; ++i) {
	hash_result = XXH3_128bits(buffer, BUFFER_SIZE);
    }
    (void)hash_result;  // Suppress unused variable warning while preventing optimization

    apr_time_t end_time = apr_time_now();
    apr_time_t total_time = end_time - start_time;
    double throughput_mb_s = (double) (BUFFER_SIZE * ITERATIONS) / (double) total_time * 1000000.0 / (1024.0 * 1024.0);

    printf("  {\n");
    printf("    \"name\": \"hash_throughput\",\n");
    printf("    \"unit\": \"MB/s\",\n");
    printf("    \"value\": %.2f\n", throughput_mb_s);
    printf("  }");

    free(buffer);
}

static void run_checksum_file_benchmark(apr_pool_t *pool)
{
    apr_file_t *file;
    const char *filename;
    char template[] = "bench_ftwin.XXXXXX";
    apr_status_t status =
	apr_file_mktemp(&file, template, APR_CREATE | APR_READ | APR_WRITE | APR_TRUNCATE | APR_BINARY, pool);

    if (status != APR_SUCCESS) {
	fprintf(stderr, "Failed to create temp file for checksum benchmark.\n");
	return;
    }

    apr_file_name_get(&filename, file);

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
	fprintf(stderr, "Failed to allocate buffer for file writing.\n");
	apr_file_close(file);
	return;
    }
    for (size_t i = 0; i < FILE_SIZE / BUFFER_SIZE; ++i) {
	apr_size_t bytes_written = BUFFER_SIZE;
	apr_file_write(file, buffer, &bytes_written);
    }
    free(buffer);
    apr_file_close(file);

    ft_hash_t hash_result;

    apr_time_t start_time = apr_time_now();

    for (int i = 0; i < ITERATIONS; ++i) {
	checksum_file(filename, FILE_SIZE, EXCESS_SIZE, &hash_result, pool);
    }

    apr_time_t end_time = apr_time_now();
    apr_time_t total_time = end_time - start_time;
    double throughput_mb_s = (double) (FILE_SIZE * ITERATIONS) / (double) total_time * 1000000.0 / (1024.0 * 1024.0);

    printf("  {\n");
    printf("    \"name\": \"checksum_file_throughput\",\n");
    printf("    \"unit\": \"MB/s\",\n");
    printf("    \"value\": %.2f\n", throughput_mb_s);
    printf("  }");

    // Clean up the temporary file
    apr_file_remove(filename, pool);
}

static void run_parallel_hashing_benchmark(apr_pool_t *pool, unsigned int num_threads)
{
    const char *test_dir = "bench_parallel_hashing";
    char thread_count_str[16];
    apr_snprintf(thread_count_str, sizeof(thread_count_str), "%u", num_threads);

    create_dummy_files(pool, test_dir, NUM_FILES, FILE_SIZE);

    const char *argv[] = {
        "ftwin",
        "-j",
        thread_count_str,
        test_dir
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    apr_time_t start_time = apr_time_now();

    for (int i = 0; i < ITERATIONS; ++i) {
        ftwin_main(argc, (const char **)argv);
    }

    apr_time_t end_time = apr_time_now();
    apr_time_t total_time = end_time - start_time;
    double operations_per_second = (double)ITERATIONS / ((double)total_time / 1000000.0);

    printf("  {\n");
    printf("    \"name\": \"parallel_hashing_ops_per_sec (threads=%u)\",\n", num_threads);
    printf("    \"unit\": \"ops/s\",\n");
    printf("    \"value\": %.2f\n", operations_per_second);
    printf("  }");

    cleanup_dummy_files(pool, test_dir, NUM_FILES);
    apr_dir_remove(test_dir, pool);
}

static void create_dummy_files(apr_pool_t *pool, const char *dir, int num_files, apr_size_t file_size)
{
    apr_dir_make(dir, APR_OS_DEFAULT, pool);
    char *buffer = malloc(BUFFER_SIZE);
    memset(buffer, 1, BUFFER_SIZE);

    for (int i = 0; i < num_files; ++i) {
        char filepath[256];
        apr_snprintf(filepath, sizeof(filepath), "%s/file%d.dat", dir, i);

        apr_file_t *file;
        apr_file_open(&file, filepath, APR_CREATE | APR_WRITE, APR_OS_DEFAULT, pool);
        for (apr_size_t j = 0; j < file_size / BUFFER_SIZE; ++j) {
            apr_size_t bytes_written = BUFFER_SIZE;
            apr_file_write(file, buffer, &bytes_written);
        }
        apr_file_close(file);

        // Create a duplicate
        char dupe_filepath[256];
        apr_snprintf(dupe_filepath, sizeof(dupe_filepath), "%s/file%d_dupe.dat", dir, i);
        apr_file_copy(filepath, dupe_filepath, APR_OS_DEFAULT, pool);
    }
    free(buffer);
}

static void cleanup_dummy_files(apr_pool_t *pool, const char *dir, int num_files)
{
    for (int i = 0; i < num_files; ++i) {
        char filepath[256];
        apr_snprintf(filepath, sizeof(filepath), "%s/file%d.dat", dir, i);
        apr_file_remove(filepath, pool);

        char dupe_filepath[256];
        apr_snprintf(dupe_filepath, sizeof(dupe_filepath), "%s/file%d_dupe.dat", dir, i);
        apr_file_remove(dupe_filepath, pool);
    }
}
