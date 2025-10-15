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
#include <unistd.h>

static char *capture_output(int fd)
{
    static char buffer[4096];
    // NOLINTNEXTLINE(clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling)
    // Safe: using sizeof(buffer) for bounds checking
    memset(buffer, 0, sizeof(buffer));
    read(fd, buffer, sizeof(buffer) - 1);
    return buffer;
}

static void create_test_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    fputs(content, f);
    fclose(f);
}

START_TEST(test_ftwin_archive_duplicates)
{
    // 1. Setup: Create test files and a tar archive
    create_test_file("a.txt", "identical content");
    create_test_file("b.txt", "identical content");
    create_test_file("c.txt", "unique content");
    create_test_file("d.txt", "identical content"); // Standalone duplicate

    system("tar -cf test_archive.tar a.txt b.txt c.txt");

    // 2. Setup: Capture ftwin's output
    int stdout_pipe[2];
    int stderr_pipe[2];
    pipe(stdout_pipe);
    pipe(stderr_pipe);

    int original_stdout = dup(STDOUT_FILENO);
    int original_stderr = dup(STDERR_FILENO);

    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    // 3. Run ftwin with archive support
    const char *argv[] = { "ftwin", "-t", "test_archive.tar", "d.txt" };
    int argc = sizeof(argv) / sizeof(argv[0]);

    ftwin_main(argc, argv);

    // 4. Restore output and capture result
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    dup2(original_stdout, STDOUT_FILENO);
    dup2(original_stderr, STDERR_FILENO);

    char *output = capture_output(stdout_pipe[0]);

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
    remove("a.txt");
    remove("b.txt");
    remove("c.txt");
    remove("d.txt");
    remove("test_archive.tar");
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_archive_suite(void)
{
    Suite *s = suite_create("Archive");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_ftwin_archive_duplicates);

    suite_add_tcase(s, tc_core);

    return s;
}