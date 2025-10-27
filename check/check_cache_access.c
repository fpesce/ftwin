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
 * @brief Create a test cache entry with specific values
 */
static napr_cache_entry_t make_test_entry(apr_time_t mtime, apr_time_t ctime, apr_off_t size, uint64_t hash_low, uint64_t hash_high)
{
    napr_cache_entry_t entry;
    entry.mtime = mtime;
    entry.ctime = ctime;
    entry.size = size;
    entry.hash.low64 = hash_low;
    entry.hash.high64 = hash_high;
    return entry;
}

/**
 * @brief Compare two cache entries for equality
 */
static int entries_equal(const napr_cache_entry_t *a, const napr_cache_entry_t *b)
{
    return (a->mtime == b->mtime && a->ctime == b->ctime && a->size == b->size && a->hash.low64 == b->hash.low64 && a->hash.high64 == b->hash.high64);
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
    const char *path = "/test/file1.txt";
    napr_cache_entry_t entry_in;
    const napr_cache_entry_t *entry_out = NULL;

    /* Create pool for transactions */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create test entry */
    entry_in = make_test_entry(100000, 100001, 12345, 0x1234567890ABCDEFULL, 0xFEDCBA0987654321ULL);

    /* Write transaction: upsert entry */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_upsert_in_txn(test_cache, path, &entry_in);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Read transaction: lookup entry */
    status = napr_cache_begin_read(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_lookup_in_txn(test_cache, path, &entry_out);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(entry_out);

    /* Verify entry data (confirms zero-copy) */
    ck_assert_msg(entries_equal(&entry_in, entry_out), "Entry data mismatch after lookup");

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
    const char *path = "/nonexistent/file.txt";
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
    const char *paths[] = { "/file1.txt", "/file2.txt", "/file3.txt" };
    napr_cache_entry_t entries[3];
    const napr_cache_entry_t *entry_out = NULL;
    int idx = 0;

    /* Create pool for transactions */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create test entries with different values */
    for (idx = 0; idx < 3; idx++) {
        entries[idx] = make_test_entry(200000 + idx, 200001 + idx, 10000 + idx * 1000, 0x1000 + idx, 0x2000 + idx);
    }

    /* Write transaction: upsert all entries */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (idx = 0; idx < 3; idx++) {
        status = napr_cache_upsert_in_txn(test_cache, paths[idx], &entries[idx]);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Read transaction: lookup and verify all entries */
    status = napr_cache_begin_read(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    for (idx = 0; idx < 3; idx++) {
        entry_out = NULL;
        status = napr_cache_lookup_in_txn(test_cache, paths[idx], &entry_out);
        ck_assert_int_eq(status, APR_SUCCESS);
        ck_assert_ptr_nonnull(entry_out);
        ck_assert_msg(entries_equal(&entries[idx], entry_out), "Entry %d data mismatch", idx);
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
    const char *path = "/test/update.txt";
    napr_cache_entry_t entry1, entry2;
    const napr_cache_entry_t *entry_out = NULL;

    /* Create pool for transactions */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create first entry */
    entry1 = make_test_entry(300000, 300001, 5000, 0xAAAA, 0xBBBB);

    /* Insert first version */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_upsert_in_txn(test_cache, path, &entry1);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create updated entry */
    entry2 = make_test_entry(400000, 400001, 6000, 0xCCCC, 0xDDDD);

    /* Update with second version */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_upsert_in_txn(test_cache, path, &entry2);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Verify updated entry */
    status = napr_cache_begin_read(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_lookup_in_txn(test_cache, path, &entry_out);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(entry_out);

    /* Should match entry2, not entry1 */
    ck_assert_msg(entries_equal(&entry2, entry_out), "Entry should be updated to second version");
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
    const char *path = "/test/persist.txt";
    napr_cache_entry_t entry_in;
    const napr_cache_entry_t *entry_out = NULL;

    /* Create pool for transactions */
    status = apr_pool_create(&txn_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Create test entry */
    entry_in = make_test_entry(500000, 500001, 7777, 0xDEADBEEF, 0xCAFEBABE);

    /* Write entry */
    status = napr_cache_begin_write(test_cache, txn_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_upsert_in_txn(test_cache, path, &entry_in);
    ck_assert_int_eq(status, APR_SUCCESS);

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

    status = napr_cache_lookup_in_txn(test_cache, path, &entry_out);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(entry_out);

    ck_assert_msg(entries_equal(&entry_in, entry_out), "Entry should persist across close/reopen");

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
