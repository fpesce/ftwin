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

static const size_t BUFFER_SIZE = 1024 * 1024;	// 1MB
static const size_t FILE_SIZE = 10 * 1024 * 1024;	// 10MB
static const size_t EXCESS_SIZE = 16 * 1024 * 1024;	// 16MB, larger than file to test small file path
static const int ITERATIONS = 100;
static const size_t BENCH_FILE_SIZE = 50 * 1024 * 1024;	// 50MB for parallel benchmarks
static const int NUM_BENCH_FILES = 12;	// Number of files for parallel benchmark
static const int MKDIR_MODE = 0755;

static void run_hash_benchmark(apr_pool_t *pool);
static void run_checksum_file_benchmark(apr_pool_t *pool);

#ifdef FTWIN_TEST_BUILD
static void run_parallel_hashing_benchmark(apr_pool_t *pool);
static void create_bench_files(const char *dir, int num_files, size_t file_size);
static void cleanup_bench_files(const char *dir);
#endif

int main(int argc, const char *const *argv)
{
    apr_pool_t *pool = NULL;

    (void) argc;
    (void) argv;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    (void)printf("[\n");

    run_hash_benchmark(pool);
    (void)printf(",\n");
    run_checksum_file_benchmark(pool);

#ifdef FTWIN_TEST_BUILD
    (void)printf(",\n");
    run_parallel_hashing_benchmark(pool);
#endif

    (void)printf("\n]\n");

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

    (void)printf("  {\n");
    (void)printf("    \"name\": \"hash_throughput\",\n");
    (void)printf("    \"unit\": \"MB/s\",\n");
    (void)printf("    \"value\": %.2f\n", throughput_mb_s);
    (void)printf("  }");

    free(buffer);
}

static void run_checksum_file_benchmark(apr_pool_t *pool)
{
    apr_file_t *file = NULL;
    const char *filename = NULL;
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
	checksum_file(filename, FILE_SIZE, EXCESS_SIZE, &hash_result, pool);
    }

    apr_time_t end_time = apr_time_now();
    apr_time_t total_time = end_time - start_time;
    double throughput_mb_s = (double) (FILE_SIZE * ITERATIONS) / (double) total_time * 1000000.0 / (1024.0 * 1024.0);

    (void)printf("  {\n");
    (void)printf("    \"name\": \"checksum_file_throughput\",\n");
    (void)printf("    \"unit\": \"MB/s\",\n");
    (void)printf("    \"value\": %.2f\n", throughput_mb_s);
    (void)printf("  }");

    // Clean up the temporary file
    (void) apr_file_remove(filename, pool);
}

#ifdef FTWIN_TEST_BUILD
static void create_bench_files(const char *dir, int num_files, size_t file_size)
{
    mkdir(dir, MKDIR_MODE);

    char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
	(void)fprintf(stderr, "Failed to allocate buffer for file creation.\n");
	return;
    }

    /* Fill buffer with pattern */
    for (size_t i = 0; i < BUFFER_SIZE; ++i) {
	buffer[i] = (char) (i % 256);
    }

    /* Create base files and duplicates */
    for (int i = 0; i < num_files / 3; i++) {
	char filename[256];
	(void)snprintf(filename, sizeof(filename), "%s/base%d.dat", dir, i);

	FILE *f = fopen(filename, "wb");
	if (f) {
	    for (size_t j = 0; j < file_size / BUFFER_SIZE; j++) {
		(void)fwrite(buffer, 1, BUFFER_SIZE, f);
	    }
	    (void) fclose(f);

	    /* Create 2 duplicates of each base file */
	    for (int k = 1; k <= 2; k++) {
		char dupname[256];
		(void)snprintf(dupname, sizeof(dupname), "%s/dup%d_%d.dat", dir, i, k);

		char cmd[1024];
		(void)snprintf(cmd, sizeof(cmd), "cp %s %s", filename, dupname);
		(void) system(cmd);
	    }
	}
    }

    free(buffer);
}

static void cleanup_bench_files(const char *dir)
{
    char command[256];
    (void)snprintf(command, sizeof(command), "rm -rf %s", dir);
    (void) system(command);
}

static void run_parallel_hashing_benchmark(apr_pool_t *pool)
{
    const char *bench_dir = "/tmp/ftwin_bench";
    const unsigned int thread_counts[] = { 1, 2, 4, 8, 12, 16, 24 };
    const int num_thread_configs = sizeof(thread_counts) / sizeof(unsigned int);

    (void) pool;

    (void)fflush(stdout);
    (void)fflush(stderr);
    (void)fprintf(stderr, "Creating benchmark files...\n");
    create_bench_files(bench_dir, NUM_BENCH_FILES, BENCH_FILE_SIZE);

    for (int t = 0; t < num_thread_configs; t++) {
	unsigned int num_threads = thread_counts[t];

	(void)fflush(stdout);
	(void)fflush(stderr);
	int stdout_save = dup(STDOUT_FILENO);
	int stderr_save = dup(STDERR_FILENO);
	int dev_null_fd = open("/dev/null", O_WRONLY);
	dup2(dev_null_fd, STDOUT_FILENO);
	(void)dup2(dev_null_fd, STDERR_FILENO);
	apr_time_t start_time = apr_time_now();
	char threads_str[16];
	(void)snprintf(threads_str, sizeof(threads_str), "%u", num_threads);
	const char *argv[] = { "ftwin", "-j", threads_str, bench_dir };
	(void)ftwin_main(4, argv);
	apr_time_t end_time = apr_time_now();
	(void)dup2(stdout_save, STDOUT_FILENO);
	(void)dup2(stderr_save, STDERR_FILENO);
	(void)close(dev_null_fd);
	(void)close(stdout_save);
	(void)close(stderr_save);

	apr_time_t total_time = end_time - start_time;
	double time_seconds = (double) total_time / 1000000.0;
	double total_mb = (double) (NUM_BENCH_FILES * BENCH_FILE_SIZE) / (1024.0 * 1024.0);
	double throughput_mb_s = total_mb / time_seconds;

	if (t > 0) {
	    (void)printf(",\n");
	}
	(void)printf("  {\n");
	(void)printf("    \"name\": \"parallel_hashing (%u threads)\",\n", num_threads);
	(void)printf("    \"unit\": \"MB/s\",\n");
	(void)printf("    \"value\": %.2f,\n", throughput_mb_s);
	(void)printf("    \"extra\": \"time_seconds=%.3f\"\n", time_seconds);
	(void)printf("  }");

	(void)fflush(stdout);
    }

    cleanup_bench_files(bench_dir);
    (void)fprintf(stderr, "Benchmark complete.\n");
}
#endif
