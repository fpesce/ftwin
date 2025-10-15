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
#include <ctype.h>
#include <apr_file_io.h>

enum
{
    OUTPUT_BUFFER_SIZE = 4096,
    PATH_BUFFER_SIZE = 1024,
    ABS_PATH_BUFFER_SIZE = 2048,
    XXH128_HEX_LENGTH = 32,
    ERROR_BUFFER_SIZE = 256
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
apr_pool_t *main_pool = NULL;

static void copy_file(const char *src_path, const char *dest_path)
{
    apr_file_t *src_file = NULL;
    apr_file_t *dest_file = NULL;
    apr_status_t status_code = APR_SUCCESS;
    char buffer[4096] = { 0 };
    apr_size_t bytes_read = 0;
    apr_size_t bytes_written = 0;

    /* 1. Open files */
    status_code = apr_file_open(&src_file, src_path, APR_READ | APR_BINARY, APR_OS_DEFAULT, main_pool);
    ck_assert_int_eq(status_code, APR_SUCCESS);

    status_code =
	apr_file_open(&dest_file, dest_path, APR_WRITE | APR_CREATE | APR_TRUNCATE | APR_BINARY, APR_OS_DEFAULT, main_pool);
    ck_assert_int_eq(status_code, APR_SUCCESS);

    /* 2. Loop and copy data */
    do {
	apr_size_t bytes_read = sizeof(buffer);
	status_code = apr_file_read(src_file, buffer, &bytes_read);
	if (status_code != APR_SUCCESS && status_code != APR_EOF) {
	    ck_abort_msg("Failed to read from source file");
	}
	if (bytes_read > 0) {
	    apr_size_t bytes_written = bytes_read;
	    status_code = apr_file_write(dest_file, buffer, &bytes_written);
	    ck_assert_int_eq(status_code, APR_SUCCESS);
	    ck_assert_int_eq(bytes_read, bytes_written);
	}
    } while (status_code == APR_SUCCESS);

    /* 3. Close files */
    apr_file_close(src_file);
    apr_file_close(dest_file);
}

static char *capture_output(int file_descriptor)
{
    static char buffer[OUTPUT_BUFFER_SIZE];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Safe: using sizeof(buffer) for bounds checking
    memset(buffer, 0, sizeof(buffer));
    (void) read(file_descriptor, buffer, sizeof(buffer) - 1);
    return buffer;
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ftwin_size_options)
{
    int stdout_pipe[2] = { 0 };
    int stderr_pipe[2] = { 0 };
    int original_stdout = 0;
    int original_stderr = 0;
    const char *argv[] = { "ftwin", "-m", "2K", "-M", "8K", "check/tests/1K_file", "check/tests/5K_file",
	"check/tests/10K_file", "check/tests/5K_file_copy"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    char *output = NULL;

    ck_assert_int_eq(pipe(stdout_pipe), 0);
    ck_assert_int_eq(pipe(stderr_pipe), 0);

    original_stdout = dup(STDOUT_FILENO);
    original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    copy_file("check/tests/5K_file", "check/tests/5K_file_copy");

    ftwin_main(argc, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    output = capture_output(stdout_pipe[0]);

    ck_assert_ptr_ne(strstr(output, "check/tests/5K_file"), NULL);
    ck_assert_ptr_ne(strstr(output, "check/tests/5K_file_copy"), NULL);
    ck_assert_ptr_eq(strstr(output, "check/tests/1K_file"), NULL);
    ck_assert_ptr_eq(strstr(output, "check/tests/10K_file"), NULL);

    remove("check/tests/5K_file_copy");
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ftwin_no_recurse)
{
    int stdout_pipe[2] = { 0 };
    int stderr_pipe[2] = { 0 };
    int original_stdout = 0;
    int original_stderr = 0;
    const char *argv[] = { "ftwin", "-R", "check/tests/recurse" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    char *output = NULL;

    ck_assert_int_eq(pipe(stdout_pipe), 0);
    ck_assert_int_eq(pipe(stderr_pipe), 0);

    original_stdout = dup(STDOUT_FILENO);
    original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    ftwin_main(argc, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    output = capture_output(stdout_pipe[0]);

    ck_assert_ptr_eq(strstr(output, "file2"), NULL);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ftwin_hidden_files)
{
    int stdout_pipe[2] = { 0 };
    int stderr_pipe[2] = { 0 };
    int original_stdout = 0;
    int original_stderr = 0;
    const char *argv[] = { "ftwin", "check/tests/recurse" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    char *output = NULL;

    ck_assert_int_eq(pipe(stdout_pipe), 0);
    ck_assert_int_eq(pipe(stderr_pipe), 0);

    original_stdout = dup(STDOUT_FILENO);
    original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    ftwin_main(argc, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    output = capture_output(stdout_pipe[0]);

    ck_assert_ptr_eq(strstr(output, ".hidden_file"), NULL);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ftwin_show_hidden_files)
{
    int stdout_pipe[2] = { 0 };
    int stderr_pipe[2] = { 0 };
    int original_stdout = 0;
    int original_stderr = 0;
    const char *argv[] = { "ftwin", "-a", "check/tests/recurse" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    char *output = NULL;

    ck_assert_int_eq(pipe(stdout_pipe), 0);
    ck_assert_int_eq(pipe(stderr_pipe), 0);

    original_stdout = dup(STDOUT_FILENO);
    original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    ftwin_main(argc, argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    output = capture_output(stdout_pipe[0]);

    ck_assert_ptr_ne(strstr(output, ".hidden_file"), NULL);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

#if HAVE_JANSSON
static void validate_json_structure(json_t *root, const char *output)
{
    ck_assert_msg(root != NULL, "JSON parsing failed. Output:\n%s", output);
    ck_assert(json_is_array(root));
    ck_assert_int_eq(json_array_size(root), 1);	// Expect 1 set (the 5K files)
}

static void validate_duplicate_set(json_t *set)
{
    const int expected_size = 5120;
    // Validate metadata (5K file size is 5120 bytes)
    ck_assert_int_eq(json_integer_value(json_object_get(set, "size_bytes")), expected_size);

    // Validate hash format
    const char *hash = json_string_value(json_object_get(set, "hash_xxh128"));
    ck_assert_ptr_ne(hash, NULL);
    ck_assert_int_eq(strlen(hash), XXH128_HEX_LENGTH);

    for (size_t i = 0; i < XXH128_HEX_LENGTH; i++) {
	ck_assert_msg(isxdigit(hash[i]), "Hash contains invalid character '%c'", hash[i]);
    }
}

static void validate_duplicate_files(json_t *duplicates, const char *path1, const char *path2)
{
    ck_assert_int_eq(json_array_size(duplicates), 2);

    json_t *dup1 = json_array_get(duplicates, 0);
    const char *mtime1 = json_string_value(json_object_get(dup1, "mtime_utc"));
    ck_assert_ptr_ne(mtime1, NULL);
    ck_assert_msg(mtime1[strlen(mtime1) - 1] == 'Z', "Timestamp is not UTC");

    const char *out_path1 = json_string_value(json_object_get(dup1, "path"));
    const char *out_path2 = json_string_value(json_object_get(json_array_get(duplicates, 1), "path"));

    int match1 = (strcmp(out_path1, path1) == 0) || (strcmp(out_path1, path2) == 0);
    int match2 = (strcmp(out_path2, path1) == 0) || (strcmp(out_path2, path2) == 0);
    ck_assert_msg(match1 && match2 && strcmp(out_path1, out_path2) != 0, "JSON paths mismatch");
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ftwin_json_output_validation)
{
    int stdout_pipe[2] = { 0 };
    int stderr_pipe[2] = { 0 };
    int original_stdout = 0;
    int original_stderr = 0;
    char current_working_dir[PATH_BUFFER_SIZE] = { 0 };
    char path1[ABS_PATH_BUFFER_SIZE] = { 0 };
    char path2[ABS_PATH_BUFFER_SIZE] = { 0 };
    int result = 0;
    char *output = NULL;
    json_error_t error;
    json_t *root = NULL;
    json_t *set = NULL;

    ck_assert_int_eq(pipe(stdout_pipe), 0);
    ck_assert_int_eq(pipe(stderr_pipe), 0);
    original_stdout = dup(STDOUT_FILENO);
    original_stderr = dup(STDERR_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    copy_file("check/tests/5K_file", "check/tests/5K_file_copy");

    ck_assert_ptr_ne(getcwd(current_working_dir, sizeof(current_working_dir)), NULL);

    result = snprintf(path1, sizeof(path1), "%s/check/tests/5K_file", current_working_dir);
    ck_assert_int_ge(result, 0);
    ck_assert_int_lt(result, sizeof(path1));
    result = snprintf(path2, sizeof(path2), "%s/check/tests/5K_file_copy", current_working_dir);
    ck_assert_int_ge(result, 0);
    ck_assert_int_lt(result, sizeof(path2));

    const char *argv[] = { "ftwin", "-J", "check/tests/5K_file", "check/tests/5K_file_copy", "check/tests/1K_file" };
    ftwin_main(sizeof(argv) / sizeof(argv[0]), argv);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    output = capture_output(stdout_pipe[0]);
    root = json_loads(output, 0, &error);

    validate_json_structure(root, output);
    set = json_array_get(root, 0);
    validate_duplicate_set(set);
    validate_duplicate_files(json_object_get(set, "duplicates"), path1, path2);

    json_decref(root);
    (void) remove("check/tests/5K_file_copy");
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

#endif
Suite *make_ftwin_suite(void)
{
    Suite *suite = suite_create("Ftwin");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_ftwin_size_options);
    tcase_add_test(tc_core, test_ftwin_no_recurse);
    tcase_add_test(tc_core, test_ftwin_hidden_files);
    tcase_add_test(tc_core, test_ftwin_show_hidden_files);
#if HAVE_JANSSON
    tcase_add_test(tc_core, test_ftwin_json_output_validation);
#endif

    suite_add_tcase(suite, tc_core);

    return suite;
}

Suite *make_napr_heap_suite(void);
Suite *make_napr_hash_suite(void);
Suite *make_ft_file_suite(void);
Suite *make_human_size_suite(void);
Suite *make_ft_system_suite(void);
Suite *make_parallel_hashing_suite(void);
Suite *make_ft_ignore_suite(void);
Suite *make_ft_archive_suite(void);

enum test_suite
{
    ALL_TESTS = 0,
    NAPR_HEAP_SUITE,
    NAPR_HASH_SUITE,
    FT_FILE_SUITE,
    HUMAN_SIZE_SUITE,
    FTWIN_SUITE,
    FT_SYSTEM_SUITE,
    PARALLEL_HASHING_SUITE,
    FT_IGNORE_SUITE,
    FT_ARCHIVE_SUITE
};

static void add_all_suites(SRunner * suite_runner)
{
    srunner_add_suite(suite_runner, make_napr_heap_suite());
    srunner_add_suite(suite_runner, make_napr_hash_suite());
    srunner_add_suite(suite_runner, make_ft_file_suite());
    srunner_add_suite(suite_runner, make_human_size_suite());
    srunner_add_suite(suite_runner, make_ftwin_suite());
    srunner_add_suite(suite_runner, make_ft_system_suite());
    srunner_add_suite(suite_runner, make_parallel_hashing_suite());
    srunner_add_suite(suite_runner, make_ft_ignore_suite());
    srunner_add_suite(suite_runner, make_ft_archive_suite());
}

int main(int argc, char **argv)
{
    char error_buffer[ERROR_BUFFER_SIZE] = { 0 };
    int number_failed = 0;
    apr_status_t status = APR_SUCCESS;
    SRunner *suite_runner = NULL;
    enum test_suite suite_num = ALL_TESTS;
    long value = 0;

    if (argc > 1) {
	char *end_ptr = NULL;
	value = strtol(argv[1], &end_ptr, 10);

	if ((value == LONG_MAX || value == LONG_MIN) || (value == 0 && argv[1] == end_ptr) || *end_ptr != '\0') {
	    suite_num = ALL_TESTS;
	}
	else {
	    suite_num = (enum test_suite) value;
	}
    }

    status = apr_initialize();
    if (APR_SUCCESS != status) {
	apr_strerror(status, error_buffer, sizeof(error_buffer));
	fprintf(stderr, "APR Initialization error: %s\n", error_buffer);
	return EXIT_FAILURE;
    }

    (void) atexit(apr_terminate);

    status = apr_pool_create(&main_pool, NULL);
    if (status != APR_SUCCESS) {
	apr_strerror(status, error_buffer, sizeof(error_buffer));
	fprintf(stderr, "APR Pool Creation error: %s\n", error_buffer);
	return EXIT_FAILURE;

    }

    suite_runner = srunner_create(NULL);
    if (suite_num == ALL_TESTS) {
	add_all_suites(suite_runner);
    }
    else {
	switch (suite_num) {
	case NAPR_HEAP_SUITE:
	    srunner_add_suite(suite_runner, make_napr_heap_suite());
	    break;
	case NAPR_HASH_SUITE:
	    srunner_add_suite(suite_runner, make_napr_hash_suite());
	    break;
	case FT_FILE_SUITE:
	    srunner_add_suite(suite_runner, make_ft_file_suite());
	    break;
	case HUMAN_SIZE_SUITE:
	    srunner_add_suite(suite_runner, make_human_size_suite());
	    break;
	case FTWIN_SUITE:
	    srunner_add_suite(suite_runner, make_ftwin_suite());
	    break;
	case FT_SYSTEM_SUITE:
	    srunner_add_suite(suite_runner, make_ft_system_suite());
	    break;
	case PARALLEL_HASHING_SUITE:
	    srunner_add_suite(suite_runner, make_parallel_hashing_suite());
	    break;
	case FT_IGNORE_SUITE:
	    srunner_add_suite(suite_runner, make_ft_ignore_suite());
	    break;
	case FT_ARCHIVE_SUITE:
	    srunner_add_suite(suite_runner, make_ft_archive_suite());
	    break;
	default:
	    /* Run all tests if the number is unrecognized */
	    add_all_suites(suite_runner);
	    break;
	}
    }

    srunner_set_fork_status(suite_runner, CK_NOFORK);
    srunner_set_xml(suite_runner, "check_log.xml");

    srunner_run_all(suite_runner, CK_NORMAL);
    number_failed = srunner_ntests_failed(suite_runner);
    srunner_free(suite_runner);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
