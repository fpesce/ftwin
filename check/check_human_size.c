#include <check.h>
#include <apr_pools.h>
#include "human_size.h"

static apr_pool_t *pool;

static void setup(void)
{
    apr_initialize();
    apr_pool_create(&pool, NULL);
}

static void teardown(void)
{
    apr_pool_destroy(pool);
    apr_terminate();
}

START_TEST(test_format_human_size)
{
    // Test Bytes
    ck_assert_str_eq(format_human_size(0, pool), "0 B");
    ck_assert_str_eq(format_human_size(512, pool), "512 B");
    ck_assert_str_eq(format_human_size(1023, pool), "1023 B");

    // Test KiB
    ck_assert_str_eq(format_human_size(1024, pool), "1.0 KiB");
    ck_assert_str_eq(format_human_size(1536, pool), "1.5 KiB");

    // Test MiB
    ck_assert_str_eq(format_human_size(1024 * 1024, pool), "1.0 MiB");

    // Test GiB
    ck_assert_str_eq(format_human_size(1024LL * 1024 * 1024, pool), "1.0 GiB");

    // Test TiB
    ck_assert_str_eq(format_human_size(1024LL * 1024LL * 1024LL * 1024LL, pool), "1.0 TiB");
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_parse_human_size_valid)
{
    ck_assert_int_eq(parse_human_size("1024"), 1024);
    ck_assert_int_eq(parse_human_size("1K"), 1024);
    ck_assert_int_eq(parse_human_size("1k"), 1024);
    ck_assert_int_eq(parse_human_size("1M"), 1024 * 1024);
    ck_assert_int_eq(parse_human_size("1m"), 1024 * 1024);
    ck_assert_int_eq(parse_human_size("1G"), 1024 * 1024 * 1024);
    ck_assert_int_eq(parse_human_size("1g"), 1024 * 1024 * 1024);
    ck_assert_int_eq(parse_human_size("1T"), 1024LL * 1024LL * 1024LL * 1024LL);
    ck_assert_int_eq(parse_human_size("1t"), 1024LL * 1024LL * 1024LL * 1024LL);
    ck_assert_int_eq(parse_human_size("1.5K"), (apr_off_t) (1.5 * 1024));
    ck_assert_int_eq(parse_human_size("2.5M"), (apr_off_t) (2.5 * 1024 * 1024));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

START_TEST(test_parse_human_size_invalid)
{
    ck_assert_int_eq(parse_human_size("1Z"), -1);
    ck_assert_int_eq(parse_human_size("abc"), -1);
    ck_assert_int_eq(parse_human_size("1.5.5K"), -1);
    ck_assert_int_eq(parse_human_size(""), -1);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_human_size_suite(void)
{
    Suite *s = suite_create("HumanSize");
    TCase *tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, test_parse_human_size_valid);
    tcase_add_test(tc_core, test_parse_human_size_invalid);
    tcase_add_test(tc_core, test_format_human_size);

    suite_add_tcase(s, tc_core);

    return s;
}
