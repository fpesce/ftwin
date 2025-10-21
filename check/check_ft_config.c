/*
 * Copyright (C) 2025 Francois Pesce <francois (dot) pesce (at) gmail (dot) com>
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

#include <check.h>
#include <ftwin.h>
#include <ft_config.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    CAPTURE_BUFFER_SIZE = 4096,
    MAX_NB_MATCH = 30
};

static const double THRESHOLD_MIN = 0.49;
static const double THRESHOLD_MAX = 0.51;

static const double TEST_VALUE_TWO = 0.2;
static const double TEST_VALUE_THREE = 0.3;
static const double TEST_VALUE_SIX = 0.6;
static const double TEST_VALUE_SEVEN = 0.7;

static char *capture_output(int file_descriptor, int *pipe_fds)
{
    static char buffer[CAPTURE_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    close(pipe_fds[1]);         // Close the write end
    read(file_descriptor, buffer, sizeof(buffer) - 1);
    return buffer;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void setup_output_capture(int *original_fds, int *pipe_fds)
{
    ck_assert_int_eq(pipe(pipe_fds), 0);
    original_fds[1] = dup(STDOUT_FILENO);
    original_fds[0] = dup(STDERR_FILENO);
    dup2(pipe_fds[1], STDOUT_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
}

static void restore_output(int *original_fds)
{
    (void) fflush(stdout);
    (void) fflush(stderr);
    dup2(original_fds[1], STDOUT_FILENO);
    dup2(original_fds[0], STDERR_FILENO);
    close(original_fds[0]);
    close(original_fds[1]);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_invalid_numeric_arg)
{
    int original_fds[2];
    int pipe_fds[2];
    char *output = NULL;

    const char *argv[] = { "ftwin", "-j", "foo", "dummy_path" };
    setup_output_capture(original_fds, pipe_fds);
    ft_config_set_should_exit_on_error(0);
    int exit_code = ftwin_main(4, argv);
    restore_output(original_fds);
    output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_int_ne(exit_code, 0);
    ck_assert_ptr_nonnull(strstr(output, "Invalid number of threads"));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_handle_image_options_threshold)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    // Test case for threshold '1'
    ft_conf_t *conf1 = ft_config_create(pool);
    const char *argv1[] = { "ftwin", "-T", "1", "dummy_path" };
    int argc1 = sizeof(argv1) / sizeof(argv1[0]);
    int first_arg1 = 0;
    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code1 = ft_config_parse_args(conf1, argc1, argv1, &first_arg1);
    ft_config_set_should_exit_on_error(1);
    ck_assert_int_eq(exit_code1, APR_SUCCESS);
    ck_assert_double_eq(conf1->threshold, TEST_VALUE_TWO);

    // Test case for threshold '2'
    ft_conf_t *conf2 = ft_config_create(pool);
    const char *argv2[] = { "ftwin", "-T", "2", "dummy_path" };
    int argc2 = sizeof(argv2) / sizeof(argv2[0]);
    int first_arg2 = 0;
    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code2 = ft_config_parse_args(conf2, argc2, argv2, &first_arg2);
    ft_config_set_should_exit_on_error(1);
    ck_assert_int_eq(exit_code2, APR_SUCCESS);
    ck_assert_double_eq(conf2->threshold, TEST_VALUE_THREE);

    // Test case for threshold '4'
    ft_conf_t *conf4 = ft_config_create(pool);
    const char *argv4[] = { "ftwin", "-T", "4", "dummy_path" };
    int argc4 = sizeof(argv4) / sizeof(argv4[0]);
    int first_arg4 = 0;
    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code4 = ft_config_parse_args(conf4, argc4, argv4, &first_arg4);
    ft_config_set_should_exit_on_error(1);
    ck_assert_int_eq(exit_code4, APR_SUCCESS);
    ck_assert_double_eq(conf4->threshold, TEST_VALUE_SIX);

    // Test case for threshold '5'
    ft_conf_t *conf5 = ft_config_create(pool);
    const char *argv5[] = { "ftwin", "-T", "5", "dummy_path" };
    int argc5 = sizeof(argv5) / sizeof(argv5[0]);
    int first_arg5 = 0;
    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code5 = ft_config_parse_args(conf5, argc5, argv5, &first_arg5);
    ft_config_set_should_exit_on_error(1);
    ck_assert_int_eq(exit_code5, APR_SUCCESS);
    ck_assert_double_eq(conf5->threshold, TEST_VALUE_SEVEN);

    apr_pool_destroy(pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_handle_string_option_p_priority_path)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    const char *priority_path = "/my/priority/path";
    const char *argv[] = { "ftwin", "-p", priority_path, "dummy_path" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    int first_arg = 0;

    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code = ft_config_parse_args(conf, argc, argv, &first_arg);
    ft_config_set_should_exit_on_error(1);

    ck_assert_int_eq(exit_code, APR_SUCCESS);
    ck_assert_str_eq(conf->p_path, priority_path);
    ck_assert_int_eq(conf->p_path_len, strlen(priority_path));

    apr_pool_destroy(pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_handle_string_option_s_separator)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    const char *separator = ";";
    const char *argv[] = { "ftwin", "-s", separator, "dummy_path" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    int first_arg = 0;

    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code = ft_config_parse_args(conf, argc, argv, &first_arg);
    ft_config_set_should_exit_on_error(1);

    ck_assert_int_eq(exit_code, APR_SUCCESS);
    ck_assert_int_eq(conf->sep, *separator);

    apr_pool_destroy(pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_handle_string_option_w_whitelist)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    const char *whitelist_regex = "\\.c$";
    const char *argv[] = { "ftwin", "-w", whitelist_regex, "dummy_path" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    int first_arg = 0;

    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code = ft_config_parse_args(conf, argc, argv, &first_arg);
    ft_config_set_should_exit_on_error(1);

    ck_assert_int_eq(exit_code, APR_SUCCESS);
    ck_assert_ptr_nonnull(conf->wl_regex);

    // Check that the regex matches a known string
    int ovector[MAX_NB_MATCH];
    const char *test_file = "test.c";
    int return_code = pcre_exec(conf->wl_regex, NULL, test_file, strlen(test_file), 0, 0, ovector, MAX_NB_MATCH);
    ck_assert_int_ge(return_code, 0);

    apr_pool_destroy(pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ft_hash_add_ignore_list)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    const char *ignore_list = "file1.txt,file2.log,another_dir/";
    const char *filename = "file1.txt";
    const char *filename2 = "file2.log";
    const char *filename3 = "another_dir/";

    ft_config_set_should_exit_on_error(0);
    // This is a static function, but we can test its effects through the config struct.
    // The function `ft_config_parse_args` will call `ft_hash_add_ignore_list` when passed the -i option.
    const char *argv[] = { "ftwin", "-i", ignore_list, "dummy_path" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    int first_arg = 0;
    apr_status_t exit_code = ft_config_parse_args(conf, argc, argv, &first_arg);
    ft_config_set_should_exit_on_error(1);

    ck_assert_int_eq(exit_code, APR_SUCCESS);

    // Check that the files were added to the ignore hash
    ck_assert_ptr_nonnull(napr_hash_search(conf->ig_files, filename, strlen(filename), NULL));
    ck_assert_ptr_nonnull(napr_hash_search(conf->ig_files, filename2, strlen(filename2), NULL));
    ck_assert_ptr_nonnull(napr_hash_search(conf->ig_files, filename3, strlen(filename3), NULL));

    apr_pool_destroy(pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_invalid_excessive_size)
{
    int original_fds[2];
    int pipe_fds[2];
    char *output = NULL;

    const char *argv[] = { "ftwin", "-x", "1Z", "dummy_path" };
    setup_output_capture(original_fds, pipe_fds);
    ft_config_set_should_exit_on_error(0);
    int exit_code = ftwin_main(4, argv);
    restore_output(original_fds);
    output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_int_ne(exit_code, 0);
    ck_assert_ptr_nonnull(strstr(output, "Invalid size for --excessive-size:"));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_invalid_regex)
{
    int original_fds[2];
    int pipe_fds[2];
    char *output = NULL;

    const char *argv[] = { "ftwin", "-e", "[invalid", "dummy_path" };
    setup_output_capture(original_fds, pipe_fds);
    ft_config_set_should_exit_on_error(0);
    int exit_code = ftwin_main(4, argv);
    restore_output(original_fds);
    output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_int_ne(exit_code, 0);
    ck_assert_ptr_nonnull(strstr(output, "can't parse"));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_image_option)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    const char *argv[] = { "ftwin", "-I", "dummy_path" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    int first_arg = 0;

    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code = ft_config_parse_args(conf, argc, argv, &first_arg);
    ft_config_set_should_exit_on_error(1);

    ck_assert_int_eq(exit_code, APR_SUCCESS);
    ck_assert_int_ne(conf->mask & OPTION_PUZZL, 0);
    ck_assert_ptr_nonnull(conf->wl_regex);

    apr_pool_destroy(pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_threshold_option)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    const char *argv[] = { "ftwin", "-T", "3", "dummy_path" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    int first_arg = 0;

    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code = ft_config_parse_args(conf, argc, argv, &first_arg);
    ft_config_set_should_exit_on_error(1);

    ck_assert_int_eq(exit_code, APR_SUCCESS);
    ck_assert(conf->threshold > THRESHOLD_MIN && conf->threshold < THRESHOLD_MAX);

    apr_pool_destroy(pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_archive_option)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    const char *argv[] = { "ftwin", "-t", "dummy_path" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    int first_arg = 0;

    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code = ft_config_parse_args(conf, argc, argv, &first_arg);
    ft_config_set_should_exit_on_error(1);

    ck_assert_int_eq(exit_code, APR_SUCCESS);
    ck_assert_int_ne(conf->mask & OPTION_UNTAR, 0);
    ck_assert_ptr_nonnull(conf->ar_regex);

    apr_pool_destroy(pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_verbose_json_interaction)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    const char *argv[] = { "ftwin", "-v", "-J", "dummy_path" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    int first_arg = 0;

    ft_config_set_should_exit_on_error(0);
    apr_status_t exit_code = ft_config_parse_args(conf, argc, argv, &first_arg);
    ft_config_set_should_exit_on_error(1);

    ck_assert_int_eq(exit_code, APR_SUCCESS);
    ck_assert_int_eq(conf->mask & OPTION_JSON, OPTION_JSON);
    ck_assert_int_eq(conf->mask & OPTION_VERBO, 0);

    apr_pool_destroy(pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_zero_threads)
{
    int original_fds[2];
    int pipe_fds[2];
    char *output = NULL;

    const char *argv[] = { "ftwin", "--threads", "0", "dummy_path" };
    setup_output_capture(original_fds, pipe_fds);
    ft_config_set_should_exit_on_error(0);
    int exit_code = ftwin_main(4, argv);
    restore_output(original_fds);
    output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_int_ne(exit_code, 0);
    ck_assert_ptr_nonnull(strstr(output, "Invalid number of threads"));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_invalid_size_format)
{
    int original_fds[2];
    int pipe_fds[2];
    char *output = NULL;

    const char *argv[] = { "ftwin", "-m", "1Z", "dummy_path" };
    setup_output_capture(original_fds, pipe_fds);
    ft_config_set_should_exit_on_error(0);
    int exit_code = ftwin_main(4, argv);
    restore_output(original_fds);
    output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_int_ne(exit_code, 0);
    ck_assert_ptr_nonnull(strstr(output, "Invalid size for --minimal-length:"));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_invalid_image_threshold)
{
    int original_fds[2];
    int pipe_fds[2];
    char *output = NULL;

    const char *argv[] = { "ftwin", "-T", "99", "dummy_path" };
    setup_output_capture(original_fds, pipe_fds);
    ft_config_set_should_exit_on_error(0);
    int exit_code = ftwin_main(4, argv);
    restore_output(original_fds);
    output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_int_ne(exit_code, 0);
    ck_assert_ptr_nonnull(strstr(output, "invalid threshold:"));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_help_flag)
{
    int original_fds[2];
    int pipe_fds[2];
    char *output = NULL;

    const char *argv[] = { "ftwin", "--help" };
    setup_output_capture(original_fds, pipe_fds);
    ft_config_set_should_exit_on_error(0);
    int exit_code = ftwin_main(2, argv);
    restore_output(original_fds);
    output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_int_ne(exit_code, 0);
    ck_assert_ptr_nonnull(strstr(output, "Usage: ftwin [OPTION]..."));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_version_flag)
{
    int original_fds[2];
    int pipe_fds[2];
    char *output = NULL;

    const char *argv[] = { "ftwin", "--version" };
    setup_output_capture(original_fds, pipe_fds);
    ft_config_set_should_exit_on_error(0);
    int exit_code = ftwin_main(2, argv);
    restore_output(original_fds);
    output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_int_ne(exit_code, 0);
    ck_assert_ptr_nonnull(strstr(output, "ftwin"));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_config_no_input_files)
{
    int original_fds[2];
    int pipe_fds[2];
    char *output = NULL;

    const char *argv[] = { "ftwin" };
    setup_output_capture(original_fds, pipe_fds);
    ft_config_set_should_exit_on_error(0);
    int exit_code = ftwin_main(1, argv);
    restore_output(original_fds);
    output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_int_ne(exit_code, 0);
    ck_assert_ptr_nonnull(strstr(output, "Please submit at least one file or directory to process."));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

static void teardown_config_test(void)
{
    /* Reset flags after each test */
    ft_config_set_should_exit_on_error(1);
}

Suite *make_ft_config_suite(void)
{
    Suite *suite = suite_create("Config");
    TCase *tc_core = tcase_create("Core");

    /* Add teardown to reset flag after each test */
    tcase_add_checked_fixture(tc_core, NULL, teardown_config_test);

    tcase_add_test(tc_core, test_config_invalid_numeric_arg);
    tcase_add_test(tc_core, test_config_zero_threads);
    tcase_add_test(tc_core, test_config_invalid_size_format);
    tcase_add_test(tc_core, test_config_invalid_excessive_size);
    tcase_add_test(tc_core, test_config_invalid_regex);
    tcase_add_test(tc_core, test_config_invalid_image_threshold);
    tcase_add_test(tc_core, test_config_image_option);
    tcase_add_test(tc_core, test_config_threshold_option);
    tcase_add_test(tc_core, test_config_archive_option);
    tcase_add_test(tc_core, test_config_verbose_json_interaction);
    tcase_add_test(tc_core, test_config_help_flag);
    tcase_add_test(tc_core, test_config_version_flag);
    tcase_add_test(tc_core, test_config_no_input_files);
    tcase_add_test(tc_core, test_ft_hash_add_ignore_list);
    tcase_add_test(tc_core, test_handle_string_option_p_priority_path);
    tcase_add_test(tc_core, test_handle_string_option_s_separator);
    tcase_add_test(tc_core, test_handle_string_option_w_whitelist);
    tcase_add_test(tc_core, test_handle_image_options_threshold);

    suite_add_tcase(suite, tc_core);
    return suite;
}
