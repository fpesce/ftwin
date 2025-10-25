/**
 * @file check_db_txn.c
 * @brief Transaction lifecycle and SWMR concurrency tests for napr_db
 *
 * Tests transaction begin/commit/abort and the Single-Writer/Multiple-Reader
 * synchronization model with configurable locking.
 */

#include <check.h>
#include <apr.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_thread_proc.h>
#include <apr_time.h>
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

    /* Remove any existing test database */
    apr_file_remove(DB_TEST_PATH_TXN, test_pool);
}

static void teardown(void)
{
    /* Clean up test database */
    if (test_pool) {
        apr_file_remove(DB_TEST_PATH_TXN, test_pool);
        apr_pool_destroy(test_pool);
        test_pool = NULL;
    }
}

/*
 * Test: Basic read transaction lifecycle
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_txn_read_lifecycle)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = 0;
    apr_size_t mapsize = DB_TEST_MAPSIZE_1MB;

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, mapsize);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_TXN, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin read transaction */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(txn);

    /* Verify transaction properties */
    ck_assert_ptr_eq(txn->env, env);
    ck_assert_ptr_nonnull(txn->pool);
    ck_assert_uint_eq(txn->flags, NAPR_DB_RDONLY);
    ck_assert_uint_eq(txn->txnid, 1);   /* Initial DB has TXNID 1 */
    ck_assert_uint_eq(txn->root_pgno, 0);       /* Empty tree */

    /* Commit read transaction */
    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Close environment */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Basic write transaction lifecycle
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_txn_write_lifecycle)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = 0;
    apr_size_t mapsize = DB_TEST_MAPSIZE_1MB;

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, mapsize);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_TXN, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);   /* No RDONLY flag = write */
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(txn);

    /* Verify transaction properties */
    ck_assert_ptr_eq(txn->env, env);
    ck_assert_ptr_nonnull(txn->pool);
    ck_assert_uint_eq(txn->flags, 0);
    ck_assert_uint_eq(txn->txnid, 2);   /* Initial DB has TXNID 1, write increments to 2 */
    ck_assert_uint_eq(txn->root_pgno, 0);       /* Empty tree */

    /* Commit write transaction */
    status = napr_db_txn_commit(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Close environment */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Write transaction abort
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_txn_write_abort)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = 0;
    apr_size_t mapsize = DB_TEST_MAPSIZE_1MB;

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, mapsize);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_TXN, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Abort write transaction */
    status = napr_db_txn_abort(txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Close environment */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Thread data structure for SWMR concurrency test
 */
struct thread_data
{
    napr_db_env_t *env;
    apr_time_t start_time;      /* Time when thread started trying to acquire lock */
    apr_time_t acquired_time;   /* Time when lock was acquired */
    int sequence;               /* Sequence number for this thread */
};

/*
 * Thread function for SWMR test
 * Tries to start a write transaction, records timing, commits, and exits
 */
static void *APR_THREAD_FUNC swmr_thread_func(apr_thread_t *thread, void *data)
{
    struct thread_data *tdata = (struct thread_data *) data;
    napr_db_txn_t *txn = NULL;
    apr_status_t status = 0;

    /* Record start time */
    tdata->start_time = apr_time_now();

    /* Begin write transaction (will block if another write is active) */
    status = napr_db_txn_begin(tdata->env, 0, &txn);
    if (status != APR_SUCCESS) {
        return NULL;
    }

    /* Record when we acquired the lock */
    tdata->acquired_time = apr_time_now();

    /* Hold the lock briefly */
    apr_sleep(apr_time_from_msec(100));

    /* Commit transaction (releases lock) */
    napr_db_txn_commit(txn);

    return NULL;
}

/*
 * Test: SWMR concurrency with intra-process locking
 * Verifies that write transactions are serialized via thread mutex
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_swmr_intraprocess)
{
    napr_db_env_t *env = NULL;
    apr_status_t status = 0;
    apr_size_t mapsize = DB_TEST_MAPSIZE_1MB;

    /* Create and open environment with INTRAPROCESS_LOCK */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, mapsize);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_TXN, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create thread data */
    struct thread_data tdata1 = {.env = env,.sequence = 1 };
    struct thread_data tdata2 = {.env = env,.sequence = 2 };

    /* Create threads */
    apr_thread_t *thread1 = NULL;
    apr_thread_t *thread2 = NULL;
    apr_threadattr_t *attr = NULL;

    status = apr_threadattr_create(&attr, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Start first thread */
    status = apr_thread_create(&thread1, attr, swmr_thread_func, &tdata1, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Sleep briefly to ensure thread1 starts first */
    apr_sleep(apr_time_from_msec(50));

    /* Start second thread (should block until thread1 commits) */
    status = apr_thread_create(&thread2, attr, swmr_thread_func, &tdata2, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Wait for both threads to complete */
    apr_status_t thread_status = APR_SUCCESS;
    apr_thread_join(&thread_status, thread1);
    apr_thread_join(&thread_status, thread2);

    /* Verify serialization: thread2 should acquire lock after thread1 releases it */
    /* thread1 acquired at acquired_time, held for 100ms */
    /* thread2 should not acquire until after thread1's acquired_time + 100ms */
    apr_time_t thread1_release_time = tdata1.acquired_time + apr_time_from_msec(100);

    /* thread2 should acquire after thread1 released */
    /* Allow some tolerance for scheduling delays */
    ck_assert(tdata2.acquired_time >= thread1_release_time - apr_time_from_msec(10));

    /* Close environment */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test: Multiple concurrent read transactions
 * Verifies that read transactions don't block each other
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_concurrent_readers)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn1 = NULL;
    napr_db_txn_t *txn2 = NULL;
    napr_db_txn_t *txn3 = NULL;
    apr_status_t status = 0;
    apr_size_t mapsize = DB_TEST_MAPSIZE_1MB;

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, mapsize);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_TXN, NAPR_DB_CREATE);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin multiple read transactions simultaneously */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn1);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn2);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn3);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* All should have the same snapshot TXNID */
    ck_assert_uint_eq(txn1->txnid, 1);
    ck_assert_uint_eq(txn2->txnid, 1);
    ck_assert_uint_eq(txn3->txnid, 1);

    /* Commit all read transactions */
    status = napr_db_txn_commit(txn1);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_commit(txn2);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_commit(txn3);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Close environment */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/*
 * Test suite setup
 */
Suite *make_db_txn_suite(void)
{
    Suite *suite = NULL;
    TCase *tc_core = NULL;

    suite = suite_create("DB Transactions");

    /* Core test case */
    tc_core = tcase_create("Core");
    tcase_add_checked_fixture(tc_core, setup, teardown);

    tcase_add_test(tc_core, test_txn_read_lifecycle);
    tcase_add_test(tc_core, test_txn_write_lifecycle);
    tcase_add_test(tc_core, test_txn_write_abort);
    tcase_add_test(tc_core, test_swmr_intraprocess);
    tcase_add_test(tc_core, test_concurrent_readers);

    suite_add_tcase(suite, tc_core);

    return suite;
}
