/*
 * Copyright (C) 2025 Francois Pesce <francois (dot) pesce (at) gmail (dot) com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *	http://www.apache.org/licenses/LICENSE-2.0
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
#include "ftwin.h"
#include "ft_file.h"
#include "ft_archive.h"
#include <unistd.h>
#include <archive.h>
#include <archive_entry.h>

enum
{
    CAPTURE_BUFFER_SIZE = 4096,
    DEFAULT_FILE_MODE = 0644,
    ARCHIVE_BUFFER_SIZE = 8192,
    BUFFER_SIZE = 100
};

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

static char *capture_output(int file_descriptor)
{
    static char buffer[CAPTURE_BUFFER_SIZE];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Safe: using sizeof(buffer) for bounds checking
    memset(buffer, 0, sizeof(buffer));
    (void) read(file_descriptor, buffer, sizeof(buffer) - 1);
    return buffer;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void create_test_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");
    ck_assert_ptr_ne(file, NULL);
    ck_assert_int_ge(fputs(content, file), 0);
    ck_assert_int_eq(fclose(file), 0);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ftwin_archive_duplicates)
{
    int stdout_pipe[2] = { 0 };
    int stderr_pipe[2] = { 0 };
    int original_stdout = 0;
    int original_stderr = 0;
    char *output = NULL;
    const char *argv[] = { "ftwin", "-t", "test_archive.tar", "d.txt" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    const char *files_to_archive[] = { "a.txt", "b.txt", "c.txt" };

    // 1. Setup: Create test files and a tar archive
    create_test_file("a.txt", "identical content");
    create_test_file("b.txt", "identical content");
    create_test_file("c.txt", "unique content");
    create_test_file("d.txt", "identical content");


    create_test_archive("test_archive.tar", files_to_archive, 3);

    // 2. Setup: Capture ftwin's output
    (void) pipe(stdout_pipe);
    (void) pipe(stderr_pipe);

    original_stdout = dup(STDOUT_FILENO);
    original_stderr = dup(STDERR_FILENO);

    (void) dup2(stdout_pipe[1], STDOUT_FILENO);
    (void) dup2(stderr_pipe[1], STDERR_FILENO);

    // 3. Run ftwin with archive support
    (void) ftwin_main(argc, argv);

    // 4. Restore output and capture result
    (void) close(stdout_pipe[1]);
    (void) close(stderr_pipe[1]);

    (void) dup2(original_stdout, STDOUT_FILENO);
    (void) dup2(original_stderr, STDERR_FILENO);

    output = capture_output(stdout_pipe[0]);

    // 5. Assertions
    // Check that the three identical files are reported as duplicates
    ck_assert_ptr_ne(strstr(output, "a.txt"), "a.txt not found in output");
    ck_assert_ptr_ne(strstr(output, "b.txt"), "b.txt not found in output");
    ck_assert_ptr_ne(strstr(output, "d.txt"), "d.txt not found in output");
    // Check that the unique file is not in the output
    ck_assert_ptr_eq(strstr(output, "c.txt"), NULL);
    // Check that the path inside the archive is correctly formatted
    ck_assert_ptr_ne(strstr(output, "test_archive.tar:/a.txt"), "archive path for a.txt is incorrect");
    ck_assert_ptr_ne(strstr(output, "test_archive.tar:/b.txt"), "archive path for b.txt is incorrect");

    // 6. Teardown
    (void) remove("a.txt");
    (void) remove("b.txt");
    (void) remove("c.txt");
    (void) remove("d.txt");
    (void) remove("test_archive.tar");
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern apr_pool_t *main_pool;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ft_archive_untar_file)
{
    const char *archive_name = "test_unit.tar";
    const char *filenames[] = { "file1.txt", "file2.txt" };
    const char *content1 = "This is file1.";
    const char *content2 = "This is file2.";
    create_test_file(filenames[0], content1);
    create_test_file(filenames[1], content2);
    create_test_archive(archive_name, filenames, 2);

    ft_file_t *file_to_extract = ft_file_make(main_pool, archive_name, "file2.txt");
    char *extracted_path = ft_archive_untar_file(file_to_extract, main_pool);
    ck_assert_ptr_ne(extracted_path, NULL);

    FILE *file = fopen(extracted_path, "r");
    ck_assert_ptr_ne(file, NULL);
    char buf[BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));
    (void) fread(buf, 1, sizeof(buf) - 1, file);
    ck_assert_str_eq(buf, content2);
    (void) fclose(file);

    (void) remove(extracted_path);
    (void) remove(archive_name);
    (void) remove(filenames[0]);
    (void) remove(filenames[1]);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ft_archive_untar_file_not_found)
{
    const char *archive_name = "test_unit.tar";
    const char *filenames[] = { "file1.txt" };
    create_test_file(filenames[0], "content");
    create_test_archive(archive_name, filenames, 1);

    ft_file_t *file_to_extract = ft_file_make(main_pool, archive_name, "non_existent_file.txt");
    char *extracted_path = ft_archive_untar_file(file_to_extract, main_pool);
    ck_assert_ptr_eq(extracted_path, NULL);

    (void) remove(archive_name);
    (void) remove(filenames[0]);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ft_archive_untar_invalid_archive)
{
    const char *archive_name = "invalid_archive.txt";
    create_test_file(archive_name, "this is not a tar file");

    ft_file_t *file_to_extract = ft_file_make(main_pool, archive_name, "any_file.txt");
    char *extracted_path = ft_archive_untar_file(file_to_extract, main_pool);
    ck_assert_ptr_eq(extracted_path, NULL);

    (void) remove(archive_name);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ft_archive_untar_non_existent_archive)
{
    ft_file_t *file_to_extract = ft_file_make(main_pool, "non_existent_archive.tar", "any_file.txt");
    char *extracted_path = ft_archive_untar_file(file_to_extract, main_pool);
    ck_assert_ptr_eq(extracted_path, NULL);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ft_archive_untar_large_file)
{
    const char *archive_name = "large_file.tar";
    const char *filename = "large_file.txt";
    const int file_size = 20000; // Larger than ARCHIVE_BLOCK_SIZE

    // Create a large file
    FILE *file = fopen(filename, "w");
    for (int i = 0; i < file_size; i++) {
        fputc('a', file);
    }
    fclose(file);

    const char *files_to_archive[] = { filename };
    create_test_archive(archive_name, files_to_archive, 1);

    ft_file_t *file_to_extract = ft_file_make(main_pool, archive_name, filename);
    char *extracted_path = ft_archive_untar_file(file_to_extract, main_pool);
    ck_assert_ptr_ne(extracted_path, NULL);

    // Verify file size
    apr_finfo_t finfo;
    ck_assert_int_eq(apr_stat(&finfo, extracted_path, APR_FINFO_SIZE, main_pool), APR_SUCCESS);
    ck_assert_int_eq(finfo.size, file_size);

    (void) remove(extracted_path);
    (void) remove(archive_name);
    (void) remove(filename);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_archive_suite(void)
{
    Suite *suite = suite_create("Archive");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_ftwin_archive_duplicates);
    tcase_add_test(tc_core, test_ft_archive_untar_file);
    tcase_add_test(tc_core, test_ft_archive_untar_file_not_found);
    tcase_add_test(tc_core, test_ft_archive_untar_invalid_archive);
    tcase_add_test(tc_core, test_ft_archive_untar_non_existent_archive);
    tcase_add_test(tc_core, test_ft_archive_untar_large_file);

    suite_add_tcase(suite, tc_core);

    return suite;
}
