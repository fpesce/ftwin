/**
 * @file check_db_cursor.c
 * @brief Unit tests for the napr_db cursor functionality
 */

#include "napr_db.h"
#include "napr_db_internal.h"
#include "check_db_constants.h"
#include <check.h>
#include <apr_file_io.h>
#include <stdio.h>

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static napr_db_env_t *env;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *pool;

/**
 * @brief Populates the database with a large number of keys.
 */
static void populate_db(void)
{
    napr_db_txn_t *txn = NULL;
    apr_status_t status;
    int i;
    char key_buf[DB_TEST_KEY_BUF_SIZE];
    char val_buf[DB_TEST_DATA_BUF_SIZE];

    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (i = 0; i < DB_TEST_NUM_KEYS_1000; i++) {
        napr_db_val_t key, data;

        snprintf(key_buf, sizeof(key_buf), "key%04d", i);
        key.data = key_buf;
        key.size = strlen(key_buf);

        snprintf(val_buf, sizeof(val_buf), "val%04d", i);
        data.data = val_buf;
        data.size = strlen(val_buf);

        status = napr_db_put(txn, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);
}

/**
 * @brief Test fixture setup.
 */
static void setup(void)
{
    apr_pool_create(&pool, NULL);
    (void)apr_file_remove(DB_TEST_PATH_CURSOR, pool);

    ck_assert_int_eq(napr_db_env_create(&env, pool), APR_SUCCESS);
    ck_assert_int_eq(napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB), APR_SUCCESS);
    ck_assert_int_eq(napr_db_env_open(env, DB_TEST_PATH_CURSOR, NAPR_DB_CREATE), APR_SUCCESS);

    populate_db();
}

/**
 * @brief Test fixture teardown.
 */
static void teardown(void)
{
    if (env) {
        napr_db_env_close(env);
        env = NULL;
    }
    if (pool) {
        apr_pool_destroy(pool);
        pool = NULL;
    }
}

START_TEST(test_cursor_first_last)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t key, data;
    apr_status_t status;

    /* Test FIRST */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_cursor_open(txn, &cursor);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_FIRST);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(key.size, strlen("key0000"));
    ck_assert_mem_eq(key.data, "key0000", key.size);
    ck_assert_int_eq(data.size, strlen("val0000"));
    ck_assert_mem_eq(data.data, "val0000", data.size);

    napr_db_cursor_close(cursor);
    napr_db_txn_abort(txn);

    /* Test LAST */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_cursor_open(txn, &cursor);
    ck_assert_int_eq(status, APR_SUCCESS);

    char last_key[DB_TEST_KEY_BUF_SIZE];
    snprintf(last_key, sizeof(last_key), "key%04d", DB_TEST_NUM_KEYS_1000 - 1);

    status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_LAST);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(key.size, strlen(last_key));
    ck_assert_mem_eq(key.data, last_key, key.size);

    napr_db_cursor_close(cursor);
    napr_db_txn_abort(txn);
}
END_TEST

START_TEST(test_cursor_set)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t search_key, data;
    apr_status_t status;

    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_cursor_open(txn, &cursor);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Test SET (exact match) */
    search_key.data = "key0500";
    search_key.size = strlen("key0500");

    status = napr_db_cursor_get(cursor, &search_key, &data, NAPR_DB_SET);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(data.size, strlen("val0500"));
    ck_assert_mem_eq(data.data, "val0500", data.size);

    napr_db_cursor_close(cursor);
    napr_db_txn_abort(txn);
}
END_TEST

START_TEST(test_cursor_set_range)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t key, data;
    apr_status_t status;

    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_cursor_open(txn, &cursor);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Test SET_RANGE (inexact match) */
    key.data = "key0500a"; // This key does not exist
    key.size = strlen((const char*)key.data);

    status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_SET_RANGE);
    ck_assert_int_eq(status, APR_SUCCESS);

    // Should find the next key, which is key0501
    ck_assert_int_eq(key.size, strlen("key0501"));
    ck_assert_mem_eq(key.data, "key0501", key.size);
    ck_assert_int_eq(data.size, strlen("val0501"));
    ck_assert_mem_eq(data.data, "val0501", data.size);

    napr_db_cursor_close(cursor);
    napr_db_txn_abort(txn);
}
END_TEST

Suite *db_cursor_suite(void)
{
    Suite *s = suite_create("DBCursor");
    TCase *tc_core = tcase_create("Core");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_cursor_first_last);
    tcase_add_test(tc_core, test_cursor_set);
    tcase_add_test(tc_core, test_cursor_set_range);

    suite_add_tcase(s, tc_core);
    return s;
}
