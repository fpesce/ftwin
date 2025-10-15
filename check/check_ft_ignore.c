/*
 * Copyright (C) 2025 François Pesce : francois.pesce (at) gmail (dot) com
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

static apr_pool_t *main_pool = NULL;

static void setup(void)
{
    if (main_pool == NULL) {
        apr_initialize();
        atexit(apr_terminate);
        apr_pool_create(&main_pool, NULL);
    }
}

/* Test basic pattern matching */
START_TEST(test_ignore_simple_pattern)
{
    ft_ignore_context_t *ctx;
    ft_ignore_match_result_t result;

    ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    ft_ignore_add_pattern_str(ctx, "*.o");

    result = ft_ignore_match(ctx, "/test/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/file.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test directory-only patterns */
START_TEST(test_ignore_directory_pattern)
{
    ft_ignore_context_t *ctx;
    ft_ignore_match_result_t result;

    ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    ft_ignore_add_pattern_str(ctx, "build/");

    /* Directory should be ignored */
    result = ft_ignore_match(ctx, "/test/build", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* File with same name should not be ignored */
    result = ft_ignore_match(ctx, "/test/build", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test double-star patterns */
START_TEST(test_ignore_doublestar_pattern)
{
    ft_ignore_context_t *ctx;
    ft_ignore_match_result_t result;

    ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    ft_ignore_add_pattern_str(ctx, "**/*.tmp");

    /* Should match at any depth */
    result = ft_ignore_match(ctx, "/test/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/subdir/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/a/b/c/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/file.txt", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test negation patterns */
START_TEST(test_ignore_negation_pattern)
{
    ft_ignore_context_t *ctx;
    ft_ignore_match_result_t result;

    ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    ft_ignore_add_pattern_str(ctx, "*.log");
    ft_ignore_add_pattern_str(ctx, "!important.log");

    /* Regular log should be ignored */
    result = ft_ignore_match(ctx, "/test/debug.log", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* important.log should be whitelisted */
    result = ft_ignore_match(ctx, "/test/important.log", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_WHITELISTED);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test rooted patterns (starting with /) */
START_TEST(test_ignore_rooted_pattern)
{
    ft_ignore_context_t *ctx;
    ft_ignore_match_result_t result;

    ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    ft_ignore_add_pattern_str(ctx, "/build");

    /* Should match only at root level */
    result = ft_ignore_match(ctx, "/test/build", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* Should not match in subdirectory */
    result = ft_ignore_match(ctx, "/test/subdir/build", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test hierarchical context (parent-child) */
START_TEST(test_ignore_hierarchical_context)
{
    ft_ignore_context_t *root_ctx, *child_ctx;
    ft_ignore_match_result_t result;

    /* Root context ignores *.o */
    root_ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    ft_ignore_add_pattern_str(root_ctx, "*.o");

    /* Child context ignores *.tmp */
    child_ctx = ft_ignore_context_create(main_pool, root_ctx, "/test/subdir");
    ft_ignore_add_pattern_str(child_ctx, "*.tmp");

    /* Child should inherit parent's patterns */
    result = ft_ignore_match(child_ctx, "/test/subdir/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* Child should also apply its own patterns */
    result = ft_ignore_match(child_ctx, "/test/subdir/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* Other files should not be ignored */
    result = ft_ignore_match(child_ctx, "/test/subdir/file.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test loading patterns from file */
START_TEST(test_ignore_load_file)
{
    ft_ignore_context_t *ctx;
    ft_ignore_match_result_t result;
    apr_file_t *file;
    const char *gitignore_path = "/tmp/test_gitignore";

    /* Create a temporary .gitignore file */
    apr_file_open(&file, gitignore_path, APR_WRITE | APR_CREATE | APR_TRUNCATE, APR_OS_DEFAULT, main_pool);
    apr_file_puts("*.o\n", file);
    apr_file_puts("build/\n", file);
    apr_file_puts("# This is a comment\n", file);
    apr_file_puts("\n", file);
    apr_file_puts("*.tmp\n", file);
    apr_file_close(file);

    /* Load patterns */
    ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    apr_status_t status = ft_ignore_load_file(ctx, gitignore_path);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify patterns were loaded */
    result = ft_ignore_match(ctx, "/test/file.o", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/build", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/file.tmp", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    /* Clean up */
    apr_file_remove(gitignore_path, main_pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test VCS directory patterns */
START_TEST(test_ignore_vcs_directories)
{
    ft_ignore_context_t *ctx;
    ft_ignore_match_result_t result;

    ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    ft_ignore_add_pattern_str(ctx, ".git/");
    ft_ignore_add_pattern_str(ctx, ".svn/");

    result = ft_ignore_match(ctx, "/test/.git", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/.svn", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/.github", 1);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test wildcard patterns */
START_TEST(test_ignore_wildcard_patterns)
{
    ft_ignore_context_t *ctx;
    ft_ignore_match_result_t result;

    ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    ft_ignore_add_pattern_str(ctx, "test_*.c");

    result = ft_ignore_match(ctx, "/test/test_foo.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/test_bar.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);

    result = ft_ignore_match(ctx, "/test/mytest.c", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_NONE);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Test last-match-wins behavior */
START_TEST(test_ignore_last_match_wins)
{
    ft_ignore_context_t *ctx;
    ft_ignore_match_result_t result;

    ctx = ft_ignore_context_create(main_pool, NULL, "/test");
    ft_ignore_add_pattern_str(ctx, "*.log");
    ft_ignore_add_pattern_str(ctx, "!important.log");
    ft_ignore_add_pattern_str(ctx, "*.log");

    /* The last *.log should override the negation */
    result = ft_ignore_match(ctx, "/test/important.log", 0);
    ck_assert_int_eq(result, FT_IGNORE_MATCH_IGNORED);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_ft_ignore_suite(void)
{
    Suite *s = suite_create("FtIgnore");
    TCase *tc_core = tcase_create("Core");

    tcase_add_checked_fixture(tc_core, setup, NULL);
    tcase_add_test(tc_core, test_ignore_simple_pattern);
    tcase_add_test(tc_core, test_ignore_directory_pattern);
    tcase_add_test(tc_core, test_ignore_doublestar_pattern);
    tcase_add_test(tc_core, test_ignore_negation_pattern);
    tcase_add_test(tc_core, test_ignore_rooted_pattern);
    tcase_add_test(tc_core, test_ignore_hierarchical_context);
    tcase_add_test(tc_core, test_ignore_load_file);
    tcase_add_test(tc_core, test_ignore_vcs_directories);
    tcase_add_test(tc_core, test_ignore_wildcard_patterns);
    tcase_add_test(tc_core, test_ignore_last_match_wins);

    suite_add_tcase(s, tc_core);

    return s;
}
