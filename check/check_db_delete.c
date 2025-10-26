/**
 * @file check_db_delete.c
 * @brief Tests for napr_db deletion functionality
 */

#include <check.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <stdlib.h>
#include <string.h>
#include "napr_db.h"

#include "check_db_constants.h"

/* Test fixture setup/teardown */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *test_pool = NULL;

static void setup(void)
{
    apr_pool_create(&test_pool, NULL);
    apr_file_remove(DB_TEST_PATH_DELETE, test_pool);
}

static void teardown(void)
{
    apr_file_remove(DB_TEST_PATH_DELETE, test_pool);
    apr_pool_destroy(test_pool);
    test_pool = NULL;
}

/**
 * @brief Test basic deletion functionality
 *
 * Insert three keys (A, B, C), delete B, verify A and C remain and B is gone.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_basic_deletion)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    apr_status_t status = APR_SUCCESS;

    /* Create and open database */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_DELETE, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert three keys: A, B, C */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyA";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    data.data = "valueA";
    data.size = DB_TEST_DELETE_DATA_SIZE;
    status = napr_db_put(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyB";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    data.data = "valueB";
    data.size = DB_TEST_DELETE_DATA_SIZE;
    status = napr_db_put(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyC";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    data.data = "valueC";
    data.size = DB_TEST_DELETE_DATA_SIZE;
    status = napr_db_put(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Delete key B */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyB";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    status = napr_db_del(txn, &key, NULL);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify: A and C exist, B does not */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyA";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(data.size, DB_TEST_DELETE_DATA_SIZE);
    ck_assert_mem_eq(data.data, "valueA", DB_TEST_DELETE_DATA_SIZE);

    key.data = "keyB";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_NOTFOUND);

    key.data = "keyC";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(data.size, DB_TEST_DELETE_DATA_SIZE);
    ck_assert_mem_eq(data.data, "valueC", DB_TEST_DELETE_DATA_SIZE);

    napr_db_txn_abort(txn);
    napr_db_env_close(env);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Test deleting first and last keys
 *
 * Verifies that deletion works correctly at page boundaries.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_delete_boundaries)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    apr_status_t status = APR_SUCCESS;
    char key_buf[DB_TEST_KEY_BUF_SIZE] = { 0 };
    char val_buf[DB_TEST_KEY_BUF_SIZE] = { 0 };

    /* Create and open database */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_DELETE, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert multiple keys (key000 to key009) */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (int i = 0; i < DB_TEST_KEY_COUNT_10; i++) {
        (void) snprintf(key_buf, sizeof(key_buf), "key%03d", i);
        (void) snprintf(val_buf, sizeof(val_buf), "value%03d", i);
        key.data = key_buf;
        key.size = (apr_size_t) strlen(key_buf);
        data.data = val_buf;
        data.size = (apr_size_t) strlen(val_buf);
        status = napr_db_put(txn, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Delete first key (key000) */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "key000";
    key.size = DB_TEST_DELETE_DATA_SIZE;
    status = napr_db_del(txn, &key, NULL);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Delete last key (key009) */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "key009";
    key.size = DB_TEST_DELETE_DATA_SIZE;
    status = napr_db_del(txn, &key, NULL);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify: first and last are gone, middle keys exist */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "key000";
    key.size = DB_TEST_DELETE_DATA_SIZE;
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_NOTFOUND);

    key.data = "key009";
    key.size = DB_TEST_DELETE_DATA_SIZE;
    status = napr_db_get(txn, &key, &data);
    ck_assert_int_eq(status, APR_NOTFOUND);

    /* Verify middle keys still exist */
    for (int i = 1; i < DB_TEST_KEY_COUNT_9; i++) {
        (void) snprintf(key_buf, sizeof(key_buf), "key%03d", i);
        (void) snprintf(val_buf, sizeof(val_buf), "value%03d", i);
        key.data = key_buf;
        key.size = (apr_size_t) strlen(key_buf);
        status = napr_db_get(txn, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
        ck_assert_int_eq(data.size, strlen(val_buf));
        ck_assert_mem_eq(data.data, val_buf, data.size);
    }

    napr_db_txn_abort(txn);
    napr_db_env_close(env);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Test deleting non-existent key
 *
 * Verifies that deleting a key that doesn't exist returns APR_NOTFOUND.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_delete_nonexistent)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    apr_status_t status = APR_SUCCESS;

    /* Create and open database */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_DELETE, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert one key */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyA";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    data.data = "valueA";
    data.size = DB_TEST_DELETE_DATA_SIZE;
    status = napr_db_put(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Try to delete non-existent key */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyZ";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    status = napr_db_del(txn, &key, NULL);
    ck_assert_int_eq(status, APR_NOTFOUND);

    napr_db_txn_abort(txn);

    /* Try to delete from empty tree */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* First delete the only key */
    key.data = "keyA";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    status = napr_db_del(txn, &key, NULL);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Now try to delete from empty tree */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyA";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    status = napr_db_del(txn, &key, NULL);
    ck_assert_int_eq(status, APR_NOTFOUND);

    napr_db_txn_abort(txn);
    napr_db_env_close(env);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Test that deletion rejects read-only transactions
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_delete_readonly_txn)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    apr_status_t status = APR_SUCCESS;

    /* Create and open database */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_DELETE, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert a key */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyA";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    data.data = "valueA";
    data.size = DB_TEST_DELETE_DATA_SIZE;
    status = napr_db_put(txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Try to delete in read-only transaction */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    key.data = "keyA";
    key.size = DB_TEST_DELETE_KEY_SIZE;
    status = napr_db_del(txn, &key, NULL);
    ck_assert_int_eq(status, APR_EINVAL);

    napr_db_txn_abort(txn);
    napr_db_env_close(env);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *db_delete_suite(void)
{
    Suite *suite = NULL;
    TCase *tc_core = NULL;

    suite = suite_create("DB_Delete");

    /* Core test case */
    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_basic_deletion);
    tcase_add_test(tc_core, test_delete_boundaries);
    tcase_add_test(tc_core, test_delete_nonexistent);
    tcase_add_test(tc_core, test_delete_readonly_txn);

    suite_add_tcase(suite, tc_core);

    return suite;
}
