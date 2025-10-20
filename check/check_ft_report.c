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
#include <ft_report.h>
#include <ft_config.h>

enum
{ CAPTURE_BUFFER_SIZE = 4096 };

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

START_TEST(test_chksum_cmp)
{
    ft_file_t file1 = { .path = "file1", .hash = { .p = &file1 }, .prioritized = 0 };
    ft_file_t file2 = { .path = "file2", .hash = { .p = &file2 }, .prioritized = 0 };

    memcpy(file1.hash.p, "hash1", 5);
    memcpy(file2.hash.p, "hash2", 5);

    ck_assert_int_lt(ft_chksum_cmp(&file1, &file2), 0);

    memcpy(file2.hash.p, "hash1", 5);
    ck_assert_int_eq(ft_chksum_cmp(&file1, &file2), 0);

    file2.prioritized = 1;
    ck_assert_int_gt(ft_chksum_cmp(&file1, &file2), 0);
}
END_TEST

START_TEST(test_report_options)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    conf->mask = OPTION_SIZED | OPTION_DRY_RUN;
    conf->sep = ';';

    napr_heap_t *heap = napr_heap_make(pool, ft_chksum_cmp);
    ft_file_t file1 = { .path = "file1", .size = 1024, .hash = { .p = "hash1" }, .prioritized = 0 };
    ft_file_t file2 = { .path = "file2", .size = 1024, .hash = { .p = "hash1" }, .prioritized = 0 };
    napr_heap_insert(heap, &file1);
    napr_heap_insert(heap, &file2);

    int original_fds[2];
    int pipe_fds[2];
    setup_output_capture(original_fds, pipe_fds);

    ft_report(conf, heap);

    restore_output(original_fds);
    char *output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_ptr_nonnull(strstr(output, "1.0K"));
    ck_assert_ptr_nonnull(strstr(output, ";"));
    ck_assert_ptr_nonnull(strstr(output, "file1"));
    ck_assert_ptr_nonnull(strstr(output, "file2"));

    apr_pool_destroy(pool);
}
END_TEST

START_TEST(test_report_archive)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    conf->mask = OPTION_UNTAR;

    napr_heap_t *heap = napr_heap_make(pool, ft_chksum_cmp);
    ft_file_t file1 = { .path = "archive.tar", .subpath = "file1", .size = 1024, .hash = { .p = "hash1" }, .prioritized = 0 };
    ft_file_t file2 = { .path = "file2", .size = 1024, .hash = { .p = "hash1" }, .prioritized = 0 };
    napr_heap_insert(heap, &file1);
    napr_heap_insert(heap, &file2);

    int original_fds[2];
    int pipe_fds[2];
    setup_output_capture(original_fds, pipe_fds);

    ft_report(conf, heap);

    restore_output(original_fds);
    char *output = capture_output(pipe_fds[0], pipe_fds);

    ck_assert_ptr_nonnull(strstr(output, "archive.tar:file1"));
    ck_assert_ptr_nonnull(strstr(output, "file2"));

    apr_pool_destroy(pool);
}
END_TEST

Suite *make_ft_report_suite(void)
{
    Suite *suite = suite_create("Report");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_chksum_cmp);
    tcase_add_test(tc_core, test_report_options);
    tcase_add_test(tc_core, test_report_archive);

    suite_add_tcase(suite, tc_core);
    return suite;
}
