#include <check.h>
#include <apr_pools.h>
#include "human_size.h"

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *pool = NULL;

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

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_format_human_size)
{
    const long long KIB = 1024;
    const long long MIB = KIB * 1024;
    const long long GIB = MIB * 1024;
    const long long TIB = GIB * 1024;

    // Test Bytes
    ck_assert_str_eq(format_human_size(0, pool), "0 B");
    ck_assert_str_eq(format_human_size(512, pool), "512 B");
    ck_assert_str_eq(format_human_size(1023, pool), "1023 B");

    // Test KiB
    ck_assert_str_eq(format_human_size(KIB, pool), "1.0 KiB");
    ck_assert_str_eq(format_human_size(1536, pool), "1.5 KiB");

    // Test MiB
    ck_assert_str_eq(format_human_size(MIB, pool), "1.0 MiB");

    // Test GiB
    ck_assert_str_eq(format_human_size(GIB, pool), "1.0 GiB");

    // Test TiB
    ck_assert_str_eq(format_human_size(TIB, pool), "1.0 TiB");
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_parse_human_size_valid)
{
    const long long KIB = 1024;
    const long long MIB = KIB * 1024;
    const long long GIB = MIB * 1024;
    const long long TIB = GIB * 1024;

    ck_assert_int_eq(parse_human_size("1024"), KIB);
    ck_assert_int_eq(parse_human_size("1K"), KIB);
    ck_assert_int_eq(parse_human_size("1k"), KIB);
    ck_assert_int_eq(parse_human_size("1M"), MIB);
    ck_assert_int_eq(parse_human_size("1m"), MIB);
    ck_assert_int_eq(parse_human_size("1G"), GIB);
    ck_assert_int_eq(parse_human_size("1g"), GIB);
    ck_assert_int_eq(parse_human_size("1T"), TIB);
    ck_assert_int_eq(parse_human_size("1t"), TIB);
    ck_assert_int_eq(parse_human_size("1.5K"), (apr_off_t) (1.5 * KIB));
    ck_assert_int_eq(parse_human_size("2.5M"), (apr_off_t) (2.5 * MIB));
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_parse_human_size_invalid)
{
    const int error_code = -1;
    ck_assert_int_eq(parse_human_size("1Z"), error_code);
    ck_assert_int_eq(parse_human_size("abc"), error_code);
    ck_assert_int_eq(parse_human_size("1.5.5K"), error_code);
    ck_assert_int_eq(parse_human_size(""), error_code);
}

/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *make_human_size_suite(void)
{
    Suite *suite = suite_create("HumanSize");
    TCase *tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, test_parse_human_size_valid);
    tcase_add_test(tc_core, test_parse_human_size_invalid);
    tcase_add_test(tc_core, test_format_human_size);

    suite_add_tcase(suite, tc_core);

    return suite;
}
