/**
 * @file check_db_split.c
 * @brief Tests for B+ tree page splitting in napr_db
 *
 * Tests leaf page splitting functionality.
 */

#include <check.h>
#include <apr_general.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <string.h>
#include <unistd.h>

#include "../src/napr_db_internal.h"
#include "check_db_constants.h"

/* Test database path in /tmp */
#define TEST_DB_PATH "/tmp/test_split.db"

/* Test buffer sizes */
enum
{
    TEST_KEY_SO_COUNT = 8,
    TEST_KEY_COUNT = 10,
    TEST_KEY_BUF_SIZE = 32,
    TEST_DATA_BUF_SIZE = 64
};

/* Helper to create and open a test database */
static apr_status_t create_test_db(apr_pool_t *pool, napr_db_env_t **env_out)
{
    apr_status_t status = APR_SUCCESS;
    napr_db_env_t *env = NULL;

    /* Remove existing test database */
    unlink(TEST_DB_PATH);

    /* Create new database */
    status = napr_db_env_create(&env, pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = napr_db_env_set_mapsize(env, ONE_MB);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = napr_db_env_open(env, TEST_DB_PATH, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    if (status != APR_SUCCESS) {
        return status;
    }

    *env_out = env;
    return APR_SUCCESS;
}

/*
 * Test: Leaf split basic functionality
 *
 * This test verifies that db_split_leaf correctly splits a full page:
 * 1. Creates a page with multiple entries
 * 2. Manually calls db_split_leaf
 * 3. Verifies the split occurred correctly
 * 4. Checks data distribution between left and right pages
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_leaf_split_basic)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    DB_PageHeader *left_page = NULL;
    DB_PageHeader *right_page = NULL;
    napr_db_val_t divider_key = { 0 };
    pgno_t left_pgno = 0;
    pgno_t *pgno_key = NULL;
    int idx = 0;
    char key_buf[TEST_KEY_BUF_SIZE] = { 0 };
    char data_buf[TEST_DATA_BUF_SIZE] = { 0 };
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    uint16_t original_num_keys = 0;
    uint16_t expected_left_keys = 0;
    uint16_t expected_right_keys = 0;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Allocate a test page */
    status = db_page_alloc(txn, 1, &left_pgno);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create the left page in memory */
    left_page = apr_pcalloc(txn->pool, PAGE_SIZE);
    ck_assert_ptr_nonnull(left_page);

    /* Initialize the left page */
    left_page->pgno = left_pgno;
    left_page->flags = P_LEAF;
    left_page->num_keys = 0;
    left_page->lower = sizeof(DB_PageHeader);
    left_page->upper = PAGE_SIZE;

    /* Store in dirty pages */
    pgno_key = apr_palloc(txn->pool, sizeof(pgno_t));
    ck_assert_ptr_nonnull(pgno_key);
    *pgno_key = left_pgno;
    apr_hash_set(txn->dirty_pages, pgno_key, sizeof(pgno_t), left_page);

    /* Insert several keys into the page (not enough to fill it completely for now) */
    for (idx = 0; idx < TEST_KEY_COUNT; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "key_%03d", idx);
        (void) snprintf(data_buf, sizeof(data_buf), "data_value_%03d", idx);

        key.data = key_buf;
        key.size = strlen(key_buf);
        data.data = data_buf;
        data.size = strlen(data_buf);

        status = db_page_insert(left_page, idx, &key, &data, 0);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    original_num_keys = left_page->num_keys;
    ck_assert_int_eq(original_num_keys, TEST_KEY_COUNT);

    /* Perform the split */
    status = db_split_leaf(txn, left_page, &right_page, &divider_key);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(right_page);

    /* Verify split occurred correctly */
    expected_left_keys = original_num_keys / 2; /* 5 keys */
    expected_right_keys = original_num_keys - expected_left_keys;       /* 5 keys */

    ck_assert_int_eq(left_page->num_keys, expected_left_keys);
    ck_assert_int_eq(right_page->num_keys, expected_right_keys);

    /* Verify the right page is a leaf page */
    ck_assert(right_page->flags & P_LEAF);

    /* Verify divider key points to the first key of the right page */
    ck_assert_ptr_nonnull(divider_key.data);
    ck_assert_int_gt(divider_key.size, 0);

    /* Verify we can read back keys from both pages */
    for (idx = 0; idx < left_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(left_page, idx);
        ck_assert_ptr_nonnull(node);
        ck_assert_int_gt(node->key_size, 0);
    }

    for (idx = 0; idx < right_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(right_page, idx);
        ck_assert_ptr_nonnull(node);
        ck_assert_int_gt(node->key_size, 0);
    }

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Verify keys are properly distributed after split
 *
 * This test verifies that:
 * 1. Keys in left page are all less than the divider key
 * 2. Keys in right page are all greater than or equal to the divider key
 * 3. All original keys are still present after the split
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_leaf_split_key_distribution)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    DB_PageHeader *left_page = NULL;
    DB_PageHeader *right_page = NULL;
    napr_db_val_t divider_key = { 0 };
    pgno_t left_pgno = 0;
    pgno_t *pgno_key = NULL;
    int idx = 0;
    char key_buf[TEST_KEY_BUF_SIZE] = { 0 };
    char data_buf[TEST_DATA_BUF_SIZE] = { 0 };
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Allocate a test page */
    status = db_page_alloc(txn, 1, &left_pgno);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create the left page in memory */
    left_page = apr_pcalloc(txn->pool, PAGE_SIZE);
    ck_assert_ptr_nonnull(left_page);

    /* Initialize the left page */
    left_page->pgno = left_pgno;
    left_page->flags = P_LEAF;
    left_page->num_keys = 0;
    left_page->lower = sizeof(DB_PageHeader);
    left_page->upper = PAGE_SIZE;

    /* Store in dirty pages */
    pgno_key = apr_palloc(txn->pool, sizeof(pgno_t));
    ck_assert_ptr_nonnull(pgno_key);
    *pgno_key = left_pgno;
    apr_hash_set(txn->dirty_pages, pgno_key, sizeof(pgno_t), left_page);

    /* Insert TEST_KEY_SO_COUNT keys in sorted order */
    for (idx = 0; idx < TEST_KEY_SO_COUNT; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "key_%03d", idx);
        (void) snprintf(data_buf, sizeof(data_buf), "data_%03d", idx);

        key.data = key_buf;
        key.size = strlen(key_buf);
        data.data = data_buf;
        data.size = strlen(data_buf);

        status = db_page_insert(left_page, idx, &key, &data, 0);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    ck_assert_int_eq(left_page->num_keys, TEST_KEY_SO_COUNT);

    /* Perform the split */
    status = db_split_leaf(txn, left_page, &right_page, &divider_key);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify we have 4 keys in each page */
    ck_assert_int_eq(left_page->num_keys, 4);
    ck_assert_int_eq(right_page->num_keys, 4);

    /* Verify the divider key matches the first key of the right page */
    DB_LeafNode *first_right_node = db_page_leaf_node(right_page, 0);
    ck_assert_int_eq(divider_key.size, first_right_node->key_size);
    ck_assert_int_eq(memcmp(divider_key.data, db_leaf_node_key(first_right_node), divider_key.size), 0);

    /* Verify keys in left page are key_000 through key_003 */
    for (idx = 0; idx < left_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(left_page, idx);
        (void) snprintf(key_buf, sizeof(key_buf), "key_%03d", idx);
        ck_assert_int_eq(node->key_size, strlen(key_buf));
        ck_assert_int_eq(memcmp(db_leaf_node_key(node), key_buf, node->key_size), 0);
    }

    /* Verify keys in right page are key_004 through key_007 */
    for (idx = 0; idx < right_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(right_page, idx);
        (void) snprintf(key_buf, sizeof(key_buf), "key_%03d", idx + 4);
        ck_assert_int_eq(node->key_size, strlen(key_buf));
        ck_assert_int_eq(memcmp(db_leaf_node_key(node), key_buf, node->key_size), 0);
    }

    /* Cleanup */
    napr_db_txn_abort(txn);
    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Suite creation
 */
Suite *make_db_split_suite(void)
{
    Suite *suite = suite_create("DB_Split");

    /* Leaf split tests */
    TCase *tc_leaf = tcase_create("LeafSplit");
    tcase_add_test(tc_leaf, test_leaf_split_basic);
    tcase_add_test(tc_leaf, test_leaf_split_key_distribution);
    suite_add_tcase(suite, tc_leaf);

    return suite;
}
