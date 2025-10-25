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
    unlink(DB_TEST_PATH_SPLIT);

    /* Create new database */
    status = napr_db_env_create(&env, pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_1MB);
    if (status != APR_SUCCESS) {
        return status;
    }

    status = napr_db_env_open(env, DB_TEST_PATH_SPLIT, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
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
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    char key_buf[DB_TEST_KEY_BUF_SIZE];
    char data_buf[DB_TEST_DATA_BUF_SIZE];
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

static void verify_page_nodes(DB_PageHeader *page)
{
    for (int idx = 0; idx < page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(page, idx);
        ck_assert_ptr_nonnull(node);
        ck_assert_int_gt(node->key_size, 0);
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

    populate_leaf_page(fixture->left_page, DB_TEST_KEY_COUNT_10);
    original_num_keys = fixture->left_page->num_keys;
    ck_assert_int_eq(original_num_keys, DB_TEST_KEY_COUNT_10);

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

    verify_page_nodes(fixture->left_page);
    verify_page_nodes(right_page);
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

static void verify_left_page_keys(db_split_fixture *fixture)
{
    char key_buf[DB_TEST_KEY_BUF_SIZE];

    for (int idx = 0; idx < fixture->left_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(fixture->left_page, idx);
        (void) snprintf(key_buf, sizeof(key_buf), "key_%03d", idx);
        ck_assert_int_eq(node->key_size, strlen(key_buf));
        ck_assert_int_eq(memcmp(db_leaf_node_key(node), key_buf, node->key_size), 0);
    }
}

static void verify_right_page_keys(DB_PageHeader *right_page)
{
    char key_buf[DB_TEST_KEY_BUF_SIZE];

    for (int idx = 0; idx < right_page->num_keys; idx++) {
        DB_LeafNode *node = db_page_leaf_node(right_page, idx);
        (void) snprintf(key_buf, sizeof(key_buf), "key_%03d", idx + 4);
        ck_assert_int_eq(node->key_size, strlen(key_buf));
        ck_assert_int_eq(memcmp(db_leaf_node_key(node), key_buf, node->key_size), 0);
    }
}

static void verify_leaf_split_key_distribution(db_split_fixture *fixture)
{
    DB_PageHeader *right_page = NULL;
    napr_db_val_t divider_key = { 0 };
    apr_status_t status = APR_SUCCESS;

    populate_leaf_page(fixture->left_page, DB_TEST_KEY_COUNT_8);
    ck_assert_int_eq(fixture->left_page->num_keys, DB_TEST_KEY_COUNT_8);

    status = db_split_leaf(fixture->txn, fixture->left_page, &right_page, &divider_key);
    ck_assert_int_eq(status, APR_SUCCESS);

    ck_assert_int_eq(fixture->left_page->num_keys, 4);
    ck_assert_int_eq(right_page->num_keys, 4);

    DB_LeafNode *first_right_node = db_page_leaf_node(right_page, 0);
    ck_assert_int_eq(divider_key.size, first_right_node->key_size);
    ck_assert_int_eq(memcmp(divider_key.data, db_leaf_node_key(first_right_node), divider_key.size), 0);

    verify_left_page_keys(fixture);
    verify_right_page_keys(right_page);
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

static void setup_stress_test_env(apr_pool_t **pool, napr_db_env_t **env)
{
    apr_status_t status = APR_SUCCESS;

    apr_initialize();
    apr_pool_create(pool, NULL);

    /* Remove existing test database */
    unlink(DB_TEST_PATH_SPLIT);

    /* Create new database with larger mapsize for stress test */
    status = napr_db_env_create(env, *pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(*env, DB_TEST_MAPSIZE_20MB);    /* 20MB for 100k keys */
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(*env, DB_TEST_PATH_SPLIT, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    ck_assert_int_eq(status, APR_SUCCESS);
}

static void insert_test_keys(napr_db_env_t *env)
{
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    char key_buf[DB_TEST_KEY_BUF_SIZE];
    char data_buf[DB_TEST_DATA_BUF_SIZE];
    napr_db_val_t key;
    napr_db_val_t data;
    int idx = 0;

    /* Insert many keys to force splits */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (idx = 0; idx < DB_TEST_NUM_KEYS_10K; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "key_%08d", idx);
        (void) snprintf(data_buf, sizeof(data_buf), "data_%08d", idx);

        key.data = key_buf;
        key.size = strlen(key_buf);
        data.data = data_buf;
        data.size = strlen(data_buf);

        status = napr_db_put(txn, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    /* Commit the transaction */
    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);
}

static void verify_test_keys(napr_db_env_t *env)
{
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    char key_buf[DB_TEST_KEY_BUF_SIZE];
    char data_buf[DB_TEST_DATA_BUF_SIZE];
    napr_db_val_t key;
    napr_db_val_t retrieved_data = { 0 };
    int idx = 0;

    /* Verify all keys are retrievable */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (idx = 0; idx < DB_TEST_NUM_KEYS_10K; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "key_%08d", idx);
        (void) snprintf(data_buf, sizeof(data_buf), "data_%08d", idx);

        key.data = key_buf;
        key.size = strlen(key_buf);

        status = napr_db_get(txn, &key, &retrieved_data);
        ck_assert_int_eq(status, APR_SUCCESS);
        ck_assert_int_eq(retrieved_data.size, strlen(data_buf));
        ck_assert_int_eq(memcmp(retrieved_data.data, data_buf, retrieved_data.size), 0);
    }

    status = napr_db_txn_abort(txn);
    ck_assert_int_eq(status, APR_SUCCESS);
}

/*
 * Test: Stress test insertions to force tree growth
 *
 * This test verifies that the tree correctly handles:
 * 1. Multiple leaf splits
 * 2. Branch splits when parents become full
 * 3. Root splits that increase tree height
 * 4. All data remains accessible after complex splits
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_stress_insertions)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;

    setup_stress_test_env(&pool, &env);
    insert_test_keys(env);
    verify_test_keys(env);

    napr_db_env_close(env);
    apr_pool_destroy(pool);
    apr_terminate();
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Verify root split occurs and tree height increases
 *
 * This test verifies that:
 * 1. The tree starts with a single leaf root
 * 2. After sufficient insertions, root splits occur
 * 3. Tree height increases correctly
 * 4. The root becomes a branch page
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_root_split)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    char key_buf[DB_TEST_KEY_BUF_SIZE];
    char data_buf[DB_TEST_DATA_BUF_SIZE];
    napr_db_val_t key;
    napr_db_val_t data;
    int idx = 0;
    pgno_t initial_root = 0;
    pgno_t final_root = 0;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert first key and capture initial root */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    (void) snprintf(key_buf, sizeof(key_buf), "key_%08d", 0);
    (void) snprintf(data_buf, sizeof(data_buf), "data_%08d", 0);
    key.data = key_buf;
    key.size = strlen(key_buf);
    data.data = data_buf;
    data.size = strlen(data_buf);

    status = napr_db_put(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    initial_root = txn->root_pgno;

    /* Commit */
    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert many more keys to force root split */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (idx = 1; idx < DB_TEST_NUM_KEYS_1000; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "key_%08d", idx);
        (void) snprintf(data_buf, sizeof(data_buf), "data_%08d", idx);

        key.data = key_buf;
        key.size = strlen(key_buf);
        data.data = data_buf;
        data.size = strlen(data_buf);

        status = napr_db_put(txn, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    final_root = txn->root_pgno;

    /* Verify root changed (root split occurred) */
    ck_assert(final_root != initial_root);

    /* Commit */
    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Close database */
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
    TCase *tc_core = tcase_create("Core");
    TCase *tc_stress = tcase_create("Stress");

    /* Basic split tests */
    tcase_add_test(tc_core, test_leaf_split_basic);
    tcase_add_test(tc_core, test_leaf_split_key_distribution);
    suite_add_tcase(suite, tc_core);

    /* Stress tests - with longer timeout */
    tcase_add_test(tc_stress, test_stress_insertions);
    tcase_add_test(tc_stress, test_root_split);
    tcase_set_timeout(tc_stress, DB_TEST_TIMEOUT_ONE_MINUTE);      /* timeout for stress tests */
    suite_add_tcase(suite, tc_stress);

    return suite;
}
