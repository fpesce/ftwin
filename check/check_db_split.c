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

/* Test fixture for db split tests */
typedef struct
{
    apr_pool_t *pool;
    napr_db_env_t *env;
    napr_db_txn_t *txn;
    DB_PageHeader *left_page;
    pgno_t left_pgno;
} db_split_fixture;

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

static void db_split_setup(db_split_fixture *fixture)
{
    apr_status_t status = APR_SUCCESS;

    apr_initialize();
    apr_pool_create(&fixture->pool, NULL);

    status = create_test_db(fixture->pool, &fixture->env);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_begin(fixture->env, 0, &fixture->txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = db_page_alloc(fixture->txn, 1, &fixture->left_pgno);
    ck_assert_int_eq(status, APR_SUCCESS);

    fixture->left_page = apr_pcalloc(fixture->txn->pool, PAGE_SIZE);
    ck_assert_ptr_nonnull(fixture->left_page);

    fixture->left_page->pgno = fixture->left_pgno;
    fixture->left_page->flags = P_LEAF;
    fixture->left_page->num_keys = 0;
    fixture->left_page->lower = sizeof(DB_PageHeader);
    fixture->left_page->upper = PAGE_SIZE;

    pgno_t *pgno_key = apr_palloc(fixture->txn->pool, sizeof(pgno_t));
    ck_assert_ptr_nonnull(pgno_key);
    *pgno_key = fixture->left_pgno;
    apr_hash_set(fixture->txn->dirty_pages, pgno_key, sizeof(pgno_t), fixture->left_page);
}

static void db_split_teardown(db_split_fixture *fixture)
{
    if (fixture->txn) {
        napr_db_txn_abort(fixture->txn);
    }
    if (fixture->env) {
        napr_db_env_close(fixture->env);
    }
    if (fixture->pool) {
        apr_pool_destroy(fixture->pool);
    }
    apr_terminate();
}

static void populate_leaf_page(DB_PageHeader *page, int num_keys)
{
    apr_status_t status = APR_SUCCESS;
    char key_buf[TEST_KEY_BUF_SIZE];
    char data_buf[TEST_DATA_BUF_SIZE];
    napr_db_val_t key;
    napr_db_val_t data;
    int idx = 0;

    for (idx = 0; idx < num_keys; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "key_%03d", idx);
        (void) snprintf(data_buf, sizeof(data_buf), "data_value_%03d", idx);

        key.data = key_buf;
        key.size = strlen(key_buf);
        data.data = data_buf;
        data.size = strlen(data_buf);

        status = db_page_insert(page, idx, &key, &data, 0);
        ck_assert_int_eq(status, APR_SUCCESS);
    }
}

static void verify_leaf_split_basic(db_split_fixture *fixture)
{
    DB_PageHeader *right_page = NULL;
    napr_db_val_t divider_key = { 0 };
    uint16_t original_num_keys = 0;
    uint16_t expected_left_keys = 0;
    uint16_t expected_right_keys = 0;
    apr_status_t status = APR_SUCCESS;
    int idx = 0;

    populate_leaf_page(fixture->left_page, TEST_KEY_COUNT);
    original_num_keys = fixture->left_page->num_keys;
    ck_assert_int_eq(original_num_keys, TEST_KEY_COUNT);

    status = db_split_leaf(fixture->txn, fixture->left_page, &right_page, &divider_key);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(right_page);

    expected_left_keys = original_num_keys / 2;
    expected_right_keys = original_num_keys - expected_left_keys;
    ck_assert_int_eq(fixture->left_page->num_keys, expected_left_keys);
    ck_assert_int_eq(right_page->num_keys, expected_right_keys);
    ck_assert(right_page->flags & P_LEAF);
    ck_assert_ptr_nonnull(divider_key.data);
    ck_assert_int_gt(divider_key.size, 0);

    for (idx = 0; idx < fixture->left_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(fixture->left_page, idx);
        ck_assert_ptr_nonnull(node);
        ck_assert_int_gt(node->key_size, 0);
    }

    for (idx = 0; idx < right_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(right_page, idx);
        ck_assert_ptr_nonnull(node);
        ck_assert_int_gt(node->key_size, 0);
    }
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
    db_split_fixture fixture = { 0 };
    db_split_setup(&fixture);
    verify_leaf_split_basic(&fixture);
    db_split_teardown(&fixture);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

static void verify_leaf_split_key_distribution(db_split_fixture *fixture)
{
    DB_PageHeader *right_page = NULL;
    napr_db_val_t divider_key = { 0 };
    apr_status_t status = APR_SUCCESS;
    int idx = 0;
    char key_buf[TEST_KEY_BUF_SIZE];

    populate_leaf_page(fixture->left_page, TEST_KEY_SO_COUNT);
    ck_assert_int_eq(fixture->left_page->num_keys, TEST_KEY_SO_COUNT);

    status = db_split_leaf(fixture->txn, fixture->left_page, &right_page, &divider_key);
    ck_assert_int_eq(status, APR_SUCCESS);

    ck_assert_int_eq(fixture->left_page->num_keys, 4);
    ck_assert_int_eq(right_page->num_keys, 4);

    DB_LeafNode *first_right_node = db_page_leaf_node(right_page, 0);
    ck_assert_int_eq(divider_key.size, first_right_node->key_size);
    ck_assert_int_eq(memcmp(divider_key.data, db_leaf_node_key(first_right_node), divider_key.size), 0);

    for (idx = 0; idx < fixture->left_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(fixture->left_page, idx);
        (void) snprintf(key_buf, sizeof(key_buf), "key_%03d", idx);
        ck_assert_int_eq(node->key_size, strlen(key_buf));
        ck_assert_int_eq(memcmp(db_leaf_node_key(node), key_buf, node->key_size), 0);
    }

    for (idx = 0; idx < right_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(right_page, idx);
        (void) snprintf(key_buf, sizeof(key_buf), "key_%03d", idx + 4);
        ck_assert_int_eq(node->key_size, strlen(key_buf));
        ck_assert_int_eq(memcmp(db_leaf_node_key(node), key_buf, node->key_size), 0);
    }
}

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
    db_split_fixture fixture = { 0 };
    db_split_setup(&fixture);
    verify_leaf_split_key_distribution(&fixture);
    db_split_teardown(&fixture);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Suite creation
 */
Suite *make_db_split_suite(void)
{
    Suite *s = suite_create("DB_Split");
    TCase *tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_leaf_split_basic);
    tcase_add_test(tc_core, test_leaf_split_key_distribution);
    suite_add_tcase(s, tc_core);

    return s;
}
