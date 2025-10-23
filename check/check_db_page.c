/**
 * @file check_db_page.c
 * @brief Tests for B+ Tree page navigation and search operations
 *
 * Tests the slotted page accessors and binary search functionality.
 * Creates mock pages in memory to verify correct behavior.
 */

#include <check.h>
#include <apr.h>
#include <apr_pools.h>
#include <string.h>
#include "../src/napr_db_internal.h"
#include "check_db_constants.h"

/* Test fixtures */
/*
 * Managed in setup/teardown.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *test_pool = NULL;

static void setup(void)
{
    apr_initialize();
    apr_pool_create(&test_pool, NULL);
}

static void teardown(void)
{
    if (test_pool) {
        apr_pool_destroy(test_pool);
        test_pool = NULL;
    }
}

/**
 * @brief Helper to create a mock leaf page with sorted keys.
 *
 * Creates a leaf page with the following keys: "aaa", "bbb", "ccc"
 * Each key has a simple value: "val1", "val2", "val3"
 *
 * Layout:
 * [PageHeader][slot0][slot1][slot2][...free...][ node2 | node1 | node0 ]
 *
 * @param buffer Buffer to store the page (must be PAGE_SIZE bytes)
 * @return Pointer to the page header
 */
static DB_PageHeader *create_mock_leaf_page(uint8_t *buffer)
{
    DB_PageHeader *page = (DB_PageHeader *) buffer;
    uint16_t *slots = NULL;
    uint16_t offset = 0;

    /* Initialize page header */
    memset(buffer, 0, PAGE_SIZE);
    page->pgno = 1;
    page->flags = P_LEAF;
    page->num_keys = 3;
    page->lower = (uint16_t) (sizeof(DB_PageHeader) + 3 * sizeof(uint16_t));    /* Header + 3 slots */
    page->upper = PAGE_SIZE;    /* Will be adjusted as we add nodes */
    page->padding = 0;

    slots = db_page_slots(page);

    /* Add nodes from end of page, working backwards */
    /* Node 2: key="ccc", value="val3" */
    offset = PAGE_SIZE - (sizeof(DB_LeafNode) + 3 + 4); /* 3 bytes key + 4 bytes value */
    page->upper = offset;
    slots[2] = offset;
    {
        DB_LeafNode *node = (DB_LeafNode *) (buffer + offset);
        node->key_size = 3;
        node->data_size = 4;
        memcpy(node->kv_data, "ccc", 3);
        memcpy(node->kv_data + 3, "val3", 4);
    }

    /* Node 1: key="bbb", value="val2" */
    offset = page->upper - (sizeof(DB_LeafNode) + 3 + 4);
    page->upper = offset;
    slots[1] = offset;
    {
        DB_LeafNode *node = (DB_LeafNode *) (buffer + offset);
        node->key_size = 3;
        node->data_size = 4;
        memcpy(node->kv_data, "bbb", 3);
        memcpy(node->kv_data + 3, "val2", 4);
    }

    /* Node 0: key="aaa", value="val1" */
    offset = page->upper - (sizeof(DB_LeafNode) + 3 + 4);
    page->upper = offset;
    slots[0] = offset;
    {
        DB_LeafNode *node = (DB_LeafNode *) (buffer + offset);
        node->key_size = 3;
        node->data_size = 4;
        memcpy(node->kv_data, "aaa", 3);
        memcpy(node->kv_data + 3, "val1", 4);
    }

    return page;
}

/**
 * @brief Helper to create a mock branch page with sorted keys.
 *
 * Creates a branch page with the following keys: "key1", "key2", "key3"
 * With child page numbers: 10, 20, 30
 *
 * @param buffer Buffer to store the page (must be PAGE_SIZE bytes)
 * @return Pointer to the page header
 */
static DB_PageHeader *create_mock_branch_page(uint8_t *buffer)
{
    DB_PageHeader *page = (DB_PageHeader *) buffer;
    uint16_t *slots = NULL;
    uint16_t offset = 0;

    /* Initialize page header */
    memset(buffer, 0, PAGE_SIZE);
    page->pgno = 2;
    page->flags = P_BRANCH;
    page->num_keys = 3;
    page->lower = (uint16_t) (sizeof(DB_PageHeader) + 3 * sizeof(uint16_t));
    page->upper = PAGE_SIZE;
    page->padding = 0;

    slots = db_page_slots(page);

    /* Add nodes from end of page, working backwards */
    /* Node 2: pgno=30, key="key3" */
    offset = PAGE_SIZE - (sizeof(DB_BranchNode) + 4);   /* 4 bytes key */
    page->upper = offset;
    slots[2] = offset;
    {
        DB_BranchNode *node = (DB_BranchNode *) (buffer + offset);
        node->pgno = TEST_PAGE_NO_30;
        node->key_size = 4;
        memcpy(node->key_data, "key3", 4);
    }

    /* Node 1: pgno=20, key="key2" */
    offset = page->upper - (sizeof(DB_BranchNode) + 4);
    page->upper = offset;
    slots[1] = offset;
    {
        DB_BranchNode *node = (DB_BranchNode *) (buffer + offset);
        node->pgno = TEST_PAGE_NO_20;
        node->key_size = 4;
        memcpy(node->key_data, "key2", 4);
    }

    /* Node 0: pgno=10, key="key1" */
    offset = page->upper - (sizeof(DB_BranchNode) + 4);
    page->upper = offset;
    slots[0] = offset;
    {
        DB_BranchNode *node = (DB_BranchNode *) (buffer + offset);
        node->pgno = TEST_PAGE_NO_10;
        node->key_size = 4;
        memcpy(node->key_data, "key1", 4);
    }

    return page;
}

/*
 * Test: Page accessor functions for leaf pages
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_leaf_page_accessors)
{
    uint8_t buffer[PAGE_SIZE] = { 0 };
    DB_PageHeader *page = NULL;
    DB_LeafNode *node = NULL;
    uint8_t *key_ptr = NULL;
    uint8_t *val_ptr = NULL;

    /* Create mock leaf page */
    page = create_mock_leaf_page(buffer);

    /* Test page header */
    ck_assert_uint_eq(page->pgno, 1);
    ck_assert_uint_eq(page->flags, P_LEAF);
    ck_assert_uint_eq(page->num_keys, 3);

    /* Test node 0: key="aaa", value="val1" */
    node = db_page_leaf_node(page, 0);
    ck_assert_ptr_nonnull(node);
    ck_assert_uint_eq(node->key_size, 3);
    ck_assert_uint_eq(node->data_size, 4);

    key_ptr = db_leaf_node_key(node);
    ck_assert_int_eq(memcmp(key_ptr, "aaa", 3), 0);

    val_ptr = db_leaf_node_value(node);
    ck_assert_int_eq(memcmp(val_ptr, "val1", 4), 0);

    /* Test node 1: key="bbb", value="val2" */
    node = db_page_leaf_node(page, 1);
    ck_assert_uint_eq(node->key_size, 3);
    ck_assert_uint_eq(node->data_size, 4);

    key_ptr = db_leaf_node_key(node);
    ck_assert_int_eq(memcmp(key_ptr, "bbb", 3), 0);

    val_ptr = db_leaf_node_value(node);
    ck_assert_int_eq(memcmp(val_ptr, "val2", 4), 0);

    /* Test node 2: key="ccc", value="val3" */
    node = db_page_leaf_node(page, 2);
    ck_assert_uint_eq(node->key_size, 3);
    ck_assert_uint_eq(node->data_size, 4);

    key_ptr = db_leaf_node_key(node);
    ck_assert_int_eq(memcmp(key_ptr, "ccc", 3), 0);

    val_ptr = db_leaf_node_value(node);
    ck_assert_int_eq(memcmp(val_ptr, "val3", 4), 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Page accessor functions for branch pages
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_branch_page_accessors)
{
    uint8_t buffer[PAGE_SIZE] = { 0 };
    DB_PageHeader *page = NULL;
    DB_BranchNode *node = NULL;
    uint8_t *key_ptr = NULL;

    /* Create mock branch page */
    page = create_mock_branch_page(buffer);

    /* Test page header */
    ck_assert_uint_eq(page->pgno, 2);
    ck_assert_uint_eq(page->flags, P_BRANCH);
    ck_assert_uint_eq(page->num_keys, 3);

    /* Test node 0: pgno=10, key="key1" */
    node = db_page_branch_node(page, 0);
    ck_assert_ptr_nonnull(node);
    ck_assert_uint_eq(node->pgno, TEST_PAGE_NO_10);
    ck_assert_uint_eq(node->key_size, 4);

    key_ptr = db_branch_node_key(node);
    ck_assert_int_eq(memcmp(key_ptr, "key1", 4), 0);

    /* Test node 1: pgno=20, key="key2" */
    node = db_page_branch_node(page, 1);
    ck_assert_uint_eq(node->pgno, TEST_PAGE_NO_20);
    ck_assert_uint_eq(node->key_size, 4);

    key_ptr = db_branch_node_key(node);
    ck_assert_int_eq(memcmp(key_ptr, "key2", 4), 0);

    /* Test node 2: pgno=30, key="key3" */
    node = db_page_branch_node(page, 2);
    ck_assert_uint_eq(node->pgno, TEST_PAGE_NO_30);
    ck_assert_uint_eq(node->key_size, 4);

    key_ptr = db_branch_node_key(node);
    ck_assert_int_eq(memcmp(key_ptr, "key3", 4), 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Search for existing keys in leaf page
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_leaf_search_existing)
{
    uint8_t buffer[PAGE_SIZE] = { 0 };
    DB_PageHeader *page = create_mock_leaf_page(buffer);
    apr_status_t status = 0;
    uint16_t index = 0;
    napr_db_val_t key = { 0 };

    /* Search for "aaa" - should be at index 0 */
    key.data = (uint8_t *) "aaa";
    key.size = 3;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(index, 0);

    /* Search for "bbb" - should be at index 1 */
    key.data = (uint8_t *) "bbb";
    key.size = 3;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(index, 1);

    /* Search for "ccc" - should be at index 2 */
    key.data = (uint8_t *) "ccc";
    key.size = 3;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(index, 2);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Search for non-existing keys in leaf page (insertion points)
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_leaf_search_insertion_points)
{
    uint8_t buffer[PAGE_SIZE] = { 0 };
    DB_PageHeader *page = create_mock_leaf_page(buffer);
    apr_status_t status = 0;
    uint16_t index = 0;
    napr_db_val_t key = { 0 };

    /* Search for "000" - before first key, insertion point should be 0 */
    key.data = (uint8_t *) "000";
    key.size = 3;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_NOTFOUND);
    ck_assert_uint_eq(index, 0);

    /* Search for "abc" - between "aaa" and "bbb", insertion point should be 1 */
    key.data = (uint8_t *) "abc";
    key.size = 3;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_NOTFOUND);
    ck_assert_uint_eq(index, 1);

    /* Search for "bcd" - between "bbb" and "ccc", insertion point should be 2 */
    key.data = (uint8_t *) "bcd";
    key.size = 3;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_NOTFOUND);
    ck_assert_uint_eq(index, 2);

    /* Search for "zzz" - after last key, insertion point should be 3 */
    key.data = (uint8_t *) "zzz";
    key.size = 3;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_NOTFOUND);
    ck_assert_uint_eq(index, 3);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Search in branch page
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_branch_search)
{
    uint8_t buffer[PAGE_SIZE] = { 0 };
    DB_PageHeader *page = create_mock_branch_page(buffer);
    apr_status_t status = 0;
    uint16_t index = 0;
    napr_db_val_t key = { 0 };

    /* Search for "key1" - should be at index 0 */
    key.data = (uint8_t *) "key1";
    key.size = 4;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(index, 0);

    /* Search for "key2" - should be at index 1 */
    key.data = (uint8_t *) "key2";
    key.size = 4;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(index, 1);

    /* Search for non-existing key "key0" - insertion point should be 0 */
    key.data = (uint8_t *) "key0";
    key.size = 4;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_NOTFOUND);
    ck_assert_uint_eq(index, 0);

    /* Search for non-existing key "key9" - insertion point should be 3 */
    key.data = (uint8_t *) "key9";
    key.size = 4;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_NOTFOUND);
    ck_assert_uint_eq(index, 3);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Search in empty page
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_empty_page_search)
{
    uint8_t buffer[PAGE_SIZE] = { 0 };
    DB_PageHeader *page = (DB_PageHeader *) buffer;
    apr_status_t status = 0;
    uint16_t index = 0;
    napr_db_val_t key = { 0 };

    /* Create empty leaf page */
    memset(buffer, 0, PAGE_SIZE);
    page->pgno = 1;
    page->flags = P_LEAF;
    page->num_keys = 0;
    page->lower = (uint16_t) sizeof(DB_PageHeader);
    page->upper = PAGE_SIZE;

    /* Search in empty page - should return insertion point 0 */
    key.data = (uint8_t *) "test";
    key.size = 4;
    status = db_page_search(page, &key, &index);
    ck_assert_int_eq(status, APR_NOTFOUND);
    ck_assert_uint_eq(index, 0);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test suite setup
 */
Suite *make_db_page_suite(void)
{
    Suite *suite = NULL;
    TCase *tc_core = NULL;

    suite = suite_create("DB Page");

    /* Core test case */
    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, test_leaf_page_accessors);
    tcase_add_test(tc_core, test_branch_page_accessors);
    tcase_add_test(tc_core, test_leaf_search_existing);
    tcase_add_test(tc_core, test_leaf_search_insertion_points);
    tcase_add_test(tc_core, test_branch_search);
    tcase_add_test(tc_core, test_empty_page_search);

    suite_add_tcase(suite, tc_core);

    return suite;
}
