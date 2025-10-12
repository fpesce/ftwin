#include <check.h>
#include "human_size.h"

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
    ck_assert_int_eq(parse_human_size("1.5K"), (apr_off_t)(1.5 * 1024));
    ck_assert_int_eq(parse_human_size("2.5M"), (apr_off_t)(2.5 * 1024 * 1024));
}
END_TEST

START_TEST(test_parse_human_size_invalid)
{
    ck_assert_int_eq(parse_human_size("1Z"), -1);
    ck_assert_int_eq(parse_human_size("abc"), -1);
    ck_assert_int_eq(parse_human_size("1.5.5K"), -1);
    ck_assert_int_eq(parse_human_size(""), -1);
}
END_TEST

Suite *make_human_size_suite(void)
{
    Suite *s = suite_create("HumanSize");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_parse_human_size_valid);
    tcase_add_test(tc_core, test_parse_human_size_invalid);

    suite_add_tcase(s, tc_core);

    return s;
}
