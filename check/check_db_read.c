/**
 * @file check_db_read.c
 * @brief Tests for napr_db tree traversal and get operations
 *
 * These tests manually construct a B+ tree structure to test
 * the read path without requiring write functionality.
 */

#include "../src/napr_db_internal.h"
#include "check_db_constants.h"
#include <check.h>
#include <apr_file_io.h>
#include <string.h>
#include <stdlib.h>

/*
 * Managed in setup/teardown.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *test_pool = NULL;

/**
 * @brief Setup function - creates APR pool and removes old test files
 */
static void setup(void)
{
    apr_pool_create(&test_pool, NULL);
    apr_file_remove(DB_TEST_PATH_READ, test_pool);
}

/**
 * @brief Teardown function - destroys APR pool and cleans up test files
 */
static void teardown(void)
{
    if (test_pool) {
        apr_file_remove(DB_TEST_PATH_READ, test_pool);
        apr_pool_destroy(test_pool);
        test_pool = NULL;
    }
}

/**
 * @brief Helper to manually construct a leaf page with given key-value pairs
 *
 * @param page_buffer Buffer of PAGE_SIZE bytes
 * @param pgno Page number for this page
 * @param entries Array of key-value pairs (key, value alternating)
 * @param num_entries Number of key-value pairs
 */
static void construct_leaf_page(uint8_t *page_buffer, pgno_t pgno, const char **entries, int num_entries)
{
    DB_PageHeader *page = (DB_PageHeader *) page_buffer;
    memset(page_buffer, 0, PAGE_SIZE);

    /* Initialize page header */
    page->pgno = pgno;
    page->flags = P_LEAF;
    page->num_keys = num_entries;
    page->lower = sizeof(DB_PageHeader) + (num_entries * sizeof(uint16_t));
    page->upper = PAGE_SIZE;

    uint16_t *slots = db_page_slots(page);
    uint16_t current_offset = PAGE_SIZE;

    /* Add entries from the end of the page, working backwards */
    for (int i = num_entries - 1; i >= 0; i--) {
        const char *key = entries[(size_t) i * 2];
        const char *value = entries[(size_t) i * 2 + 1];
        uint16_t key_size = strlen(key);
        uint16_t value_size = strlen(value);

        /* Calculate node size */
        uint16_t node_size = sizeof(DB_LeafNode) + key_size + value_size;

        /* Move offset backwards */
        current_offset -= node_size;

        /* Create the node */
        DB_LeafNode *node = (DB_LeafNode *) (page_buffer + current_offset);
        node->key_size = key_size;
        node->data_size = value_size;
        memcpy(node->kv_data, key, key_size);
        memcpy(node->kv_data + key_size, value, value_size);

        /* Set the slot */
        slots[i] = current_offset;
    }

    page->upper = current_offset;
}

/**
 * @brief Helper to manually construct a branch page
 *
 * @param page_buffer Buffer of PAGE_SIZE bytes
 * @param pgno Page number for this page
 * @param entries Array of (child_pgno_as_string, key) pairs
 * @param num_entries Number of entries
 */
static void construct_branch_page(uint8_t *page_buffer, pgno_t pgno, const char **entries, int num_entries)
{
    DB_PageHeader *page = (DB_PageHeader *) page_buffer;
    memset(page_buffer, 0, PAGE_SIZE);

    /* Initialize page header */
    page->pgno = pgno;
    page->flags = P_BRANCH;
    page->num_keys = num_entries;
    page->lower = sizeof(DB_PageHeader) + (num_entries * sizeof(uint16_t));
    page->upper = PAGE_SIZE;

    uint16_t *slots = db_page_slots(page);
    uint16_t current_offset = PAGE_SIZE;

    /* Add entries from the end of the page, working backwards */
    for (int i = num_entries - 1; i >= 0; i--) {
        pgno_t child_pgno = (pgno_t) strtol(entries[(size_t) i * 2], NULL, DB_TEST_DECIMAL_BASE);
        const char *key = entries[(size_t) i * 2 + 1];
        uint16_t key_size = strlen(key);

        /* Calculate node size */
        uint16_t node_size = sizeof(DB_BranchNode) + key_size;

        /* Move offset backwards */
        current_offset -= node_size;

        /* Create the node */
        DB_BranchNode *node = (DB_BranchNode *) (page_buffer + current_offset);
        node->pgno = child_pgno;
        node->key_size = key_size;
        memcpy(node->key_data, key, key_size);

        /* Set the slot */
        slots[i] = current_offset;
    }

    page->upper = current_offset;
}

/**
 * @brief Test napr_db_get with a single leaf page tree
 *
 * Creates a tree with just one leaf page (root is leaf).
 * Tests successful retrieval and miss cases.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_get_single_leaf)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    napr_db_val_t key;
    napr_db_val_t data;

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_1MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_READ, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /*
     * Extend the file to accommodate our test pages.
     * We need to extend the file manually because the database
     * is initially created with only 2 meta pages.
     */
    apr_off_t new_size = (apr_off_t) DB_TEST_PAGE_COUNT_5 * PAGE_SIZE;        /* Pages 0-4 */
    status = apr_file_trunc(env->file, new_size);
    ck_assert_int_eq(status, APR_SUCCESS);

    /*
     * Manually construct a simple tree:
     * Root (page 2) is a leaf with keys: "apple", "banana", "cherry"
     */
    uint8_t *page2 = (uint8_t *) env->map_addr + ((size_t) 2 * PAGE_SIZE);
    const char *leaf_entries[] = {
        "apple", "red",
        "banana", "yellow",
        "cherry", "red"
    };
    construct_leaf_page(page2, 2, leaf_entries, 3);

    /* Update meta page to point to our root */
    env->live_meta->root = 2;
    env->live_meta->last_pgno = 2;

    /* Begin a read transaction */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(txn);

    /* Test 1: Retrieve "apple" */
    key.data = (uint8_t *) "apple";
    key.size = strlen("apple");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(data.size, strlen("red"));
    ck_assert_int_eq(memcmp(data.data, "red", data.size), 0);

    /* Test 2: Retrieve "banana" */
    key.data = (uint8_t *) "banana";
    key.size = strlen("banana");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(data.size, strlen("yellow"));
    ck_assert_int_eq(memcmp(data.data, "yellow", data.size), 0);

    /* Test 3: Retrieve "cherry" */
    key.data = (uint8_t *) "cherry";
    key.size = strlen("cherry");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(data.size, strlen("red"));
    ck_assert_int_eq(memcmp(data.data, "red", data.size), 0);

    /* Test 4: Try to retrieve non-existent key "grape" */
    key.data = (uint8_t *) "grape";
    key.size = strlen("grape");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_NOTFOUND);

    /* Test 5: Try to retrieve key before first "aaa" */
    key.data = (uint8_t *) "aaa";
    key.size = strlen("aaa");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_NOTFOUND);

    /* Test 6: Try to retrieve key after last "zzz" */
    key.data = (uint8_t *) "zzz";
    key.size = strlen("zzz");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_NOTFOUND);

    /* Commit transaction */
    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Close environment */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Test napr_db_get with a two-level tree (branch + leaves)
 *
 * Creates a tree with:
 * - Root (page 2): Branch with 2 entries pointing to leaves
 * - Leaf 1 (page 3): Contains "apple", "banana"
 * - Leaf 2 (page 4): Contains "cherry", "date", "elderberry"
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_get_two_level_tree)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    napr_db_val_t key;
    napr_db_val_t data;

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_1MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_READ, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /*
     * Extend the file to accommodate our test pages.
     */
    apr_off_t new_size = (apr_off_t) DB_TEST_PAGE_COUNT_5 * PAGE_SIZE;        /* Pages 0-4 */
    status = apr_file_trunc(env->file, new_size);
    ck_assert_int_eq(status, APR_SUCCESS);

    /*
     * Construct a two-level tree:
     *
     * Root (page 2, branch):
     *   Entry 0: pgno=3, key="apple" (child 3 contains keys >= "apple")
     *   Entry 1: pgno=4, key="cherry" (child 4 contains keys >= "cherry")
     *
     * Leaf 1 (page 3): "apple" -> "red", "banana" -> "yellow"
     * Leaf 2 (page 4): "cherry" -> "red", "date" -> "brown", "elderberry" -> "black"
     *
     * Search logic:
     * - "apple": exact match at index 0 → child 3 ✓
     * - "banana": insertion point 1, decrement to 0 → child 3 ✓
     * - "cherry": exact match at index 1 → child 4 ✓
     * - "date": insertion point 2, decrement to 1 → child 4 ✓
     */

    uint8_t *page3 = (uint8_t *) env->map_addr + ((size_t) 3 * PAGE_SIZE);
    const char *leaf1_entries[] = {
        "apple", "red",
        "banana", "yellow"
    };
    construct_leaf_page(page3, 3, leaf1_entries, 2);

    uint8_t *page4 = (uint8_t *) env->map_addr + ((size_t) 4 * PAGE_SIZE);
    const char *leaf2_entries[] = {
        "cherry", "red",
        "date", "brown",
        "elderberry", "black"
    };
    construct_leaf_page(page4, 4, leaf2_entries, 3);

    uint8_t *page2 = (uint8_t *) env->map_addr + ((size_t) 2 * PAGE_SIZE);
    const char *branch_entries[] = {
        "3", "apple",           /* Child 3: contains keys >= "apple" */
        "4", "cherry"           /* Child 4: contains keys >= "cherry" */
    };
    construct_branch_page(page2, 2, branch_entries, 2);

    /* Update meta page to point to our root */
    env->live_meta->root = 2;
    env->live_meta->last_pgno = 4;

    /* Begin a read transaction */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(txn);

    /* Test 1: Retrieve "apple" from leaf 1 */
    key.data = (uint8_t *) "apple";
    key.size = strlen("apple");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(data.size, strlen("red"));
    ck_assert_int_eq(memcmp(data.data, "red", data.size), 0);

    /* Test 2: Retrieve "banana" from leaf 1 */
    key.data = (uint8_t *) "banana";
    key.size = strlen("banana");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(data.size, strlen("yellow"));
    ck_assert_int_eq(memcmp(data.data, "yellow", data.size), 0);

    /* Test 3: Retrieve "cherry" from leaf 2 */
    key.data = (uint8_t *) "cherry";
    key.size = strlen("cherry");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(data.size, strlen("red"));
    ck_assert_int_eq(memcmp(data.data, "red", data.size), 0);

    /* Test 4: Retrieve "date" from leaf 2 */
    key.data = (uint8_t *) "date";
    key.size = strlen("date");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(data.size, strlen("brown"));
    ck_assert_int_eq(memcmp(data.data, "brown", data.size), 0);

    /* Test 5: Retrieve "elderberry" from leaf 2 */
    key.data = (uint8_t *) "elderberry";
    key.size = strlen("elderberry");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_uint_eq(data.size, strlen("black"));
    ck_assert_int_eq(memcmp(data.data, "black", data.size), 0);

    /* Test 6: Try to retrieve non-existent key "fig" */
    key.data = (uint8_t *) "fig";
    key.size = strlen("fig");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_NOTFOUND);

    /* Test 7: Try to retrieve non-existent key "aaa" (before all keys) */
    key.data = (uint8_t *) "aaa";
    key.size = strlen("aaa");
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_NOTFOUND);

    /* Commit transaction */
    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Close environment */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Create the test suite for DB read operations
 */
Suite *make_db_read_suite(void)
{
    Suite *suite = suite_create("DB_READ");

    /* Test case for single leaf tree */
    TCase *tc_single = tcase_create("SingleLeaf");
    tcase_add_checked_fixture(tc_single, setup, teardown);
    tcase_add_test(tc_single, test_get_single_leaf);
    suite_add_tcase(suite, tc_single);

    /* Test case for two-level tree */
    TCase *tc_two_level = tcase_create("TwoLevel");
    tcase_add_checked_fixture(tc_two_level, setup, teardown);
    tcase_add_test(tc_two_level, test_get_two_level_tree);
    suite_add_tcase(suite, tc_two_level);

    return suite;
}
