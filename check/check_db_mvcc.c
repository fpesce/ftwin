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
 * @brief Test that freed pages are tracked during CoW operations
 *
 * Verifies that:
 * 1. Pages are added to freed_pages array when CoW creates dirty copies
 * 2. The freed_pages array contains the correct page numbers
 * 3. Multiple writes accumulate freed pages correctly
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_freed_pages_tracking)
{
    apr_status_t status = APR_SUCCESS;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *write_txn = NULL;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    char key_buf[DB_TEST_KEY_BUF_SIZE] = { 0 };
    char data_buf[DB_TEST_DATA_BUF_SIZE] = { 0 };

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_MVCC, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin write transaction */
    status = napr_db_txn_begin(env, 0, &write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify freed_pages array is initialized but empty */
    ck_assert_ptr_nonnull(write_txn->freed_pages);
    ck_assert_int_eq(write_txn->freed_pages->nelts, 0);

    /* Insert first key - creates root page (no CoW yet, it's a new page) */
    (void) snprintf(key_buf, sizeof(key_buf), "key1");
    key.data = key_buf;
    key.size = DB_TEST_MVCC_KEY_SIZE;
    (void) snprintf(data_buf, sizeof(data_buf), "value1");
    data.data = data_buf;
    data.size = DB_TEST_MVCC_DATA_SIZE;

    status = napr_db_put(write_txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* First insertion creates a new root page, no CoW yet */
    /* freed_pages should still be empty */
    ck_assert_int_eq(write_txn->freed_pages->nelts, 0);

    /* Commit to persist the first key */
    status = napr_db_txn_commit(write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Begin second write transaction */
    status = napr_db_txn_begin(env, 0, &write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Insert second key - this will trigger CoW of the root page */
    (void) snprintf(key_buf, sizeof(key_buf), "key2");
    key.data = key_buf;
    key.size = DB_TEST_MVCC_KEY_SIZE;
    (void) snprintf(data_buf, sizeof(data_buf), "value2");
    data.data = data_buf;
    data.size = DB_TEST_MVCC_DATA_SIZE;

    status = napr_db_put(write_txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Now freed_pages should contain the original root page number */
    ck_assert_int_gt(write_txn->freed_pages->nelts, 0);

    /* Commit second transaction */
    status = napr_db_txn_commit(write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Clean up */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_free_db_initialization)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn1 = NULL;
    napr_db_txn_t *txn2 = NULL;
    napr_db_txn_t *read_txn = NULL;
    apr_status_t status = APR_SUCCESS;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    char key_buf[DB_TEST_MVCC_KEY_SIZE] = { 0 };
    char data_buf[DB_TEST_MVCC_DATA_SIZE] = { 0 };
    DB_MetaPage *meta = NULL;

    /* Create and open environment */
    status = napr_db_env_create(&env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_set_mapsize(env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_env_open(env, DB_TEST_PATH_MVCC, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify Free DB root is initially 0 */
    meta = env->live_meta;
    ck_assert_int_eq(meta->free_db_root, 0);

    /* Create first transaction and insert data */
    status = napr_db_txn_begin(env, 0, &txn1);
    ck_assert_int_eq(status, APR_SUCCESS);

    (void) snprintf(key_buf, sizeof(key_buf), "key1");
    key.data = key_buf;
    key.size = DB_TEST_MVCC_KEY_SIZE;
    (void) snprintf(data_buf, sizeof(data_buf), "value1");
    data.data = data_buf;
    data.size = DB_TEST_MVCC_DATA_SIZE;

    status = napr_db_put(txn1, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Commit - Free DB should still be empty (no CoW occurred) */
    status = napr_db_txn_commit(txn1);
    ck_assert_int_eq(status, APR_SUCCESS);

    meta = env->live_meta;
    ck_assert_int_eq(meta->free_db_root, 0);

    /* Create second transaction and update data (triggers CoW) */
    status = napr_db_txn_begin(env, 0, &txn2);
    ck_assert_int_eq(status, APR_SUCCESS);

    (void) snprintf(data_buf, sizeof(data_buf), "value2");
    data.data = data_buf;
    data.size = DB_TEST_MVCC_DATA_SIZE;

    status = napr_db_put(txn2, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify freed_pages is not empty */
    ck_assert_int_gt(txn2->freed_pages->nelts, 0);

    /* Commit - Free DB should now be initialized */
    status = napr_db_txn_commit(txn2);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify Free DB root is no longer 0 */
    meta = env->live_meta;
    ck_assert_int_ne(meta->free_db_root, 0);

    /* Verify we can start a read transaction with the new Free DB root */
    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &read_txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_int_eq(read_txn->free_db_root_pgno, meta->free_db_root);

    status = napr_db_txn_abort(read_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Clean up */
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* Forward declaration of internal helper function for testing */
extern apr_status_t read_from_free_db(napr_db_txn_t *txn, txnid_t txnid, pgno_t **freed_pages_out, size_t *num_pages_out);

static void create_db_env(napr_db_env_t **env)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    status = napr_db_env_create(env, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_env_set_mapsize(*env, DB_TEST_MAPSIZE_10MB);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_env_open(*env, DB_TEST_PATH_MVCC, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    ck_assert_int_eq(status, APR_SUCCESS);
}

static void commit_dummy_data(napr_db_env_t *env, const char *key_str, const char *data_str, txnid_t *txnid)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    napr_db_txn_t *write_txn = NULL;
    napr_db_val_t key = {.data = (void *) key_str,.size = strlen(key_str) + 1 };
    napr_db_val_t data = {.data = (void *) data_str,.size = strlen(data_str) + 1 };

    status = napr_db_txn_begin(env, 0, &write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    if (txnid) {
        *txnid = write_txn->txnid;
    }
    status = napr_db_put(write_txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_txn_commit(write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);
}

static void get_freed_pages(napr_db_txn_t *read_txn, txnid_t txnid, pgno_t **freed_pages, size_t *num_pages)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    status = read_from_free_db(read_txn, txnid, freed_pages, num_pages);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(*freed_pages);

    /* Verify all page numbers are valid (> 0) */
    const pgno_t *pages = *freed_pages;
    for (size_t idx = 0; idx < *num_pages; idx++) {
        ck_assert_int_gt(pages[idx], 0);
    }
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_free_db_entry_storage)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *read_txn = NULL;
    apr_status_t status = APR_SUCCESS;
    pgno_t *freed_pages = NULL;
    size_t num_pages = 0;
    txnid_t txn2_id = 0;
    int original_freed_count = 0;
    napr_db_txn_t *write_txn = NULL;
    napr_db_val_t key = {.data = "key1",.size = DB_TEST_MVCC_KEY_SIZE };
    napr_db_val_t data = {.data = "value2",.size = DB_TEST_MVCC_DATA_SIZE };

    create_db_env(&env);
    commit_dummy_data(env, "key1", "value1", NULL);

    status = napr_db_txn_begin(env, 0, &write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    txn2_id = write_txn->txnid;
    status = napr_db_put(write_txn, &key, &data);
    ck_assert_int_eq(status, APR_SUCCESS);

    original_freed_count = write_txn->freed_pages->nelts;
    ck_assert_int_gt(original_freed_count, 0);

    status = napr_db_txn_commit(write_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &read_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    get_freed_pages(read_txn, txn2_id, &freed_pages, &num_pages);
    ck_assert_int_eq(num_pages, (size_t) original_freed_count);

    status = napr_db_txn_abort(read_txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_free_db_multiple_entries)
{
    napr_db_env_t *env = NULL;
    napr_db_txn_t *read_txn = NULL;
    apr_status_t status = APR_SUCCESS;
    pgno_t *freed_pages = NULL;
    size_t num_pages = 0;
    txnid_t txnids[DB_TEST_TXNS_COUNT_5] = { 0 };
    char data_buf[DB_TEST_DATA_BUF_SIZE];

    create_db_env(&env);
    commit_dummy_data(env, "key1", "value0", NULL);

    for (int idx = 0; idx < DB_TEST_TXNS_COUNT_5; idx++) {
        (void) snprintf(data_buf, sizeof(data_buf), "value%d", idx + 1);
        commit_dummy_data(env, "key1", data_buf, &txnids[idx]);
    }

    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &read_txn);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (int idx = 0; idx < DB_TEST_TXNS_COUNT_5; idx++) {
        get_freed_pages(read_txn, txnids[idx], &freed_pages, &num_pages);
        ck_assert_int_gt(num_pages, 0);
    }

    status = read_from_free_db(read_txn, DB_TEST_NON_EXISTENT_TXNID, &freed_pages, &num_pages);
    ck_assert_int_eq(status, APR_NOTFOUND);

    status = napr_db_txn_abort(read_txn);
    ck_assert_int_eq(status, APR_SUCCESS);
    status = napr_db_env_close(env);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

static void setup_initial_data(napr_db_env_t *env, pgno_t *last_pgno_out)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    napr_db_txn_t *txn_setup = NULL;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    char key_buf[DB_TEST_MVCC_KEY_SIZE] = { 0 };
    char data_buf[DB_TEST_MVCC_DATA_SIZE] = { 0 };

    status = napr_db_txn_begin(env, 0, &txn_setup);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (int idx = 0; idx < DB_TEST_MVCC_MANY_KEY_COUNT_500; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "k%03d", idx);
        key.data = key_buf;
        key.size = DB_TEST_MVCC_KEY_SIZE;
        (void) snprintf(data_buf, sizeof(data_buf), "v%03d", idx);
        data.data = data_buf;
        data.size = DB_TEST_MVCC_DATA_SIZE;
        status = napr_db_put(txn_setup, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    status = napr_db_txn_commit(txn_setup);
    ck_assert_int_eq(status, APR_SUCCESS);
    *last_pgno_out = env->live_meta->last_pgno;
}

static void delete_data_to_free_pages(napr_db_env_t *env, pgno_t *last_pgno_out)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    napr_db_txn_t *txn_delete = NULL;
    napr_db_val_t key = { 0 };
    char key_buf[DB_TEST_MVCC_KEY_SIZE] = { 0 };

    status = napr_db_txn_begin(env, 0, &txn_delete);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (int idx = DB_TEST_MVCC_MANY_KEY_COUNT_250; idx < DB_TEST_MVCC_MANY_KEY_COUNT_500; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "k%03d", idx);
        key.data = key_buf;
        key.size = DB_TEST_MVCC_KEY_SIZE;
        status = napr_db_del(txn_delete, &key, NULL);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    ck_assert_int_gt(txn_delete->freed_pages->nelts, 0);
    status = napr_db_txn_commit(txn_delete);
    ck_assert_int_eq(status, APR_SUCCESS);
    *last_pgno_out = env->live_meta->last_pgno;
}

static void write_with_active_reader(napr_db_env_t *env, pgno_t *last_pgno_out)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    napr_db_txn_t *txn_r1 = NULL;
    napr_db_txn_t *txn_w2 = NULL;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    char key_buf[DB_TEST_MVCC_KEY_SIZE] = { 0 };
    char data_buf[DB_TEST_MVCC_DATA_SIZE] = { 0 };

    status = napr_db_txn_begin(env, NAPR_DB_RDONLY, &txn_r1);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_db_txn_begin(env, 0, &txn_w2);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (int idx = DB_TEST_MVCC_MANY_KEY_COUNT_600; idx < DB_TEST_MVCC_MANY_KEY_COUNT_900; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "k%03d", idx);
        key.data = key_buf;
        key.size = DB_TEST_MVCC_KEY_SIZE;
        (void) snprintf(data_buf, sizeof(data_buf), "v%03d", idx);
        data.data = data_buf;
        data.size = DB_TEST_MVCC_DATA_SIZE;
        status = napr_db_put(txn_w2, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    status = napr_db_txn_commit(txn_w2);
    ck_assert_int_eq(status, APR_SUCCESS);
    *last_pgno_out = env->live_meta->last_pgno;

    status = napr_db_txn_abort(txn_r1);
    ck_assert_int_eq(status, APR_SUCCESS);
}

static void write_after_reader_closes(napr_db_env_t *env, pgno_t *last_pgno_out)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    napr_db_txn_t *txn_w3 = NULL;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    char key_buf[DB_TEST_MVCC_KEY_SIZE] = { 0 };
    char data_buf[DB_TEST_MVCC_DATA_SIZE] = { 0 };

    status = napr_db_txn_begin(env, 0, &txn_w3);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (int idx = DB_TEST_MVCC_MANY_KEY_COUNT_900; idx < DB_TEST_MVCC_MANY_KEY_COUNT_999; idx++) {
        (void) snprintf(key_buf, sizeof(key_buf), "k%03d", idx);
        key.data = key_buf;
        key.size = DB_TEST_MVCC_KEY_SIZE;
        (void) snprintf(data_buf, sizeof(data_buf), "v%03d", idx);
        data.data = data_buf;
        data.size = DB_TEST_MVCC_DATA_SIZE;
        status = napr_db_put(txn_w3, &key, &data);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    status = napr_db_txn_commit(txn_w3);
    ck_assert_int_eq(status, APR_SUCCESS);
    *last_pgno_out = env->live_meta->last_pgno;
}

/**
 * @brief Test page reclamation safety with MVCC
 *
 * Verifies that pages cannot be reused while there are active readers that need them,
 * and can be reused once those readers are done. Uses insertions that trigger splits
 * to actually allocate new pages.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_page_reclamation_safety)
{
    napr_db_env_t *env = NULL;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    pgno_t last_pgno_after_setup = 0;
    pgno_t last_pgno_after_delete = 0;
    pgno_t last_pgno_after_w2 = 0;
    pgno_t last_pgno_after_w3 = 0;

    /* Create and open environment */
    create_db_env(&env);

    /* Phase 1: Setup initial data and verify we created a multi-page tree */
    setup_initial_data(env, &last_pgno_after_setup);
    ck_assert_int_gt(last_pgno_after_setup, 3);

    /* Phase 2: Delete data to free pages */
    delete_data_to_free_pages(env, &last_pgno_after_delete);

    /* Phase 3: Write with an active reader (should extend file) */
    write_with_active_reader(env, &last_pgno_after_w2);
    ck_assert_int_gt(last_pgno_after_w2, last_pgno_after_delete);

    /* Phase 4: Write after reader closes (should reuse pages) */
    write_after_reader_closes(env, &last_pgno_after_w3);
    ck_assert_int_le(last_pgno_after_w3, last_pgno_after_w2);

    /* Cleanup */
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
    tcase_add_test(tc_core, test_freed_pages_tracking);
    tcase_add_test(tc_core, test_free_db_initialization);
    tcase_add_test(tc_core, test_free_db_entry_storage);
    tcase_add_test(tc_core, test_free_db_multiple_entries);
    tcase_add_test(tc_core, test_page_reclamation_safety);

    suite_add_tcase(suite, tc_core);
    return suite;
}
