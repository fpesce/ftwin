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
#include "ft_config.h"
#include "ft_file.h"
#include <unistd.h>
#include <archive.h>
#include <archive_entry.h>

enum
{
    CAPTURE_BUFFER_SIZE = 4096,
    DEFAULT_FILE_MODE = 0644,
    ARCHIVE_BUFFER_SIZE = 8192
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
extern apr_pool_t *main_pool;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_hashing_worker_callback_archive_error)
{
    int stderr_pipe[2] = { 0 };
    int original_stderr = 0;
    char *output = NULL;
    const char *argv[] = { "ftwin", "--untar", "test_archive.tar:/non_existent_file.txt", "b.txt" };
    int argc = sizeof(argv) / sizeof(argv[0]);
    const char *files_to_archive[] = { "a.txt" };

    // 1. Setup: Create test files and a tar archive
    create_test_file("a.txt", "identical content");
    create_test_file("b.txt", "identical content");
    create_test_archive("test_archive.tar", files_to_archive, 1);

    // 2. Setup: Capture ftwin's stderr
    (void) pipe(stderr_pipe);
    original_stderr = dup(STDERR_FILENO);
    (void) dup2(stderr_pipe[1], STDERR_FILENO);

    // 3. Run ftwin with archive support, which will call hashing_worker_callback internally
    // and trigger the error.
    ft_config_set_should_exit_on_error(0);
    (void) ftwin_main(argc, argv);
    ft_config_set_should_exit_on_error(1);

    // 4. Restore output and capture result
    (void) close(stderr_pipe[1]);
    (void) dup2(original_stderr, STDERR_FILENO);
    output = capture_output(stderr_pipe[0]);

    // 5. Assertions
    ck_assert_ptr_ne(strstr(output, "error calling ft_archive_untar_file"), "expected error message not found in stderr");

    // 6. Teardown
    (void) remove("a.txt");
    (void) remove("b.txt");
    (void) remove("test_archive.tar");
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_process_suite(void)
{
    Suite *suite = suite_create("Process");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_hashing_worker_callback_archive_error);

    suite_add_tcase(suite, tc_core);

    return suite;
}
