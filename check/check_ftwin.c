/*
 * Copyright (C) 2007 François Pesce : francois.pesce (at) gmail (dot) com
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
#if HAVE_JANSSON
#include <jansson.h>
#endif
#include "ftwin.h"
#include <unistd.h>

apr_pool_t *main_pool = NULL;

static char *capture_output(int fd)
{
    static char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    read(fd, buffer, sizeof(buffer) - 1);
    return buffer;
}

START_TEST(test_ftwin_size_options)
{
    int stdout_pipe[2];
    int stderr_pipe[2];
    pipe(stdout_pipe);
    pipe(stderr_pipe);

    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    system("cp check/tests/5K_file check/tests/5K_file_copy");

    const char *argv[] =
	{ "ftwin", "-m", "2K", "-M", "8K", "check/tests/1K_file", "check/tests/5K_file", "check/tests/10K_file",
	"check/tests/5K_file_copy"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ftwin_main(argc, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output = capture_output(stdout_pipe[0]);

    ck_assert_ptr_ne(strstr(output, "check/tests/5K_file"), NULL);
    ck_assert_ptr_ne(strstr(output, "check/tests/5K_file_copy"), NULL);
    ck_assert_ptr_eq(strstr(output, "check/tests/1K_file"), NULL);
    ck_assert_ptr_eq(strstr(output, "check/tests/10K_file"), NULL);

    remove("check/tests/5K_file_copy");
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_ftwin_no_recurse)
{
    int stdout_pipe[2];
    int stderr_pipe[2];
    pipe(stdout_pipe);
    pipe(stderr_pipe);

    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    const char *argv[] = { "ftwin", "-R", "check/tests/recurse" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ftwin_main(argc, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output = capture_output(stdout_pipe[0]);

    ck_assert_ptr_eq(strstr(output, "file2"), NULL);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_ftwin_hidden_files)
{
    int stdout_pipe[2];
    int stderr_pipe[2];
    pipe(stdout_pipe);
    pipe(stderr_pipe);

    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    const char *argv[] = { "ftwin", "check/tests/recurse" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ftwin_main(argc, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output = capture_output(stdout_pipe[0]);

    ck_assert_ptr_eq(strstr(output, ".hidden_file"), NULL);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_ftwin_show_hidden_files)
{
    int stdout_pipe[2];
    int stderr_pipe[2];
    pipe(stdout_pipe);
    pipe(stderr_pipe);

    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    const char *argv[] = { "ftwin", "-a", "check/tests/recurse" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ftwin_main(argc, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output = capture_output(stdout_pipe[0]);

    ck_assert_ptr_ne(strstr(output, ".hidden_file"), NULL);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

#if HAVE_JANSSON
START_TEST(test_ftwin_json_output_validation)
{
    // Setup pipes and redirection (boilerplate from existing tests)
    int stdout_pipe[2];
    int stderr_pipe[2];
    pipe(stdout_pipe);
    pipe(stderr_pipe);

    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    // Prepare test files
    system("cp check/tests/5K_file check/tests/5K_file_copy");

    // Determine expected absolute paths dynamically for portability
    char cwd[4096];
    ck_assert_ptr_ne(getcwd(cwd, sizeof(cwd)), NULL);
    char expected_abs_path1[4096];
    char expected_abs_path2[4096];
    snprintf(expected_abs_path1, sizeof(expected_abs_path1), "%s/check/tests/5K_file", cwd);
    snprintf(expected_abs_path2, sizeof(expected_abs_path2), "%s/check/tests/5K_file_copy", cwd);

    // Run ftwin with --json using relative input paths (ftwin should resolve them)
    const char *argv[] = { "ftwin", "-J", "check/tests/5K_file", "check/tests/5K_file_copy", "check/tests/1K_file" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    ftwin_main(argc, argv);

    // Restore stdout/stderr and capture output
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);
    char *output = capture_output(stdout_pipe[0]);

    // Parse and Validate JSON
    json_error_t error;
    json_t *root = json_loads(output, 0, &error);

    ck_assert_msg(root != NULL, "JSON parsing failed: %s at line %d\nOutput:\n%s", error.text, error.line, output);
    ck_assert(json_is_array(root));
    ck_assert_int_eq(json_array_size(root), 1);	// Expect 1 set (the 5K files)

    json_t *set = json_array_get(root, 0);
    // Validate metadata (5K file size is 5120 bytes)
    ck_assert_int_eq(json_integer_value(json_object_get(set, "size_bytes")), 5120);
    ck_assert(json_is_string(json_object_get(set, "hash_xxh128")));

    json_t *duplicates = json_object_get(set, "duplicates");
    ck_assert_int_eq(json_array_size(duplicates), 2);

    // Validate file entries
    json_t *dup1 = json_array_get(duplicates, 0);

    // Check for UTC timestamp (ends with Z)
    const char *mtime1 = json_string_value(json_object_get(dup1, "mtime_utc"));
    ck_assert_ptr_ne(mtime1, NULL);
    ck_assert_msg(mtime1[strlen(mtime1) - 1] == 'Z', "Timestamp is not in UTC format (missing Z)");

    // Check paths (must be absolute)
    const char *path1 = json_string_value(json_object_get(dup1, "path"));
    const char *path2 = json_string_value(json_object_get(json_array_get(duplicates, 1), "path"));

    // Check if the output paths match the expected absolute paths (order independent)
    int match1 = (strcmp(path1, expected_abs_path1) == 0) || (strcmp(path1, expected_abs_path2) == 0);
    int match2 = (strcmp(path2, expected_abs_path1) == 0) || (strcmp(path2, expected_abs_path2) == 0);

    ck_assert_msg(match1 && match2 && strcmp(path1, path2) != 0, "JSON output paths do not match expected absolute paths.");

    json_decref(root);
    remove("check/tests/5K_file_copy");
}

END_TEST
#endif
Suite *make_ftwin_suite(void)
{
    Suite *s = suite_create("Ftwin");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_ftwin_size_options);
    tcase_add_test(tc_core, test_ftwin_no_recurse);
    tcase_add_test(tc_core, test_ftwin_hidden_files);
    tcase_add_test(tc_core, test_ftwin_show_hidden_files);
#if HAVE_JANSSON
    tcase_add_test(tc_core, test_ftwin_json_output_validation);
#endif

    suite_add_tcase(s, tc_core);

    return s;
}

Suite *make_napr_heap_suite(void);
Suite *make_apr_hash_suite(void);
Suite *make_ft_file_suite(void);
Suite *make_archive_suite(void);
Suite *make_human_size_suite(void);
Suite *make_ft_system_suite(void);
Suite *make_parallel_hashing_suite(void);

int main(int argc, char **argv)
{
    char buf[256];
    int nf;
    apr_status_t status;
    SRunner *sr;
    int num = 0;

    if (argc > 1) {
	num = atoi(argv[1]);
    }

    status = apr_initialize();
    if (APR_SUCCESS != status) {
	apr_strerror(status, buf, 200);
	printf("error: %s\n", buf);
    }

    atexit(apr_terminate);

    if ((status = apr_pool_create(&main_pool, NULL)) != APR_SUCCESS) {
	apr_strerror(status, buf, 200);
	printf("error: %s\n", buf);
    }

    sr = srunner_create(NULL);

    if (!num || num == 1)
	srunner_add_suite(sr, make_napr_heap_suite());

    if (!num || num == 2)
	srunner_add_suite(sr, make_apr_hash_suite());

    if (!num || num == 3)
	srunner_add_suite(sr, make_ft_file_suite());

    if (!num || num == 4)
	srunner_add_suite(sr, make_archive_suite());

    if (!num || num == 5)
	srunner_add_suite(sr, make_human_size_suite());

    if (!num || num == 6)
	srunner_add_suite(sr, make_ftwin_suite());

    if (!num || num == 7)
	srunner_add_suite(sr, make_ft_system_suite());

    if (!num || num == 8)
	srunner_add_suite(sr, make_parallel_hashing_suite());

    srunner_set_fork_status(sr, CK_NOFORK);
    srunner_set_xml(sr, "check_log.xml");

    srunner_run_all(sr, CK_NORMAL);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);

    apr_terminate();
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
