/*
 * Copyright (C) 2025 Francois Pesce <francois (dot) pesce (at) gmail (dot) com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law of a-zing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <check.h>
#include <ftwin.h>
#include <ft_report_json.h>
#include <ft_config.h>

#if HAVE_JANSSON
#include <jansson.h>
#endif

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
    dup2(pipe_fds[1], STDOUT_FILeno);
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

START_TEST(test_format_time)
{
    char buf[21];
    time_t now = 1672531200; // 2023-01-01 00:00:00 UTC
    ft_format_time_iso8601_utc(buf, now);
    ck_assert_str_eq(buf, "2023-01-01T00:00:00Z");
}
END_TEST

START_TEST(test_hash_to_hex)
{
    char buf[33];
    unsigned char hash[16] = { 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xbe, 0xef };
    ft_hash_to_hex(buf, hash);
    ck_assert_str_eq(buf, "deadbeefdeadbeefdeadbeefdeadbeef");
}
END_TEST

#if HAVE_JANSSON
START_TEST(test_json_output)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    conf->mask = OPTION_JSON;

    napr_heap_t *heap = napr_heap_make(pool, ft_chksum_cmp);
    ft_file_t file1 = { .path = "file1", .size = 1024, .mtime = 1672531200, .hash = { .p = "hash1" }, .prioritized = 0 };
    ft_file_t file2 = { .path = "file2", .size = 1024, .mtime = 1672531200, .hash = { .p = "hash1" }, .prioritized = 0 };
    napr_heap_insert(heap, &file1);
    napr_heap_insert(heap, &file2);

    int original_fds[2];
    int pipe_fds[2];
    setup_output_capture(original_fds, pipe_fds);

    ft_report_json(conf, heap);

    restore_output(original_fds);
    char *output = capture_output(pipe_fds[0], pipe_fds);

    json_error_t error;
    json_t *root = json_loads(output, 0, &error);
    ck_assert_ptr_nonnull(root);

    ck_assert(json_is_array(root));
    ck_assert_int_eq(json_array_size(root), 1);

    json_t *set = json_array_get(root, 0);
    ck_assert_int_eq(json_integer_value(json_object_get(set, "size_bytes")), 1024);

    json_decref(root);
    apr_pool_destroy(pool);
}
END_TEST

START_TEST(test_json_archive_output)
{
    apr_pool_t *pool = NULL;
    ck_assert_int_eq(apr_pool_create(&pool, NULL), APR_SUCCESS);

    ft_conf_t *conf = ft_config_create(pool);
    conf->mask = OPTION_JSON | OPTION_UNTAR;

    napr_heap_t *heap = napr_heap_make(pool, ft_chksum_cmp);
    ft_file_t file1 = { .path = "archive.tar", .subpath = "file1", .size = 1024, .mtime = 1672531200, .hash = { .p = "hash1" }, .prioritized = 0 };
    ft_file_t file2 = { .path = "file2", .size = 1024, .mtime = 1672531200, .hash = { .p = "hash1" }, .prioritized = 0 };
    napr_heap_insert(heap, &file1);
    napr_heap_insert(heap, &file2);

    int original_fds[2];
    int pipe_fds[2];
    setup_output_capture(original_fds, pipe_fds);

    ft_report_json(conf, heap);

    restore_output(original_fds);
    char *output = capture_output(pipe_fds[0], pipe_fds);

    json_error_t error;
    json_t *root = json_loads(output, 0, &error);
    ck_assert_ptr_nonnull(root);

    json_t *set = json_array_get(root, 0);
    json_t *duplicates = json_object_get(set, "duplicates");
    json_t *dup1 = json_array_get(duplicates, 0);
    const char *subpath = json_string_value(json_object_get(dup1, "archive_subpath"));
    ck_assert_str_eq(subpath, "file1");

    json_decref(root);
    apr_pool_destroy(pool);
}
END_TEST
#endif

Suite *make_ft_report_json_suite(void)
{
    Suite *suite = suite_create("ReportJSON");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_format_time);
    tcase_add_test(tc_core, test_hash_to_hex);
#if HAVE_JANSSON
    tcase_add_test(tc_core, test_json_output);
    tcase_add_test(tc_core, test_json_archive_output);
#endif

    suite_add_tcase(suite, tc_core);
    return suite;
}
