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

enum { CAPTURE_BUFFER_SIZE = 4096 };

static char *capture_output(int file_descriptor, int *pipe_fds)
{
    static char buffer[CAPTURE_BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    close(pipe_fds[1]); // Close the write end
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
    (void)fflush(stdout);
    (void)fflush(stderr);
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
    ck_assert_ptr_nonnull(strstr(output, "Please submit at least two files or one directory to process."));
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
    tcase_add_test(tc_core, test_config_invalid_image_threshold);
    tcase_add_test(tc_core, test_config_help_flag);
    tcase_add_test(tc_core, test_config_version_flag);
    tcase_add_test(tc_core, test_config_no_input_files);

    suite_add_tcase(suite, tc_core);
    return suite;
}
