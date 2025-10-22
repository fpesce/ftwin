/**
 * @file check_db_layout.c
 * @brief Structure layout validation tests for napr_db
 *
 * This file contains critical compile-time and runtime tests to verify
 * that all on-disk structures have the exact size and layout required
 * for zero-copy memory-mapped access.
 *
 * These tests MUST pass for the database to function correctly.
 */

#include <check.h>
#include <stddef.h>
#include "../src/napr_db_internal.h"

/*
 * Compile-time structure size validation
 *
 * These _Static_assert statements verify sizes at compile time.
 * If any fail, compilation will stop with an error.
 */

/* Verify basic types */
_Static_assert(sizeof(pgno_t) == 8, "pgno_t must be 8 bytes");
_Static_assert(sizeof(txnid_t) == 8, "txnid_t must be 8 bytes");

/* Verify DB_PageHeader layout */
_Static_assert(sizeof(DB_PageHeader) == 18, "DB_PageHeader must be 18 bytes");
_Static_assert(offsetof(DB_PageHeader, pgno) == 0, "pgno offset must be 0");
_Static_assert(offsetof(DB_PageHeader, flags) == 8, "flags offset must be 8");
_Static_assert(offsetof(DB_PageHeader, num_keys) == 10, "num_keys offset must be 10");
_Static_assert(offsetof(DB_PageHeader, lower) == 12, "lower offset must be 12");
_Static_assert(offsetof(DB_PageHeader, upper) == 14, "upper offset must be 14");
_Static_assert(offsetof(DB_PageHeader, padding) == 16, "padding offset must be 16");

/* Verify DB_MetaPage layout - CRITICAL: must be exactly PAGE_SIZE */
_Static_assert(sizeof(DB_MetaPage) == PAGE_SIZE, "DB_MetaPage must be exactly PAGE_SIZE (4096 bytes)");
_Static_assert(offsetof(DB_MetaPage, magic) == 0, "magic offset must be 0");
_Static_assert(offsetof(DB_MetaPage, version) == 4, "version offset must be 4");
_Static_assert(offsetof(DB_MetaPage, txnid) == 8, "txnid offset must be 8");
_Static_assert(offsetof(DB_MetaPage, root) == 16, "root offset must be 16");
_Static_assert(offsetof(DB_MetaPage, last_pgno) == 24, "last_pgno offset must be 24");

/* Verify DB_BranchNode base layout (without flexible array) */
_Static_assert(sizeof(DB_BranchNode) == 10, "DB_BranchNode base must be 10 bytes");
_Static_assert(offsetof(DB_BranchNode, pgno) == 0, "pgno offset must be 0");
_Static_assert(offsetof(DB_BranchNode, key_size) == 8, "key_size offset must be 8");
_Static_assert(offsetof(DB_BranchNode, key_data) == 10, "key_data offset must be 10");

/* Verify DB_LeafNode base layout (without flexible array) */
_Static_assert(sizeof(DB_LeafNode) == 4, "DB_LeafNode base must be 4 bytes");
_Static_assert(offsetof(DB_LeafNode, key_size) == 0, "key_size offset must be 0");
_Static_assert(offsetof(DB_LeafNode, data_size) == 2, "data_size offset must be 2");
_Static_assert(offsetof(DB_LeafNode, kv_data) == 4, "kv_data offset must be 4");

/*
 * Runtime tests - verify the same properties at runtime
 */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_basic_types)
{
    ck_assert_uint_eq(sizeof(pgno_t), 8);
    ck_assert_uint_eq(sizeof(txnid_t), 8);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_page_header_layout)
{
    ck_assert_uint_eq(sizeof(DB_PageHeader), 18);
    ck_assert_uint_eq(offsetof(DB_PageHeader, pgno), 0);
    ck_assert_uint_eq(offsetof(DB_PageHeader, flags), 8);
    ck_assert_uint_eq(offsetof(DB_PageHeader, num_keys), 10);
    ck_assert_uint_eq(offsetof(DB_PageHeader, lower), 12);
    ck_assert_uint_eq(offsetof(DB_PageHeader, upper), 14);
    ck_assert_uint_eq(offsetof(DB_PageHeader, padding), 16);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_meta_page_layout)
{
    /* CRITICAL: Meta page must be exactly PAGE_SIZE for direct mapping */
    ck_assert_uint_eq(sizeof(DB_MetaPage), PAGE_SIZE);
    ck_assert_uint_eq(sizeof(DB_MetaPage), 4096);

    ck_assert_uint_eq(offsetof(DB_MetaPage, magic), 0);
    ck_assert_uint_eq(offsetof(DB_MetaPage, version), 4);
    ck_assert_uint_eq(offsetof(DB_MetaPage, txnid), 8);
    ck_assert_uint_eq(offsetof(DB_MetaPage, root), 16);
    ck_assert_uint_eq(offsetof(DB_MetaPage, last_pgno), 24);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_branch_node_layout)
{
    ck_assert_uint_eq(sizeof(DB_BranchNode), 10);
    ck_assert_uint_eq(offsetof(DB_BranchNode, pgno), 0);
    ck_assert_uint_eq(offsetof(DB_BranchNode, key_size), 8);
    ck_assert_uint_eq(offsetof(DB_BranchNode, key_data), 10);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_leaf_node_layout)
{
    ck_assert_uint_eq(sizeof(DB_LeafNode), 4);
    ck_assert_uint_eq(offsetof(DB_LeafNode, key_size), 0);
    ck_assert_uint_eq(offsetof(DB_LeafNode, data_size), 2);
    ck_assert_uint_eq(offsetof(DB_LeafNode, kv_data), 4);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_constants)
{
    ck_assert_uint_eq(PAGE_SIZE, 4096);
    ck_assert_uint_eq(DB_MAGIC, 0xDECAFBAD);
    ck_assert_uint_eq(DB_VERSION, 1);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_page_flags)
{
    /* Verify flags are distinct */
    ck_assert_uint_eq(P_BRANCH, 0x01);
    ck_assert_uint_eq(P_LEAF, 0x02);
    ck_assert_uint_eq(P_OVERFLOW, 0x04);
    ck_assert_uint_eq(P_FREE, 0x08);

    /* Verify no overlap */
    ck_assert((P_BRANCH & P_LEAF) == 0);
    ck_assert((P_BRANCH & P_OVERFLOW) == 0);
    ck_assert((P_BRANCH & P_FREE) == 0);
    ck_assert((P_LEAF & P_OVERFLOW) == 0);
    ck_assert((P_LEAF & P_FREE) == 0);
    ck_assert((P_OVERFLOW & P_FREE) == 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test suite setup
 */
Suite *make_db_layout_suite(void)
{
    Suite *suite;
    TCase *tc_core;

    suite = suite_create("DB Layout");

    /* Core test case */
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_basic_types);
    tcase_add_test(tc_core, test_page_header_layout);
    tcase_add_test(tc_core, test_meta_page_layout);
    tcase_add_test(tc_core, test_branch_node_layout);
    tcase_add_test(tc_core, test_leaf_node_layout);
    tcase_add_test(tc_core, test_constants);
    tcase_add_test(tc_core, test_page_flags);

    suite_add_tcase(suite, tc_core);

    return suite;
}
