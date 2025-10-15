/*
 * Copyright (C) 2025 François Pesce : francois.pesce (at) gmail (dot) com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <check.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <apr.h>
#include <apr_pools.h>
#ifdef HAVE_CONFIG_H
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef PACKAGE_BUGREPORT
#include "config.h"
#endif
#include "ftwin.h"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *main_pool = NULL;

static void setup(void)
{
    if (main_pool == NULL) {
	apr_initialize();
	atexit(apr_terminate);
	apr_pool_create(&main_pool, NULL);
    }
}

static char *capture_output(int fd)
{
    static char buffer[8192];
    memset(buffer, 0, sizeof(buffer));
    read(fd, buffer, sizeof(buffer) - 1);
    return buffer;
}

static void create_test_file(const char *path, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (f) {
	for (size_t i = 0; i < size; i++) {
	    fputc((i % 256), f);
	}
	fclose(f);
    }
}

/**
 * Test that single-threaded and multi-threaded produce identical results
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_parallel_correctness)
{
    int stdout_pipe1[2], stdout_pipe2[2];
    int stderr_pipe1[2], stderr_pipe2[2];

    /* Create test files with duplicates */
    mkdir("check/tests/parallel_test", 0755);
    create_test_file("check/tests/parallel_test/file1.dat", 10240);
    system("cp check/tests/parallel_test/file1.dat check/tests/parallel_test/file2.dat");
    system("cp check/tests/parallel_test/file1.dat check/tests/parallel_test/file3.dat");
    create_test_file("check/tests/parallel_test/file4.dat", 10240);
    system("cp check/tests/parallel_test/file4.dat check/tests/parallel_test/file5.dat");

    /* Test with single thread */
    pipe(stdout_pipe1);
    pipe(stderr_pipe1);
    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe1[1], STDOUT_FILENO);
    dup2(stderr_pipe1[1], STDERR_FILENO);

    const char *argv1[] = { "ftwin", "-j", "1", "check/tests/parallel_test" };
    ftwin_main(4, argv1);

    close(stdout_pipe1[1]);
    close(stderr_pipe1[1]);
    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output1 = strdup(capture_output(stdout_pipe1[0]));
    close(stdout_pipe1[0]);
    close(stderr_pipe1[0]);

    /* Test with 4 threads */
    pipe(stdout_pipe2);
    pipe(stderr_pipe2);

    dup2(stdout_pipe2[1], STDOUT_FILENO);
    dup2(stderr_pipe2[1], STDERR_FILENO);

    const char *argv2[] = { "ftwin", "-j", "4", "check/tests/parallel_test" };
    ftwin_main(4, argv2);

    close(stdout_pipe2[1]);
    close(stderr_pipe2[1]);
    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output2 = capture_output(stdout_pipe2[0]);
    close(stdout_pipe2[0]);
    close(stderr_pipe2[0]);

    /* Verify both outputs contain the same duplicates */
    ck_assert_ptr_ne(strstr(output1, "file1.dat"), NULL);
    ck_assert_ptr_ne(strstr(output1, "file2.dat"), NULL);
    ck_assert_ptr_ne(strstr(output1, "file3.dat"), NULL);
    ck_assert_ptr_ne(strstr(output1, "file4.dat"), NULL);
    ck_assert_ptr_ne(strstr(output1, "file5.dat"), NULL);

    ck_assert_ptr_ne(strstr(output2, "file1.dat"), NULL);
    ck_assert_ptr_ne(strstr(output2, "file2.dat"), NULL);
    ck_assert_ptr_ne(strstr(output2, "file3.dat"), NULL);
    ck_assert_ptr_ne(strstr(output2, "file4.dat"), NULL);
    ck_assert_ptr_ne(strstr(output2, "file5.dat"), NULL);

    /* Cleanup */
    free(output1);
    system("rm -rf check/tests/parallel_test");
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * Test thread pool with various thread counts
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_thread_counts)
{
    int stdout_pipe[2];
    int stderr_pipe[2];

    mkdir("check/tests/thread_test", 0755);
    create_test_file("check/tests/thread_test/a.dat", 5120);
    system("cp check/tests/thread_test/a.dat check/tests/thread_test/b.dat");
    system("cp check/tests/thread_test/a.dat check/tests/thread_test/c.dat");

    /* Test with various thread counts: 1, 2, 4, 8 */
    const char *thread_counts[] = { "1", "2", "4", "8", "12", "16", "24" };
    for (int i = 0; i < (sizeof(thread_counts) / sizeof(const char *)); i++) {
	pipe(stdout_pipe);
	pipe(stderr_pipe);

	int original_stdout = dup(STDOUT_FILENO);
	int original_stderr = dup(STDERR_FILENO);

	dup2(stdout_pipe[1], STDOUT_FILENO);
	dup2(stderr_pipe[1], STDERR_FILENO);

	const char *argv[] = { "ftwin", "-j", thread_counts[i], "check/tests/thread_test" };
	int result = ftwin_main(4, argv);

	close(stdout_pipe[1]);
	close(stderr_pipe[1]);
	dup2(original_stdout, STDOUT_FILENO);
	dup2(original_stderr, STDERR_FILENO);

	char *output = capture_output(stdout_pipe[0]);
	close(stdout_pipe[0]);
	close(stderr_pipe[0]);

	/* Verify duplicates found regardless of thread count */
	ck_assert_int_eq(result, 0);
	ck_assert_ptr_ne(strstr(output, "a.dat"), NULL);
	ck_assert_ptr_ne(strstr(output, "b.dat"), NULL);
	ck_assert_ptr_ne(strstr(output, "c.dat"), NULL);
    }

    system("rm -rf check/tests/thread_test");
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * Test with files of various sizes
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_various_file_sizes)
{
    int stdout_pipe[2];
    int stderr_pipe[2];

    mkdir("check/tests/size_test", 0755);

    /* Create files of different sizes with duplicates */
    create_test_file("check/tests/size_test/tiny1.dat", 10);
    system("cp check/tests/size_test/tiny1.dat check/tests/size_test/tiny2.dat");

    create_test_file("check/tests/size_test/small1.dat", 1024);
    system("cp check/tests/size_test/small1.dat check/tests/size_test/small2.dat");

    create_test_file("check/tests/size_test/medium1.dat", 50000);
    system("cp check/tests/size_test/medium1.dat check/tests/size_test/medium2.dat");

    pipe(stdout_pipe);
    pipe(stderr_pipe);

    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    const char *argv[] = { "ftwin", "-j", "2", "check/tests/size_test" };
    int result = ftwin_main(4, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output = capture_output(stdout_pipe[0]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    /* Verify all size categories found duplicates */
    ck_assert_int_eq(result, 0);
    ck_assert_ptr_ne(strstr(output, "tiny1.dat"), NULL);
    ck_assert_ptr_ne(strstr(output, "tiny2.dat"), NULL);
    ck_assert_ptr_ne(strstr(output, "small1.dat"), NULL);
    ck_assert_ptr_ne(strstr(output, "small2.dat"), NULL);
    ck_assert_ptr_ne(strstr(output, "medium1.dat"), NULL);
    ck_assert_ptr_ne(strstr(output, "medium2.dat"), NULL);

    system("rm -rf check/tests/size_test");
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * Test with many files to stress test thread pool
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_many_files)
{
    int stdout_pipe[2];
    int stderr_pipe[2];

    mkdir("check/tests/many_test", 0755);

    /* Create 20 sets of duplicate files (3 copies each = 60 files) */
    for (int i = 0; i < 20; i++) {
	char cmd[512];
	char base[128];
	snprintf(base, sizeof(base), "check/tests/many_test/base%d.dat", i);
	create_test_file(base, 1024 + i * 100);

	for (int j = 1; j <= 2; j++) {
	    snprintf(cmd, sizeof(cmd), "cp %s check/tests/many_test/dup%d_%d.dat", base, i, j);
	    system(cmd);
	}
    }

    pipe(stdout_pipe);
    pipe(stderr_pipe);

    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    const char *argv[] = { "ftwin", "-j", "4", "check/tests/many_test" };
    int result = ftwin_main(4, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output = capture_output(stdout_pipe[0]);
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    /* Verify no crashes and duplicates found */
    ck_assert_int_eq(result, 0);
    ck_assert_ptr_ne(strstr(output, "base0.dat"), NULL);
    ck_assert_ptr_ne(strstr(output, "dup0_1.dat"), NULL);
    ck_assert_ptr_ne(strstr(output, "base19.dat"), NULL);
    ck_assert_ptr_ne(strstr(output, "dup19_1.dat"), NULL);

    system("rm -rf check/tests/many_test");
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_parallel_hashing_suite(void)
{
    Suite *s = suite_create("ParallelHashing");
    TCase *tc_core = tcase_create("Core");

    /* Increase timeout for these tests as they create many files */
    tcase_set_timeout(tc_core, 30);

    tcase_add_checked_fixture(tc_core, setup, NULL);
    tcase_add_test(tc_core, test_parallel_correctness);
    tcase_add_test(tc_core, test_thread_counts);
    tcase_add_test(tc_core, test_various_file_sizes);
    tcase_add_test(tc_core, test_many_files);

    suite_add_tcase(s, tc_core);

    return s;
}
