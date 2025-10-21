/*
 * Copyright (C) 2025 Francois Pesce : francois.pesce (at) gmail (dot) com
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

#include <check.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include "ft_ignore.h"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *main_pool = NULL;

static void setup(void)
{
    if (main_pool == NULL) {
        (void) apr_initialize();
        (void) atexit(apr_terminate);
        (void) apr_pool_create(&main_pool, NULL);
    }
}

/* Test basic pattern matching */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_simple_pattern)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "*.o");

    result = ft_ignore_match(context, "/test/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/file.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test directory-only patterns */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_directory_pattern)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "build/");

    /* Directory should be ignored */
    result = ft_ignore_match(context, "/test/build", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* File with same name should not be ignored */
    result = ft_ignore_match(context, "/test/build", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test double-star patterns */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_doublestar_pattern)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "**/*.tmp");

    /* Should match at any depth */
    result = ft_ignore_match(context, "/test/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/subdir/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/a/b/c/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/file.txt", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test negation patterns */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_negation_pattern)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "*.log");
    (void) ft_ignore_add_pattern_str(context, "!important.log");

    /* Regular log should be ignored */
    result = ft_ignore_match(context, "/test/debug.log", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* important.log should be whitelisted */
    result = ft_ignore_match(context, "/test/important.log", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_WHITELISTED);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test rooted patterns (starting with /) */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_rooted_pattern)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "/build");

    /* Should match only at root level */
    result = ft_ignore_match(context, "/test/build", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* Should not match in subdirectory */
    result = ft_ignore_match(context, "/test/subdir/build", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test hierarchical context (parent-child) */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_hierarchical_context)
{
    ft_ignore_context_t *root_context = NULL;
    ft_ignore_context_t *child_context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    /* Root context ignores *.o */
    root_context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(root_context, "*.o");

    /* Child context ignores *.tmp */
    child_context = ft_ignore_context_create(main_pool, root_context, "/test/subdir");
    (void) ft_ignore_add_pattern_str(child_context, "*.tmp");

    /* Child should inherit parent's patterns */
    result = ft_ignore_match(child_context, "/test/subdir/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* Child should also apply its own patterns */
    result = ft_ignore_match(child_context, "/test/subdir/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* Other files should not be ignored */
    result = ft_ignore_match(child_context, "/test/subdir/file.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test loading patterns from file */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_load_file)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;
    apr_file_t *file = NULL;
    const char *gitignore_path = "/tmp/test_gitignore";
    apr_status_t status = APR_SUCCESS;

    /* Create a temporary .gitignore file */
    (void) apr_file_open(&file, gitignore_path, APR_WRITE | APR_CREATE | APR_TRUNCATE, APR_OS_DEFAULT, main_pool);
    (void) apr_file_puts("*.o\n", file);
    (void) apr_file_puts("build/\n", file);
    (void) apr_file_puts("# This is a comment\n", file);
    (void) apr_file_puts("\n", file);
    (void) apr_file_puts("*.tmp\n", file);
    (void) apr_file_close(file);

    /* Load patterns */
    context = ft_ignore_context_create(main_pool, NULL, "/test");
    status = ft_ignore_load_file(context, gitignore_path);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify patterns were loaded */
    result = ft_ignore_match(context, "/test/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/build", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* Clean up */
    (void) apr_file_remove(gitignore_path, main_pool);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test VCS directory patterns */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_vcs_directories)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, ".git/");
    (void) ft_ignore_add_pattern_str(context, ".svn/");

    result = ft_ignore_match(context, "/test/.git", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/.svn", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/.github", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test wildcard patterns */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_wildcard_patterns)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "test_*.c");

    result = ft_ignore_match(context, "/test/test_foo.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/test_bar.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/mytest.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test last-match-wins behavior */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_last_match_wins)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "*.log");
    (void) ft_ignore_add_pattern_str(context, "!important.log");
    (void) ft_ignore_add_pattern_str(context, "*.log");

    /* The last *.log should override the negation */
    result = ft_ignore_match(context, "/test/important.log", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test loading patterns from file */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_load_file_with_cr)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;
    apr_file_t *file = NULL;
    const char *gitignore_path = "/tmp/test_gitignore_cr";
    apr_status_t status = APR_SUCCESS;

    /* Create a temporary .gitignore file */
    (void) apr_file_open(&file, gitignore_path, APR_WRITE | APR_CREATE | APR_TRUNCATE, APR_OS_DEFAULT, main_pool);
    (void) apr_file_puts("*.o\r\n", file);
    (void) apr_file_puts("build/\r\n", file);
    (void) apr_file_close(file);

    /* Load patterns */
    context = ft_ignore_context_create(main_pool, NULL, "/test");
    status = ft_ignore_load_file(context, gitignore_path);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify patterns were loaded */
    result = ft_ignore_match(context, "/test/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/build", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* Clean up */
    (void) apr_file_remove(gitignore_path, main_pool);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_leading_whitespace)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "  *.o");

    result = ft_ignore_match(context, "/test/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_question_mark)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "file?.o");

    result = ft_ignore_match(context, "/test/file1.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_char_class)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "file[0-9].o");

    result = ft_ignore_match(context, "/test/file1.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/filea.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_ignore_escaped_char)
{
    ft_ignore_context_t *context = NULL;
    ft_ignore_match_result_t result = FT_IGNORE_MATCH_NONE;

    context = ft_ignore_context_create(main_pool, NULL, "/test");
    (void) ft_ignore_add_pattern_str(context, "\\*.o");

    result = ft_ignore_match(context, "/test/*.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(context, "/test/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_ignore_suite(void)
{
    Suite *suite = suite_create("FtIgnore");
    TCase *tc_core = tcase_create("Core");

    tcase_add_checked_fixture(tc_core, setup, NULL);
    tcase_add_test(tc_core, test_ignore_simple_pattern);
    tcase_add_test(tc_core, test_ignore_directory_pattern);
    tcase_add_test(tc_core, test_ignore_doublestar_pattern);
    tcase_add_test(tc_core, test_ignore_negation_pattern);
    tcase_add_test(tc_core, test_ignore_rooted_pattern);
    tcase_add_test(tc_core, test_ignore_hierarchical_context);
    tcase_add_test(tc_core, test_ignore_load_file);
    tcase_add_test(tc_core, test_ignore_load_file_with_cr);
    tcase_add_test(tc_core, test_ignore_vcs_directories);
    tcase_add_test(tc_core, test_ignore_wildcard_patterns);
    tcase_add_test(tc_core, test_ignore_last_match_wins);
    tcase_add_test(tc_core, test_ignore_leading_whitespace);
    tcase_add_test(tc_core, test_ignore_question_mark);
    tcase_add_test(tc_core, test_ignore_char_class);
    tcase_add_test(tc_core, test_ignore_escaped_char);

    suite_add_tcase(suite, tc_core);

    return suite;
}
