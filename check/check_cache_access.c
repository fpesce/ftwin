/**
 * @file check_cache_access.c
 * @brief Tests for napr_cache CRUD operations (lookup and upsert)
 */

#include <check.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <string.h>

#include "../src/napr_cache.h"
#include "check_cache_constants.h"

/* Global APR pool for tests */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static apr_pool_t *test_pool = NULL;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static napr_cache_t *test_cache = NULL;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static const char *test_cache_path = NULL;

/* ========================================================================
 * Test Setup and Teardown
 * ======================================================================== */

static void setup(void)
{
    apr_status_t status = APR_SUCCESS;
    const char *temp_dir = NULL;

    status = apr_pool_create(&test_pool, NULL);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create unique cache path */
    status = apr_temp_dir_get(&temp_dir, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    test_cache_path = apr_psprintf(test_pool, "%s/napr_cache_access_test_%d.db", temp_dir, (int) apr_time_now());

    /* Clean up any existing files */
    (void) apr_file_remove(test_cache_path, test_pool);
    (void) apr_file_remove(apr_psprintf(test_pool, "%s.lock", test_cache_path), test_pool);

    /* Open cache */
    status = napr_cache_open(&test_cache, test_cache_path, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(test_cache);
}

static void teardown(void)
{
    if (test_cache) {
        apr_status_t status = napr_cache_close(test_cache);
        ck_assert_int_eq(status, APR_SUCCESS);
        test_cache = NULL;
    }

    if (test_cache_path && test_pool) {
        (void) apr_file_remove(test_cache_path, test_pool);
        (void) apr_file_remove(apr_psprintf(test_pool, "%s.lock", test_cache_path), test_pool);
    }

    if (test_pool) {
        apr_pool_destroy(test_pool);
        test_pool = NULL;
    }
}

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

/**
 * @brief Arguments for make_test_entry
 */
typedef struct {
    apr_time_t mtime;       /**< Modification time */
    apr_time_t ctime;       /**< Creation time */
    apr_off_t size;         /**< File size */
    uint64_t hash_low;      /**< Low 64 bits of hash */
    uint64_t hash_high;     /**< High 64 bits of hash */
} make_test_entry_args;

/**
 * @brief Create a test cache entry with specific values
 */
static napr_cache_entry_t make_test_entry(const make_test_entry_args *args)
{
    napr_cache_entry_t entry;
    entry.mtime = args->mtime;
    entry.ctime = args->ctime;
    entry.size = args->size;
    entry.hash.low64 = args->hash_low;
    entry.hash.high64 = args->hash_high;
    return entry;
}

/**
 * @brief Compare two cache entries for equality
 */
static int entries_equal(const napr_cache_entry_t *first, const napr_cache_entry_t *second)
{
    return (first->mtime == second->mtime && first->ctime == second->ctime && first->size == second->size && first->hash.low64 == second->hash.low64 && first->hash.high64 == second->hash.high64);
}

/**
 * @brief Assert that a cache upsert operation succeeded.
 * @param cache The cache instance.
 * @param path The key for the upsert.
 * @param entry The entry to upsert.
 */
static void assert_upsert_succeeded(napr_cache_t *cache, const char *path, napr_cache_entry_t *entry)
{
    apr_status_t status = napr_cache_upsert_in_txn(cache, path, entry);
    ck_assert_int_eq(status, APR_SUCCESS);
}

/**
 * @brief Assert that a cache lookup operation succeeded and the returned entry matches the expected one.
 * @param cache The cache instance.
 * @param path The key for the lookup.
 * @param expected_entry The expected entry.
 */
static const napr_cache_entry_t *assert_lookup_succeeded(napr_cache_t *cache, const char *path, const napr_cache_entry_t *expected_entry)
{
    const napr_cache_entry_t *actual_entry = NULL;
    apr_status_t status = napr_cache_lookup_in_txn(cache, path, &actual_entry);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(actual_entry);
    ck_assert_msg(entries_equal(expected_entry, actual_entry), "Entry data mismatch for path: %s", path);
    return actual_entry;
}

/* ========================================================================
 * Transaction Tests
 * ======================================================================== */

/**
 * @test Test transaction wrapper functions
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_transaction_wrappers)
{
    apr_status_t status = APR_SUCCESS;
    apr_pool_t *txn_pool = NULL;

    /* Create pool for transaction */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Test read transaction */
    status = napr_cache_begin_read(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_end_read(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Test write transaction commit */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Test write transaction abort */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_abort_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    apr_pool_destroy(txn_pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* ========================================================================
 * CRUD Tests
 * ======================================================================== */

/**
 * @test Test basic upsert and lookup
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_upsert_and_lookup)
{
    apr_status_t status = APR_SUCCESS;
    apr_pool_t *txn_pool = NULL;
    const char *path = CACHE_TEST_PATH_FILE1;
    napr_cache_entry_t entry_in;
    const napr_cache_entry_t *entry_out = NULL;

    /* Create pool for transactions */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create test entry */
    entry_in = make_test_entry(&(make_test_entry_args){ .mtime = CACHE_TEST_BASIC_MTIME,
                                                        .ctime = CACHE_TEST_BASIC_CTIME,
                                                        .size = CACHE_TEST_BASIC_SIZE,
                                                        .hash_low = CACHE_TEST_BASIC_HASH_LOW,
                                                        .hash_high = CACHE_TEST_BASIC_HASH_HIGH });

    /* Write transaction: upsert entry */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    assert_upsert_succeeded(test_cache, path, &entry_in);

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Read transaction: lookup entry */
    status = napr_cache_begin_read(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    entry_out = assert_lookup_succeeded(test_cache, path, &entry_in);
    ck_assert_ptr_nonnull(entry_out);

    status = napr_cache_end_read(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    apr_pool_destroy(txn_pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @test Test lookup miss (non-existent key)
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_lookup_miss)
{
    apr_status_t status = APR_SUCCESS;
    apr_pool_t *txn_pool = NULL;
    const char *path = CACHE_TEST_PATH_NONEXISTENT;
    const napr_cache_entry_t *entry_out = NULL;

    /* Create pool for transaction */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Read transaction: lookup non-existent entry */
    status = napr_cache_begin_read(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_lookup_in_txn(test_cache, path, &entry_out);
    ck_assert_int_eq(status, APR_NOTFOUND);
    ck_assert_ptr_null(entry_out);

    status = napr_cache_end_read(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    apr_pool_destroy(txn_pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @test Test multiple upserts and lookups
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_multiple_entries)
{
    apr_status_t status = APR_SUCCESS;
    apr_pool_t *txn_pool = NULL;
    const char *paths[] = { CACHE_TEST_PATH_MULTI1, CACHE_TEST_PATH_MULTI2, CACHE_TEST_PATH_MULTI3 };
    napr_cache_entry_t entries[CACHE_TEST_MULTI_ENTRY_COUNT];
    int idx = 0;

    /* Create pool for transactions */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create test entries with different values */
    for (idx = 0; idx < CACHE_TEST_MULTI_ENTRY_COUNT; idx++) {
        entries[idx] = make_test_entry(&(make_test_entry_args){
            .mtime = CACHE_TEST_MULTI_BASE_MTIME + idx,
            .ctime = CACHE_TEST_MULTI_BASE_CTIME + idx,
            .size = CACHE_TEST_MULTI_BASE_SIZE + idx * CACHE_TEST_MULTI_SIZE_INCREMENT,
            .hash_low = CACHE_TEST_MULTI_BASE_HASH_LOW + idx,
            .hash_high = CACHE_TEST_MULTI_BASE_HASH_HIGH + idx,
        });
    }

    /* Write transaction: upsert all entries */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (idx = 0; idx < CACHE_TEST_MULTI_ENTRY_COUNT; idx++) {
        assert_upsert_succeeded(test_cache, paths[idx], &entries[idx]);
    }

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Read transaction: lookup and verify all entries */
    status = napr_cache_begin_read(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (idx = 0; idx < CACHE_TEST_MULTI_ENTRY_COUNT; idx++) {
        assert_lookup_succeeded(test_cache, paths[idx], &entries[idx]);
    }

    status = napr_cache_end_read(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    apr_pool_destroy(txn_pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @test Test upsert updates existing entry
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_upsert_update)
{
    apr_status_t status = APR_SUCCESS;
    apr_pool_t *txn_pool = NULL;
    const char *path = CACHE_TEST_PATH_UPDATE;
    napr_cache_entry_t entry1;
    napr_cache_entry_t entry2;
    const napr_cache_entry_t *entry_out = NULL;

    /* Create pool for transactions */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create first entry */
    entry1 = make_test_entry(&(make_test_entry_args){ .mtime = CACHE_TEST_UPDATE_V1_MTIME,
                                                      .ctime = CACHE_TEST_UPDATE_V1_CTIME,
                                                      .size = CACHE_TEST_UPDATE_V1_SIZE,
                                                      .hash_low = CACHE_TEST_UPDATE_V1_HASH_LOW,
                                                      .hash_high = CACHE_TEST_UPDATE_V1_HASH_HIGH });

    /* Insert first version */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    assert_upsert_succeeded(test_cache, path, &entry1);

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create updated entry */
    entry2 = make_test_entry(&(make_test_entry_args){ .mtime = CACHE_TEST_UPDATE_V2_MTIME,
                                                      .ctime = CACHE_TEST_UPDATE_V2_CTIME,
                                                      .size = CACHE_TEST_UPDATE_V2_SIZE,
                                                      .hash_low = CACHE_TEST_UPDATE_V2_HASH_LOW,
                                                      .hash_high = CACHE_TEST_UPDATE_V2_HASH_HIGH });

    /* Update with second version */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    assert_upsert_succeeded(test_cache, path, &entry2);

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify updated entry */
    status = napr_cache_begin_read(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    entry_out = assert_lookup_succeeded(test_cache, path, &entry2);
    ck_assert_msg(!entries_equal(&entry1, entry_out), "Entry should not match first version");

    status = napr_cache_end_read(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    apr_pool_destroy(txn_pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @test Test persistence across cache close/reopen
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_persistence)
{
    apr_status_t status = APR_SUCCESS;
    apr_pool_t *txn_pool = NULL;
    const char *path = CACHE_TEST_PATH_PERSIST;
    napr_cache_entry_t entry_in;
    const napr_cache_entry_t *entry_out = NULL;

    /* Create pool for transactions */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create test entry */
    entry_in = make_test_entry(&(make_test_entry_args){ .mtime = CACHE_TEST_PERSIST_MTIME,
                                                        .ctime = CACHE_TEST_PERSIST_CTIME,
                                                        .size = CACHE_TEST_PERSIST_SIZE,
                                                        .hash_low = CACHE_TEST_PERSIST_HASH_LOW,
                                                        .hash_high = CACHE_TEST_PERSIST_HASH_HIGH });

    /* Write entry */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    assert_upsert_succeeded(test_cache, path, &entry_in);

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Close cache */
    status = napr_cache_close(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);
    test_cache = NULL;

    /* Reopen cache */
    status = napr_cache_open(&test_cache, test_cache_path, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(test_cache);

    /* Verify entry still exists */
    status = napr_cache_begin_read(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    entry_out = assert_lookup_succeeded(test_cache, path, &entry_in);
    ck_assert_ptr_nonnull(entry_out);

    status = napr_cache_end_read(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    apr_pool_destroy(txn_pool);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* ========================================================================
 * Test Suite Setup
 * ======================================================================== */

Suite *cache_access_suite(void)
{
    Suite *suite = NULL;
    TCase *tc_txn = NULL;
    TCase *tc_crud = NULL;

    suite = suite_create("Cache Access");

    /* Transaction tests */
    tc_txn = tcase_create("Transactions");
    tcase_add_checked_fixture(tc_txn, setup, teardown);
    tcase_add_test(tc_txn, test_transaction_wrappers);
    suite_add_tcase(suite, tc_txn);

    /* CRUD tests */
    tc_crud = tcase_create("CRUD Operations");
    tcase_add_checked_fixture(tc_crud, setup, teardown);
    tcase_add_test(tc_crud, test_upsert_and_lookup);
    tcase_add_test(tc_crud, test_lookup_miss);
    tcase_add_test(tc_crud, test_multiple_entries);
    tcase_add_test(tc_crud, test_upsert_update);
    tcase_add_test(tc_crud, test_persistence);
    suite_add_tcase(suite, tc_crud);

    return suite;
}
