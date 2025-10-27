/**
 * @file check_cache_model.c
 * @brief Tests for napr_cache data model and structure layout
 *
 * This file contains compile-time and runtime tests to verify the
 * cache entry structure is exactly 40 bytes as required for zero-copy
 * operation.
 */

#include <check.h>
#include <stddef.h>

#include "../src/napr_cache.h"

enum
{
    TEST_CACHE_ENTRY_SIZE = 40,
    TEST_CACHE_OFFSET_MTIME = 0,
    TEST_CACHE_OFFSET_CTIME = 8,
    TEST_CACHE_OFFSET_SIZE = 16,
    TEST_CACHE_OFFSET_HASH = 24,
    TEST_CACHE_HASH_SIZE = 16,
    TEST_CACHE_APR_TIME_SIZE = 8,
    TEST_CACHE_APR_OFF_SIZE = 8
};

/* ========================================================================
 * CRITICAL: Structure Size and Layout Verification
 *
 * These compile-time assertions ensure the napr_cache_entry_t structure
 * is exactly 40 bytes. This is MANDATORY for zero-copy semantics.
 *
 * If these assertions fail, the structure padding or alignment is wrong
 * and the cache will NOT work correctly.
 * ======================================================================== */

/**
 * @test Verify napr_cache_entry_t is exactly 40 bytes
 *
 * This is a compile-time check. If this fails, compilation will stop.
 */
_Static_assert(sizeof(napr_cache_entry_t) == TEST_CACHE_ENTRY_SIZE, "napr_cache_entry_t must be exactly 40 bytes for zero-copy");

/**
 * @test Verify structure field offsets are as expected
 *
 * While the total size is the critical requirement, we also verify
 * the expected layout for documentation purposes.
 */
_Static_assert(offsetof(napr_cache_entry_t, mtime) == TEST_CACHE_OFFSET_MTIME, "mtime must be at offset 0");

_Static_assert(offsetof(napr_cache_entry_t, ctime) == TEST_CACHE_OFFSET_CTIME, "ctime must be at offset 8 (after 8-byte mtime)");

_Static_assert(offsetof(napr_cache_entry_t, size) == TEST_CACHE_OFFSET_SIZE, "size must be at offset 16 (after 8-byte ctime)");

_Static_assert(offsetof(napr_cache_entry_t, hash) == TEST_CACHE_OFFSET_HASH, "hash must be at offset 24 (after 8-byte size)");

/**
 * @test Verify XXH128_hash_t is 16 bytes
 *
 * The XXH128 hash should be 16 bytes (128 bits).
 */
_Static_assert(sizeof(XXH128_hash_t) == TEST_CACHE_HASH_SIZE, "XXH128_hash_t must be 16 bytes");

/* ========================================================================
 * Runtime Tests
 * ======================================================================== */

/**
 * @test Runtime verification of structure size
 *
 * This duplicates the compile-time check but provides a runtime test
 * that will show up in test reports.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cache_entry_size)
{
    ck_assert_msg(sizeof(napr_cache_entry_t) == TEST_CACHE_ENTRY_SIZE, "napr_cache_entry_t size is %zu, expected 40", sizeof(napr_cache_entry_t));
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @test Verify individual field sizes
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cache_entry_field_sizes)
{
    /* Verify APR types are as expected on this platform */
    ck_assert_msg(sizeof(apr_time_t) == TEST_CACHE_APR_TIME_SIZE, "apr_time_t size is %zu, expected 8", sizeof(apr_time_t));

    ck_assert_msg(sizeof(apr_off_t) == TEST_CACHE_APR_OFF_SIZE, "apr_off_t size is %zu, expected 8", sizeof(apr_off_t));

    ck_assert_msg(sizeof(XXH128_hash_t) == TEST_CACHE_HASH_SIZE, "XXH128_hash_t size is %zu, expected 16", sizeof(XXH128_hash_t));

    /* Total: 8 + 8 + 8 + 16 = 40 bytes */
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @test Verify structure is tightly packed (no padding)
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cache_entry_no_padding)
{
    size_t expected_size = sizeof(apr_time_t) + /* mtime: 8 */
        sizeof(apr_time_t) +    /* ctime: 8 */
        sizeof(apr_off_t) +     /* size: 8 */
        sizeof(XXH128_hash_t);  /* hash: 16 */

    ck_assert_msg(sizeof(napr_cache_entry_t) == expected_size, "Structure has unexpected padding: actual=%zu, expected=%zu", sizeof(napr_cache_entry_t), expected_size);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* ========================================================================
 * Test Suite Setup
 * ======================================================================== */

Suite *cache_model_suite(void)
{
    Suite *suite = NULL;
    TCase *tc_layout = NULL;

    suite = suite_create("Cache Model");

    tc_layout = tcase_create("Layout");
    tcase_add_test(tc_layout, test_cache_entry_size);
    tcase_add_test(tc_layout, test_cache_entry_field_sizes);
    tcase_add_test(tc_layout, test_cache_entry_no_padding);
    suite_add_tcase(suite, tc_layout);

    return suite;
}
