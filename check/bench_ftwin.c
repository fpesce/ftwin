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
#include "checksum.h"
#include "ft_file.h"

#ifdef FTWIN_TEST_BUILD
#include "ftwin.h"
#endif

#define BUFFER_SIZE (1024 * 1024)	// 1MB
#define FILE_SIZE (10 * 1024 * 1024)	// 10MB
#define EXCESS_SIZE (16 * 1024 * 1024)	// 16MB, larger than file to test small file path
#define ITERATIONS 100
#define BENCH_FILE_SIZE (50 * 1024 * 1024)	// 50MB for parallel benchmarks
#define NUM_BENCH_FILES 12	// Number of files for parallel benchmark

static void run_hash_benchmark(apr_pool_t *pool);
static void run_checksum_file_benchmark(apr_pool_t *pool);

#ifdef FTWIN_TEST_BUILD
static void run_parallel_hashing_benchmark(apr_pool_t *pool);
static void create_bench_files(const char *dir, int num_files, size_t file_size);
static void cleanup_bench_files(const char *dir, int num_files);
#endif

int main(int argc, const char *const *argv)
{
    apr_pool_t *pool;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    printf("[\n");

    run_hash_benchmark(pool);
    printf(",\n");
    run_checksum_file_benchmark(pool);

#ifdef FTWIN_TEST_BUILD
    printf(",\n");
    run_parallel_hashing_benchmark(pool);
#endif

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
    (void) hash_result;		// Suppress unused variable warning while preventing optimization

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

#ifdef FTWIN_TEST_BUILD
static void create_bench_files(const char *dir, int num_files, size_t file_size)
{
    mkdir(dir, 0755);

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
	fprintf(stderr, "Failed to allocate buffer for file creation.\n");
	return;
    }

    /* Fill buffer with pattern */
    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
	buffer[i] = (char) (i % 256);
    }

    /* Create base files and duplicates */
    for (int i = 0; i < num_files / 3; i++) {
	char filename[256];
	snprintf(filename, sizeof(filename), "%s/base%d.dat", dir, i);

	FILE *f = fopen(filename, "wb");
	if (f) {
	    for (size_t j = 0; j < file_size / BUFFER_SIZE; j++) {
		fwrite(buffer, 1, BUFFER_SIZE, f);
	    }
	    fclose(f);

	    /* Create 2 duplicates of each base file */
	    for (int k = 1; k <= 2; k++) {
		char dupname[256];
		snprintf(dupname, sizeof(dupname), "%s/dup%d_%d.dat", dir, i, k);

		char cmd[1024];
		snprintf(cmd, sizeof(cmd), "cp %s %s", filename, dupname);
		system(cmd);
	    }
	}
    }

    free(buffer);
}

static void cleanup_bench_files(const char *dir, int num_files)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    system(cmd);
}

static void run_parallel_hashing_benchmark(apr_pool_t *pool)
{
    const char *bench_dir = "/tmp/ftwin_bench";
    const unsigned int thread_counts[] = { 1, 2, 4, 8, 12, 16, 24 };
    int num_thread_configs;

    num_thread_configs = sizeof(thread_counts) / sizeof(unsigned int);
    /* Ensure all previous output is flushed before we start */
    fflush(stdout);
    fflush(stderr);

    /* Create benchmark files */
    fprintf(stderr, "Creating benchmark files...\n");
    create_bench_files(bench_dir, NUM_BENCH_FILES, BENCH_FILE_SIZE);

    printf("  {\n");
    printf("    \"name\": \"parallel_hashing_scalability\",\n");
    printf("    \"unit\": \"MB/s\",\n");
    printf("    \"thread_results\": [\n");

    for (int t = 0; t < num_thread_configs; t++) {
	unsigned int num_threads = thread_counts[t];

	/* Flush stdout/stderr before redirecting to avoid losing buffered data */
	fflush(stdout);
	fflush(stderr);

	/* Redirect output to /dev/null */
	int stdout_save = dup(STDOUT_FILENO);
	int stderr_save = dup(STDERR_FILENO);
	int devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, STDOUT_FILENO);
	dup2(devnull, STDERR_FILENO);

	apr_time_t start_time = apr_time_now();

	/* Run ftwin with specified thread count */
	char threads_str[16];
	snprintf(threads_str, sizeof(threads_str), "%u", num_threads);
	const char *argv[] = { "ftwin", "-j", threads_str, bench_dir };
	ftwin_main(4, argv);

	apr_time_t end_time = apr_time_now();

	/* Restore output */
	dup2(stdout_save, STDOUT_FILENO);
	dup2(stderr_save, STDERR_FILENO);
	close(devnull);
	close(stdout_save);
	close(stderr_save);

	apr_time_t total_time = end_time - start_time;
	double time_seconds = (double) total_time / 1000000.0;
	double total_mb = (double) (NUM_BENCH_FILES * BENCH_FILE_SIZE) / (1024.0 * 1024.0);
	double throughput_mb_s = total_mb / time_seconds;

	printf("      {\n");
	printf("        \"threads\": %u,\n", num_threads);
	printf("        \"throughput\": %.2f,\n", throughput_mb_s);
	printf("        \"time_seconds\": %.3f\n", time_seconds);
	printf("      }");

	if (t < num_thread_configs - 1) {
	    printf(",");
	}
	printf("\n");
	fflush(stdout);
    }

    printf("    ]\n");
    printf("  }");

    /* Cleanup */
    cleanup_bench_files(bench_dir, NUM_BENCH_FILES);
    fprintf(stderr, "Benchmark complete.\n");
}
#endif
