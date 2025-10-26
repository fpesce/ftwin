/**
 * @file check_db_mvcc.c
 * @brief Tests for MVCC reader tracking functionality
 *
 * This test suite verifies the MVCC reader tracking table implementation:
 * - DB_ReaderSlot size and alignment validation
 * - Reader registration and unregistration
 * - Concurrent reader tracking
 * - db_get_oldest_reader_txnid() correctness
 */

#include <check.h>
#include <apr.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_thread_proc.h>
#include <stddef.h>
#include <string.h>
#include "napr_db.h"
#include "napr_db_internal.h"
#include "check_db_constants.h"

/* Global fixtures */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *test_pool = NULL;

/**
 * @brief Verifies that the reader table is empty
 */
static void assert_reader_table_is_empty(napr_db_env_t *env)
{
    for (int idx = 0; idx < MAX_READERS; idx++) {
        ck_assert_int_eq(env->reader_table[idx].txnid, 0);
    }
}

/**
 * @brief Verifies that a single reader is registered with the correct TXNID
 */
static void assert_single_reader_is_registered(napr_db_env_t *env, txnid_t expected_txnid)
{
    int registered_count = 0;
    for (int idx = 0; idx < MAX_READERS; idx++) {
        if (env->reader_table[idx].txnid != 0) {
            registered_count++;
            ck_assert_int_eq(env->reader_table[idx].txnid, expected_txnid);
        }
    }
    ck_assert_int_eq(registered_count, 1);
}

/**
 * @brief Verifies the total number of registered readers
 */
static void assert_reader_count(napr_db_env_t *env, int expected_count)
{
    int registered_count = 0;
    for (int idx = 0; idx < MAX_READERS; idx++) {
        if (env->reader_table[idx].txnid != 0) {
            registered_count++;
        }
    }
    ck_assert_int_eq(registered_count, expected_count);
}


/**
 * @brief Setup function - runs before each test
 */
static void setup(void)
{
    apr_status_t status = APR_SUCCESS;

    /* Initialize APR if not already initialized */
    apr_initialize();

    /* Create a pool for the test */
    status = apr_pool_create(&test_pool, NULL);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Remove test database if it exists */
    apr_file_remove(DB_TEST_PATH_MVCC, test_pool);
}

/**
 * @brief Teardown function - runs after each test
 */
static void teardown(void)
{
    /* Clean up test database */
    if (test_pool) {
        apr_file_remove(DB_TEST_PATH_MVCC, test_pool);
        apr_pool_destroy(test_pool);
        test_pool = NULL;
    }
}

/**
 * @brief Validate DB_ReaderSlot structure size and alignment
 *
 * CRITICAL (Spec 3.2): The structure must be exactly CACHE_LINE_SIZE (64 bytes)
 * to prevent false sharing between CPU cores.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_reader_slot_alignment)
{
    /* Verify size is exactly 64 bytes (cache line size) */
    _Static_assert(sizeof(DB_ReaderSlot) == CACHE_LINE_SIZE, "DB_ReaderSlot must be exactly 64 bytes for cache-line alignment");

    /* Test passes if static assertion succeeds */
    ck_assert_int_eq(sizeof(DB_ReaderSlot), CACHE_LINE_SIZE);

    /* Verify array elements are properly spaced (each 64 bytes apart) */
    DB_ReaderSlot test_array[2] = { 0 };
    size_t offset = (char *) &test_array[1] - (char *) &test_array[0];
    ck_assert_int_eq(offset, CACHE_LINE_SIZE);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Test reader registration and unregistration
 *
 * Verifies that:
 * 1. Read-only transactions register in the reader table
 * 2. Reader table entries are correctly populated
 * 3. Readers are unregistered on commit/abort
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_reader_registration)
{
    apr_status_t status = APR_SUCCESS;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn1 = NULL;
    napr_db_txn_t *txn2 = NULL;

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_MVCC, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify reader table is initially empty */
    assert_reader_table_is_empty(env);

    /* Begin read-only transaction 1 - should register */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn1);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify one reader is registered */
    assert_single_reader_is_registered(env, txn1->txnid);

    /* Begin read-only transaction 2 - should register */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn2);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify two readers are registered */
    assert_reader_count(env, 2);

    /* Commit txn1 - should unregister */
    status = napr_db_txn_commit(txn1);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify only one reader remains */
    assert_single_reader_is_registered(env, txn2->txnid);

    /* Abort txn2 - should unregister */
    status = napr_db_txn_abort(txn2);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify reader table is empty again */
    assert_reader_table_is_empty(env);

    /* Clean up */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Test db_get_oldest_reader_txnid() correctness
 *
 * Verifies that the function correctly identifies the oldest active reader TXNID.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_oldest_reader_txnid)
{
    apr_status_t status = APR_SUCCESS;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *write_txn = NULL;
    napr_db_txn_t *read_txn1 = NULL;
    napr_db_txn_t *read_txn2 = NULL;
    napr_db_txn_t *read_txn3 = NULL;
    txnid_t oldest_txnid = 0;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_MVCC, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Initially, no readers - should return 0 */
    status = db_get_oldest_reader_txnid(env, &oldest_txnid);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(oldest_txnid, 0);

    /* Insert some data to create different TXNIDs */
    status = napr_db_txn_begin(env, 0, &write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    key.data = "key1";
    key.size = DB_TEST_MVCC_KEY_SIZE;
    data.data = "value1";
    data.size = DB_TEST_MVCC_DATA_SIZE;
    status = napr_db_put(write_txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_txn_commit(write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Start read transaction 1 - captures TXNID 1 */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &read_txn1);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Oldest reader should be TXNID 1 */
    status = db_get_oldest_reader_txnid(env, &oldest_txnid);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(oldest_txnid, read_txn1->txnid);

    /* Insert more data to advance TXNID */
    status = napr_db_txn_begin(env, 0, &write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    key.data = "key2";
    key.size = DB_TEST_MVCC_KEY_SIZE;
    data.data = "value2";
    data.size = DB_TEST_MVCC_DATA_SIZE;
    status = napr_db_put(write_txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_txn_commit(write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Start read transaction 2 - captures TXNID 2 */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &read_txn2);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Oldest reader should still be TXNID 1 */
    status = db_get_oldest_reader_txnid(env, &oldest_txnid);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(oldest_txnid, read_txn1->txnid);

    /* Insert more data */
    status = napr_db_txn_begin(env, 0, &write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    key.data = "key3";
    key.size = DB_TEST_MVCC_KEY_SIZE;
    data.data = "value3";
    data.size = DB_TEST_MVCC_DATA_SIZE;
    status = napr_db_put(write_txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_txn_commit(write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Start read transaction 3 - captures TXNID 3 */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &read_txn3);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Oldest reader should still be TXNID 1 */
    status = db_get_oldest_reader_txnid(env, &oldest_txnid);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(oldest_txnid, read_txn1->txnid);

    /* Commit read_txn1 - oldest should now be read_txn2 */
    status = napr_db_txn_commit(read_txn1);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = db_get_oldest_reader_txnid(env, &oldest_txnid);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(oldest_txnid, read_txn2->txnid);

    /* Commit read_txn2 - oldest should now be read_txn3 */
    status = napr_db_txn_commit(read_txn2);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = db_get_oldest_reader_txnid(env, &oldest_txnid);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(oldest_txnid, read_txn3->txnid);

    /* Commit read_txn3 - no readers, should return 0 */
    status = napr_db_txn_commit(read_txn3);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = db_get_oldest_reader_txnid(env, &oldest_txnid);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(oldest_txnid, 0);

    /* Clean up */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Test that write transactions do not register in reader table
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_write_txn_not_registered)
{
    apr_status_t status = APR_SUCCESS;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *write_txn = NULL;

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_MVCC, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction - should NOT register */
    status = napr_db_txn_begin(env, 0, &write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify reader table is empty */
    assert_reader_count(env, 0);

    /* Commit write transaction */
    status = napr_db_txn_commit(write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify reader table is still empty */
    assert_reader_count(env, 0);

    /* Clean up */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @brief Create the MVCC test suite
 */
Suite *db_mvcc_suite(void)
{
    Suite *suite = suite_create("DB_MVCC");
    TCase *tc_core = tcase_create("Core");

    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, test_reader_slot_alignment);
    tcase_add_test(tc_core, test_reader_registration);
    tcase_add_test(tc_core, test_oldest_reader_txnid);
    tcase_add_test(tc_core, test_write_txn_not_registered);

    suite_add_tcase(suite, tc_core);
    return suite;
}
