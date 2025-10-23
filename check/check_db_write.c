/**
 * @file check_db_write.c
 * @brief Tests for B+ tree write operations (insertion) in napr_db
 *
 * Tests napr_db_put functionality with Copy-on-Write path propagation.
 * This iteration focuses on simple insertions without page splits.
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
#define TEST_DB_PATH "/tmp/test_write.db"

/* Test buffer sizes */
enum {
    TEST_KEY_BUF_SIZE = 32,
    TEST_DATA_BUF_SIZE = 64
};

/* Helper to create and open a test database */

/* Helper to insert a range of keys into the database */
static void helper_insert_data_forward(napr_db_txn_t *txn, const char *key_prefix,
                                       const char *value_prefix, const int num_keys)
{
    char key_buf[TEST_KEY_BUF_SIZE] = { 0 };
    char data_buf[TEST_DATA_BUF_SIZE] = { 0 };
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    apr_status_t status = APR_SUCCESS;

    for (int idx = 0; idx < num_keys; idx++) {
        int ret = 0;
        ret = snprintf(key_buf, sizeof(key_buf), "%s_%03d", key_prefix, idx);
        ck_assert_int_ge(ret, 0);
        ck_assert_int_lt(ret, sizeof(key_buf));
        ret = snprintf(data_buf, sizeof(data_buf), "%s_%03d", value_prefix, idx);
        ck_assert_int_ge(ret, 0);
        ck_assert_int_lt(ret, sizeof(data_buf));

        key.data = key_buf;
        key.size = strlen(key_buf);
        data.data = data_buf;
        data.size = strlen(data_buf);

        status = napr_db_put(txn, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
    }
}

static void helper_insert_data_reverse(napr_db_txn_t *txn, const char *key_prefix,
                                       const char *value_prefix, const int num_keys)
{
    char key_buf[TEST_KEY_BUF_SIZE] = { 0 };
    char data_buf[TEST_DATA_BUF_SIZE] = { 0 };
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    apr_status_t status = APR_SUCCESS;

    for (int idx = num_keys - 1; idx >= 0; idx--) {
        int ret = 0;
        ret = snprintf(key_buf, sizeof(key_buf), "%s_%03d", key_prefix, idx);
        ck_assert_int_ge(ret, 0);
        ck_assert_int_lt(ret, sizeof(key_buf));
        ret = snprintf(data_buf, sizeof(data_buf), "%s_%03d", value_prefix, idx);
        ck_assert_int_ge(ret, 0);
        ck_assert_int_lt(ret, sizeof(data_buf));

        key.data = key_buf;
        key.size = strlen(key_buf);
        data.data = data_buf;
        data.size = strlen(data_buf);

        status = napr_db_put(txn, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
    }
}

/* Helper to verify a range of keys from the database */
static void helper_verify_data(napr_db_txn_t *txn, const char *key_prefix,
                               const char *value_prefix, const int num_keys)
{
    char key_buf[TEST_KEY_BUF_SIZE] = { 0 };
    char data_buf[TEST_DATA_BUF_SIZE] = { 0 };
    napr_db_val_t key = { 0 };
    napr_db_val_t retrieved = { 0 };
    apr_status_t status = APR_SUCCESS;

    for (int idx = 0; idx < num_keys; idx++) {
        int ret = 0;
        ret = snprintf(key_buf, sizeof(key_buf), "%s_%03d", key_prefix, idx);
        ck_assert_int_ge(ret, 0);
        ck_assert_int_lt(ret, sizeof(key_buf));
        ret = snprintf(data_buf, sizeof(data_buf), "%s_%03d", value_prefix, idx);
        ck_assert_int_ge(ret, 0);
        ck_assert_int_lt(ret, sizeof(data_buf));

        key.data = key_buf;
        key.size = strlen(key_buf);

        status = napr_db_get(txn, &key, &retrieved);
        ck_assert_int_eq(status, APR_SUCCESS);
        ck_assert_int_eq(retrieved.size, strlen(data_buf));
        ck_assert_int_eq(memcmp(retrieved.data, data_buf, strlen(data_buf)), 0);
    }
}

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
 * Test: Insert a single key into an empty database
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_insert_single_key)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    napr_db_val_t retrieved = { 0 };
    const char *key_str = "testkey";
    const char *data_str = "testvalue";

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(txn);

    /* Prepare key and data */
    key.data = (void *) key_str;
    key.size = strlen(key_str);
    data.data = (void *) data_str;
    data.size = strlen(data_str);

    /* Insert the key */
    status = napr_db_put(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify data is visible within the same transaction */
    status = napr_db_get(txn, &key, &retrieved);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(retrieved.size, data.size);
    ck_assert_int_eq(memcmp(retrieved.data, data.data, data.size), 0);

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
 * Test: Insert multiple keys that don't fill the first page
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_insert_multiple_keys)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    const int num_keys = 10;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert and verify data */
    helper_insert_data_forward(txn, "key", "value_%03d_data", num_keys);
    helper_verify_data(txn, "key", "value_%03d_data", num_keys);

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
 * Test: Transaction abort discards changes (atomicity test)
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_insert_abort_atomicity)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    napr_db_val_t retrieved = { 0 };
    const char *key_str = "abort_key";
    const char *data_str = "abort_value";

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Prepare key and data */
    key.data = (void *) key_str;
    key.size = strlen(key_str);
    data.data = (void *) data_str;
    data.size = strlen(data_str);

    /* Insert the key */
    status = napr_db_put(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify data is visible within the same transaction */
    status = napr_db_get(txn, &key, &retrieved);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Abort the transaction */
    napr_db_txn_abort(txn);

    /* Start a new read transaction */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify data is gone (atomicity) */
    status = napr_db_get(txn, &key, &retrieved);
    ck_assert_int_eq(status, APR_NOTFOUND);

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
 * Test: Insert with duplicate key returns APR_EEXIST
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_insert_duplicate_key)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    napr_db_val_t key = { 0 };
    napr_db_val_t data1 = { 0 };
    napr_db_val_t data2 = { 0 };
    const char *key_str = "dupkey";
    const char *data_str1 = "value1";
    const char *data_str2 = "value2";

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Prepare key and data */
    key.data = (void *) key_str;
    key.size = strlen(key_str);
    data1.data = (void *) data_str1;
    data1.size = strlen(data_str1);
    data2.data = (void *) data_str2;
    data2.size = strlen(data_str2);

    /* Insert the key first time */
    status = napr_db_put(txn, &key, &data1);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Attempt to insert the same key again */
    status = napr_db_put(txn, &key, &data2);
    ck_assert_int_eq(status, APR_EEXIST);

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
 * Test: Read-only transaction rejects write operations
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_insert_rdonly_rejected)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    const char *key_str = "rdonly_key";
    const char *data_str = "rdonly_value";

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin READ-ONLY transaction */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Prepare key and data */
    key.data = (void *) key_str;
    key.size = strlen(key_str);
    data.data = (void *) data_str;
    data.size = strlen(data_str);

    /* Attempt to insert - should fail */
    status = napr_db_put(txn, &key, &data);
    ck_assert_int_eq(status, APR_EACCES);

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
 * Test: Insert keys in sorted order
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_insert_sorted_order)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    const int num_keys = 8;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert and verify data */
    helper_insert_data_forward(txn, "sorted_key", "sorted_value", num_keys);
    helper_verify_data(txn, "sorted_key", "sorted_value", num_keys);

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
 * Test: Insert keys in reverse sorted order
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_insert_reverse_order)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = APR_SUCCESS;
    const int num_keys = 8;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Create test database */
    status = create_test_db(pool, &env);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert and verify data */
    helper_insert_data_reverse(txn, "reverse_key", "reverse_value", num_keys);
    helper_verify_data(txn, "reverse_key", "reverse_value", num_keys);

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
Suite *make_db_write_suite(void)
{
    Suite *suite = suite_create("DB_Write");

    /* Basic insertion tests */
    TCase *tc_basic = tcase_create("BasicInsert");
    tcase_add_test(tc_basic, test_insert_single_key);
    tcase_add_test(tc_basic, test_insert_multiple_keys);
    tcase_add_test(tc_basic, test_insert_sorted_order);
    tcase_add_test(tc_basic, test_insert_reverse_order);
    suite_add_tcase(suite, tc_basic);

    /* Transaction semantics tests */
    TCase *tc_txn = tcase_create("TransactionSemantics");
    tcase_add_test(tc_txn, test_insert_abort_atomicity);
    tcase_add_test(tc_txn, test_insert_duplicate_key);
    tcase_add_test(tc_txn, test_insert_rdonly_rejected);
    suite_add_tcase(suite, tc_txn);

    return suite;
}
