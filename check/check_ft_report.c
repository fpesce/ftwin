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
#include <string.h>
#include <ftwin.h>
#include <ft_report.h>
#include <ft_config.h>
#include <ft_types.h>
#include <ft_file.h>
#include <apr_file_io.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <archive.h>
#include <archive_entry.h>

enum
{
    DEFAULT_FILE_MODE = 0644,
    ARCHIVE_BUFFER_SIZE = 8192,
};

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void create_test_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");
    ck_assert_ptr_ne(file, NULL);
    if (content) {
        ck_assert_int_ge(fputs(content, file), 0);
    }
    ck_assert_int_eq(fclose(file), 0);
}

static void add_file_to_archive(struct archive *archive, const char *filename)
{
    struct archive_entry *entry = NULL;
    FILE *file = NULL;
    char buffer[ARCHIVE_BUFFER_SIZE] = { 0 };
    size_t length = 0;
    long size = 0;

    entry = archive_entry_new();
    ck_assert_ptr_ne(entry, NULL);

    archive_entry_set_pathname(entry, filename);
    archive_entry_set_mode(entry, S_IFREG | DEFAULT_FILE_MODE);

    file = fopen(filename, "rb");
    ck_assert_ptr_ne(file, NULL);
    ck_assert_int_eq(fseek(file, 0, SEEK_END), 0);
    size = ftell(file);
    ck_assert_int_eq(fseek(file, 0, SEEK_SET), 0);

    archive_entry_set_size(entry, size);
    archive_write_header(archive, entry);

    length = fread(buffer, 1, sizeof(buffer), file);
    while (length > 0) {
        archive_write_data(archive, buffer, length);
        length = fread(buffer, 1, sizeof(buffer), file);
    }

    ck_assert_int_eq(fclose(file), 0);
    archive_entry_free(entry);
}

static void create_test_archive(const char *archive_name, const char **filenames, int num_files)
{
    struct archive *archive = archive_write_new();
    ck_assert_ptr_ne(archive, NULL);

    archive_write_set_format_pax_restricted(archive);
    archive_write_open_filename(archive, archive_name);

    for (int i = 0; i < num_files; ++i) {
        add_file_to_archive(archive, filenames[i]);
    }

    archive_write_close(archive);
    archive_write_free(archive);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_chksum_cmp)
{
    ft_file_t file1 = {.path = "file1",.prioritized = 0 };
    ft_file_t file2 = {.path = "file2",.prioritized = 0 };

    ft_chksum_t chk1 = {.file = &file1 };
    ft_chksum_t chk2 = {.file = &file2 };

    // Set hash1 > hash2
    chk1.hash_value.high64 = 0x2;
    chk1.hash_value.low64 = 0;
    chk2.hash_value.high64 = 0x1;
    chk2.hash_value.low64 = 0;

    ck_assert_int_gt(ft_chksum_cmp(&chk1, &chk2), 0);

    // Set hash1 == hash2
    chk2.hash_value.high64 = 0x2;
    chk2.hash_value.low64 = 0;
    ck_assert_int_eq(ft_chksum_cmp(&chk1, &chk2), 0);

    // Same hash, but file2 is prioritized
    file2.prioritized = 1;
    ck_assert_int_gt(ft_chksum_cmp(&chk1, &chk2), 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern apr_pool_t *main_pool;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_get_comparison_paths)
{
    ft_conf_t *conf = ft_config_create(main_pool);
    ck_assert_ptr_ne(conf, NULL);
    conf->mask = OPTION_UNTAR;

    const char *filenames[] = { "file1.txt", "file2.txt" };
    create_test_file(filenames[0], "content1");
    create_test_file(filenames[1], "content2");

    const char *archive_name1 = "test1.tar";
    create_test_archive(archive_name1, &filenames[0], 1);

    const char *archive_name2 = "test2.tar";
    create_test_archive(archive_name2, &filenames[1], 1);

    ft_file_t *file1_archived = ft_file_make(main_pool, archive_name1, "file1.txt");
    ft_file_t *file2_archived = ft_file_make(main_pool, archive_name2, "file2.txt");
    ft_file_t *file1_regular = ft_file_make(main_pool, "file1.txt", NULL);

    char *path1 = NULL;
    char *path2 = NULL;

    // Test case 1: Both files are in archives
    ck_assert_int_eq(get_comparison_paths(conf, file1_archived, file2_archived, &path1, &path2), APR_SUCCESS);
    ck_assert_ptr_ne(path1, NULL);
    ck_assert_ptr_ne(path2, NULL);
    ck_assert_str_ne(path1, file1_archived->path);
    ck_assert_str_ne(path2, file2_archived->path);
    (void) apr_file_remove(path1, main_pool);
    (void) apr_file_remove(path2, main_pool);

    // Test case 2: First file is in an archive, second is a regular file
    path1 = NULL;
    path2 = NULL;
    ck_assert_int_eq(get_comparison_paths(conf, file1_archived, file1_regular, &path1, &path2), APR_SUCCESS);
    ck_assert_ptr_ne(path1, NULL);
    ck_assert_str_ne(path1, file1_archived->path);
    ck_assert_str_eq(path2, file1_regular->path);
    (void) apr_file_remove(path1, main_pool);

    // Test case 3: First file is a regular file, second is in an archive
    path1 = NULL;
    path2 = NULL;
    ck_assert_int_eq(get_comparison_paths(conf, file1_regular, file2_archived, &path1, &path2), APR_SUCCESS);
    ck_assert_str_eq(path1, file1_regular->path);
    ck_assert_ptr_ne(path2, NULL);
    ck_assert_str_ne(path2, file2_archived->path);
    (void) apr_file_remove(path2, main_pool);

    // Test case 4: Failure case - file not in archive
    ft_file_t *file_not_in_archive = ft_file_make(main_pool, archive_name1, "nonexistent.txt");
    ck_assert_int_eq(get_comparison_paths(conf, file_not_in_archive, file1_regular, &path1, &path2), APR_EGENERAL);

    // Test case 5: Failure case - second file not in archive
    ck_assert_int_eq(get_comparison_paths(conf, file1_archived, file_not_in_archive, &path1, &path2), APR_EGENERAL);

    // Cleanup
    (void) remove(archive_name1);
    (void) remove(archive_name2);
    (void) remove(filenames[0]);
    (void) remove(filenames[1]);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_report_suite(void)
{
    Suite *suite = suite_create("Report");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_chksum_cmp);
    tcase_add_test(tc_core, test_get_comparison_paths);

    suite_add_tcase(suite, tc_core);
    return suite;
}
