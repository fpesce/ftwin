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
_Static_assert(sizeof(pgno_t) == PGNO_T_SIZE, "pgno_t must be 8 bytes");
_Static_assert(sizeof(txnid_t) == TXNID_T_SIZE, "txnid_t must be 8 bytes");

/* Verify DB_PageHeader layout */
_Static_assert(sizeof(DB_PageHeader) == DB_PAGEHEADER_SIZE, "DB_PageHeader must be 18 bytes");
_Static_assert(offsetof(DB_PageHeader, pgno) == DB_PAGEHEADER_PGNO_OFFSET, "pgno offset must be 0");
_Static_assert(offsetof(DB_PageHeader, flags) == DB_PAGEHEADER_FLAGS_OFFSET, "flags offset must be 8");
_Static_assert(offsetof(DB_PageHeader, num_keys) == DB_PAGEHEADER_NUM_KEYS_OFFSET, "num_keys offset must be 10");
_Static_assert(offsetof(DB_PageHeader, lower) == DB_PAGEHEADER_LOWER_OFFSET, "lower offset must be 12");
_Static_assert(offsetof(DB_PageHeader, upper) == DB_PAGEHEADER_UPPER_OFFSET, "upper offset must be 14");
_Static_assert(offsetof(DB_PageHeader, padding) == DB_PAGEHEADER_PADDING_OFFSET, "padding offset must be 16");

/* Verify DB_MetaPage layout - CRITICAL: must be exactly PAGE_SIZE */
_Static_assert(sizeof(DB_MetaPage) == DB_METAPAGE_SIZE, "DB_MetaPage must be exactly PAGE_SIZE (4096 bytes)");
_Static_assert(offsetof(DB_MetaPage, magic) == DB_METAPAGE_MAGIC_OFFSET, "magic offset must be 0");
_Static_assert(offsetof(DB_MetaPage, version) == DB_METAPAGE_VERSION_OFFSET, "version offset must be 4");
_Static_assert(offsetof(DB_MetaPage, txnid) == DB_METAPAGE_TXNID_OFFSET, "txnid offset must be 8");
_Static_assert(offsetof(DB_MetaPage, root) == DB_METAPAGE_ROOT_OFFSET, "root offset must be 16");
_Static_assert(offsetof(DB_MetaPage, last_pgno) == DB_METAPAGE_LAST_PGNO_OFFSET, "last_pgno offset must be 24");

/* Verify DB_BranchNode base layout (without flexible array) */
_Static_assert(sizeof(DB_BranchNode) == DB_BRANCHNODE_BASE_SIZE, "DB_BranchNode base must be 10 bytes");
_Static_assert(offsetof(DB_BranchNode, pgno) == DB_BRANCHNODE_PGNO_OFFSET, "pgno offset must be 0");
_Static_assert(offsetof(DB_BranchNode, key_size) == DB_BRANCHNODE_KEY_SIZE_OFFSET, "key_size offset must be 8");
_Static_assert(offsetof(DB_BranchNode, key_data) == DB_BRANCHNODE_KEY_DATA_OFFSET, "key_data offset must be 10");

/* Verify DB_LeafNode base layout (without flexible array) */
_Static_assert(sizeof(DB_LeafNode) == DB_LEAFNODE_BASE_SIZE, "DB_LeafNode base must be 4 bytes");
_Static_assert(offsetof(DB_LeafNode, key_size) == DB_LEAFNODE_KEY_SIZE_OFFSET, "key_size offset must be 0");
_Static_assert(offsetof(DB_LeafNode, data_size) == DB_LEAFNODE_DATA_SIZE_OFFSET, "data_size offset must be 2");
_Static_assert(offsetof(DB_LeafNode, kv_data) == DB_LEAFNODE_KV_DATA_OFFSET, "kv_data offset must be 4");

/*
 * Runtime tests - verify the same properties at runtime
 */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_basic_types)
{
    ck_assert_uint_eq(sizeof(pgno_t), PGNO_T_SIZE);
    ck_assert_uint_eq(sizeof(txnid_t), TXNID_T_SIZE);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_page_header_layout)
{
    ck_assert_uint_eq(sizeof(DB_PageHeader), DB_PAGEHEADER_SIZE);
    ck_assert_uint_eq(offsetof(DB_PageHeader, pgno), DB_PAGEHEADER_PGNO_OFFSET);
    ck_assert_uint_eq(offsetof(DB_PageHeader, flags), DB_PAGEHEADER_FLAGS_OFFSET);
    ck_assert_uint_eq(offsetof(DB_PageHeader, num_keys), DB_PAGEHEADER_NUM_KEYS_OFFSET);
    ck_assert_uint_eq(offsetof(DB_PageHeader, lower), DB_PAGEHEADER_LOWER_OFFSET);
    ck_assert_uint_eq(offsetof(DB_PageHeader, upper), DB_PAGEHEADER_UPPER_OFFSET);
    ck_assert_uint_eq(offsetof(DB_PageHeader, padding), DB_PAGEHEADER_PADDING_OFFSET);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_meta_page_layout)
{
    /* CRITICAL: Meta page must be exactly PAGE_SIZE for direct mapping */
    ck_assert_uint_eq(sizeof(DB_MetaPage), DB_METAPAGE_SIZE);
    ck_assert_uint_eq(sizeof(DB_MetaPage), PAGE_SIZE);

    ck_assert_uint_eq(offsetof(DB_MetaPage, magic), DB_METAPAGE_MAGIC_OFFSET);
    ck_assert_uint_eq(offsetof(DB_MetaPage, version), DB_METAPAGE_VERSION_OFFSET);
    ck_assert_uint_eq(offsetof(DB_MetaPage, txnid), DB_METAPAGE_TXNID_OFFSET);
    ck_assert_uint_eq(offsetof(DB_MetaPage, root), DB_METAPAGE_ROOT_OFFSET);
    ck_assert_uint_eq(offsetof(DB_MetaPage, last_pgno), DB_METAPAGE_LAST_PGNO_OFFSET);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_branch_node_layout)
{
    ck_assert_uint_eq(sizeof(DB_BranchNode), DB_BRANCHNODE_BASE_SIZE);
    ck_assert_uint_eq(offsetof(DB_BranchNode, pgno), DB_BRANCHNODE_PGNO_OFFSET);
    ck_assert_uint_eq(offsetof(DB_BranchNode, key_size), DB_BRANCHNODE_KEY_SIZE_OFFSET);
    ck_assert_uint_eq(offsetof(DB_BranchNode, key_data), DB_BRANCHNODE_KEY_DATA_OFFSET);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_leaf_node_layout)
{
    ck_assert_uint_eq(sizeof(DB_LeafNode), DB_LEAFNODE_BASE_SIZE);
    ck_assert_uint_eq(offsetof(DB_LeafNode, key_size), DB_LEAFNODE_KEY_SIZE_OFFSET);
    ck_assert_uint_eq(offsetof(DB_LeafNode, data_size), DB_LEAFNODE_DATA_SIZE_OFFSET);
    ck_assert_uint_eq(offsetof(DB_LeafNode, kv_data), DB_LEAFNODE_KV_DATA_OFFSET);
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
