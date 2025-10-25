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
#include <string.h>

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
    apr_status_t status = APR_SUCCESS;
    int idx = 0;
    char key_buf[DB_TEST_KEY_BUF_SIZE];
    char val_buf[DB_TEST_DATA_BUF_SIZE];

    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (idx = 0; idx < DB_TEST_NUM_KEYS_1000; idx++) {
        napr_db_val_t key;
        napr_db_val_t data;

        (void) snprintf(key_buf, sizeof(key_buf), "key%04d", idx);
        key.data = key_buf;
        key.size = strlen(key_buf);

        (void) snprintf(val_buf, sizeof(val_buf), "val%04d", idx);
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
    (void) apr_file_remove(DB_TEST_PATH_CURSOR, pool);

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

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cursor_first_last)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t key;
    napr_db_val_t data;
    apr_status_t status = APR_SUCCESS;

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
    (void) snprintf(last_key, sizeof(last_key), "key%04d", DB_TEST_NUM_KEYS_1000 - 1);

    status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_LAST);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(key.size, strlen(last_key));
    ck_assert_mem_eq(key.data, last_key, key.size);

    napr_db_cursor_close(cursor);
    napr_db_txn_abort(txn);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cursor_set)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t search_key;
    napr_db_val_t data;
    apr_status_t status = APR_SUCCESS;

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
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cursor_set_range)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t key;
    napr_db_val_t data;
    apr_status_t status = APR_SUCCESS;

    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_cursor_open(txn, &cursor);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Test SET_RANGE (inexact match) */
    key.data = "key0500a";      // This key does not exist
    key.size = strlen((const char *) key.data);

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
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cursor_forward_iteration)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t key;
    napr_db_val_t data;
    apr_status_t status = APR_SUCCESS;
    int count = 0;
    char prev_key[DB_TEST_KEY_BUF_SIZE];

    /* Test full forward iteration using FIRST + NEXT */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_cursor_open(txn, &cursor);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Position at first key */
    status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_FIRST);
    ck_assert_int_eq(status, APR_SUCCESS);

    count = 1;
    memcpy(prev_key, key.data, key.size);
    prev_key[key.size] = '\0';

    /* Iterate through all remaining keys */
    while ((status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_NEXT)) == APR_SUCCESS) {
        char current_key[DB_TEST_KEY_BUF_SIZE];
        memcpy(current_key, key.data, key.size);
        current_key[key.size] = '\0';

        /* Debug: print first failure */
        if (strcmp(prev_key, current_key) >= 0 && count < DB_TEST_KEY_COUNT_10) {
            (void) fprintf(stderr, "DEBUG: count=%d, prev=%s, current=%s\n", count, prev_key, current_key);
        }

        /* Verify lexicographical ordering */
        ck_assert_msg(strcmp(prev_key, current_key) < 0, "Keys not in order: %s >= %s", prev_key, current_key);

        memcpy(prev_key, key.data, key.size);
        prev_key[key.size] = '\0';
        count++;
    }

    /* Should have iterated through all keys */
    ck_assert_int_eq(count, DB_TEST_NUM_KEYS_1000);
    ck_assert_int_eq(status, APR_NOTFOUND);     /* End of iteration */

    napr_db_cursor_close(cursor);
    napr_db_txn_abort(txn);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cursor_backward_iteration)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t key;
    napr_db_val_t data;
    apr_status_t status = APR_SUCCESS;
    int count = 0;
    char prev_key[DB_TEST_KEY_BUF_SIZE];

    /* Test full backward iteration using LAST + PREV */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_cursor_open(txn, &cursor);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Position at last key */
    status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_LAST);
    ck_assert_int_eq(status, APR_SUCCESS);

    count = 1;
    memcpy(prev_key, key.data, key.size);
    prev_key[key.size] = '\0';

    /* Iterate backward through all remaining keys */
    while ((status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_PREV)) == APR_SUCCESS) {
        char current_key[DB_TEST_KEY_BUF_SIZE];
        memcpy(current_key, key.data, key.size);
        current_key[key.size] = '\0';

        /* Verify reverse lexicographical ordering */
        ck_assert_msg(strcmp(prev_key, current_key) > 0, "Keys not in reverse order: %s <= %s", prev_key, current_key);

        memcpy(prev_key, key.data, key.size);
        prev_key[key.size] = '\0';
        count++;
    }

    /* Should have iterated through all keys */
    ck_assert_int_eq(count, DB_TEST_NUM_KEYS_1000);
    ck_assert_int_eq(status, APR_NOTFOUND);     /* Beginning of iteration */

    napr_db_cursor_close(cursor);
    napr_db_txn_abort(txn);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Verifies the cursor's current position against an expected key-value pair.
 */
static void verify_cursor_position(napr_db_val_t *key, napr_db_val_t *data, int idx)
{
    char expected_key[DB_TEST_KEY_BUF_SIZE];
    char expected_val[DB_TEST_DATA_BUF_SIZE];

    (void) snprintf(expected_key, sizeof(expected_key), "key%04d", idx);
    (void) snprintf(expected_val, sizeof(expected_val), "val%04d", idx);

    ck_assert_int_eq(key->size, strlen(expected_key));
    ck_assert_mem_eq(key->data, expected_key, key->size);
    ck_assert_int_eq(data->size, strlen(expected_val));
    ck_assert_mem_eq(data->data, expected_val, data->size);
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cursor_page_boundary)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t key;
    napr_db_val_t data;
    apr_status_t status = APR_SUCCESS;
    int idx = 0;

    /*
     * Test iteration across page boundaries.
     * With 1000 keys, we should have multiple pages and therefore
     * multiple page boundary crossings during iteration.
     * This verifies the stack ascent/descent logic works correctly.
     */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_cursor_open(txn, &cursor);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Forward iteration - verify all keys are present */
    status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_FIRST);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (idx = 0; idx < DB_TEST_NUM_KEYS_1000; idx++) {
        verify_cursor_position(&key, &data, idx);

        /* Move to next (will cross page boundaries during this loop) */
        if (idx < DB_TEST_NUM_KEYS_1000 - 1) {
            status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_NEXT);
            ck_assert_int_eq(status, APR_SUCCESS);
        }
    }

    /* Verify we're at the end */
    status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_NEXT);
    ck_assert_int_eq(status, APR_NOTFOUND);

    napr_db_cursor_close(cursor);
    napr_db_txn_abort(txn);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Moves the cursor forward a specified number of steps.
 */
static void move_cursor_forward(napr_db_cursor_t *cursor, napr_db_val_t *key, napr_db_val_t *data, int steps)
{
    apr_status_t status = APR_SUCCESS;
    for (int i = 0; i < steps; i++) {
        status = napr_db_cursor_get(cursor, key, data, NAPR_DB_NEXT);
        ck_assert_int_eq(status, APR_SUCCESS);
    }
}

/**
 * @brief Moves the cursor backward a specified number of steps.
 */
static void move_cursor_backward(napr_db_cursor_t *cursor, napr_db_val_t *key, napr_db_val_t *data, int steps)
{
    apr_status_t status = APR_SUCCESS;
    for (int i = 0; i < steps; i++) {
        status = napr_db_cursor_get(cursor, key, data, NAPR_DB_PREV);
        ck_assert_int_eq(status, APR_SUCCESS);
    }
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_cursor_bidirectional)
{
    napr_db_txn_t *txn = NULL;
    napr_db_cursor_t *cursor = NULL;
    napr_db_val_t key;
    napr_db_val_t data;
    apr_status_t status = APR_SUCCESS;

    /* Test mixing NEXT and PREV operations */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_cursor_open(txn, &cursor);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Start from key0500 */
    key.data = "key0500";
    key.size = strlen("key0500");
    status = napr_db_cursor_get(cursor, &key, &data, NAPR_DB_SET);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Move forward and verify */
    move_cursor_forward(cursor, &key, &data, DB_TEST_KEY_COUNT_10);
    ck_assert_int_eq(key.size, strlen("key0510"));
    ck_assert_mem_eq(key.data, "key0510", key.size);

    /* Move backward and verify */
    move_cursor_backward(cursor, &key, &data, DB_TEST_KEY_COUNT_5);
    ck_assert_int_eq(key.size, strlen("key0505"));
    ck_assert_mem_eq(key.data, "key0505", key.size);

    napr_db_cursor_close(cursor);
    napr_db_txn_abort(txn);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

Suite *db_cursor_suite(void)
{
    Suite *suite = suite_create("DBCursor");
    TCase *tc_core = tcase_create("Core");

    tcase_add_checked_fixture(tc_core, setup, teardown);
    tcase_add_test(tc_core, test_cursor_first_last);
    tcase_add_test(tc_core, test_cursor_set);
    tcase_add_test(tc_core, test_cursor_set_range);
    tcase_add_test(tc_core, test_cursor_forward_iteration);
    tcase_add_test(tc_core, test_cursor_backward_iteration);
    tcase_add_test(tc_core, test_cursor_page_boundary);
    tcase_add_test(tc_core, test_cursor_bidirectional);

    suite_add_tcase(suite, tc_core);
    return suite;
}
