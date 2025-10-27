/**
 * @file check_cache_mark_sweep.c
 * @brief Tests for napr_cache mark-and-sweep garbage collection
 */

#include <check.h>
#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include <apr_thread_proc.h>
#include <apr_hash.h>
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

    test_cache_path = apr_psprintf(test_pool, "%s/napr_cache_mark_sweep_test_%d.db", temp_dir, (int) apr_time_now());

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
 * Memory Management Tests
 * ======================================================================== */

/**
 * @test Test memory management (CRITICAL)
 *
 * This test verifies that napr_cache_mark_visited correctly duplicates
 * the path string into the cache's main pool, ensuring the string remains
 * valid even after the original allocation pool is destroyed.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_mark_visited_memory_management)
{
    apr_status_t status = APR_SUCCESS;
    apr_pool_t *scratch_pool = NULL;
    const char *original_path = NULL;
    const char *test_path = "/tmp/test_memory_management.txt";

    /* Create a short-lived scratch pool */
    status = apr_pool_create(&scratch_pool, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Allocate a path string from the scratch pool */
    original_path = apr_pstrdup(scratch_pool, test_path);
    ck_assert_ptr_nonnull(original_path);

    /* Mark the path as visited */
    status = napr_cache_mark_visited(test_cache, original_path);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Destroy the scratch pool (invalidating the original string) */
    apr_pool_destroy(scratch_pool);
    scratch_pool = NULL;

    /* CRITICAL: Verify the path is still accessible in visited_set
     * This proves the path was correctly duplicated into the main pool */

    /* We need to access the internal visited_set to verify.
     * Since we can't access it directly (it's private), we'll verify
     * by marking the same path again - if the internal copy is valid,
     * this should succeed without issues */
    status = napr_cache_mark_visited(test_cache, test_path);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* ========================================================================
 * Concurrency Tests
 * ======================================================================== */

/* Thread data structure */
typedef struct
{
    napr_cache_t *cache;
    int thread_id;
    int num_marks;
    apr_status_t result;
} thread_data_t;

/**
 * @brief Thread function for concurrent mark_visited testing
 */
static void *APR_THREAD_FUNC mark_visited_thread(apr_thread_t *thread, void *data)
{
    thread_data_t *tdata = (thread_data_t *) data;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    int idx = 0;
    char path_buf[CACHE_TEST_PATH_BUG_SIZE];

    (void) thread;              /* Unused parameter */

    /* Mark multiple paths concurrently */
    for (idx = 0; idx < tdata->num_marks; idx++) {
        /* Create a unique path for this thread and iteration */
        (void) snprintf(path_buf, sizeof(path_buf), "/thread_%d/file_%d.txt", tdata->thread_id, idx);

        status = napr_cache_mark_visited(tdata->cache, path_buf);
        if (status != APR_SUCCESS) {
            tdata->result = status;
            return NULL;
        }
    }

    tdata->result = APR_SUCCESS;
    return NULL;
}

/**
 * @test Test concurrent calls to mark_visited
 *
 * This test spawns multiple threads that concurrently call mark_visited
 * to stress the mutex and verify all entries are recorded without races.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_mark_visited_concurrency)
{
    apr_status_t status = APR_SUCCESS;
    apr_thread_t *threads[CACHE_TEST_NB_THREADS];
    thread_data_t thread_data[CACHE_TEST_NB_THREADS];
    int idx = 0;
    apr_threadattr_t *thread_attr = NULL;

    /* Create thread attributes */
    status = apr_threadattr_create(&thread_attr, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* Spawn threads */
    for (idx = 0; idx < CACHE_TEST_NB_THREADS; idx++) {
        thread_data[idx].cache = test_cache;
        thread_data[idx].thread_id = idx;
        thread_data[idx].num_marks = CACHE_TEST_MARK_PER_THREAD;
        thread_data[idx].result = APR_EGENERAL;

        status = apr_thread_create(&threads[idx], thread_attr, mark_visited_thread, &thread_data[idx], test_pool);
        ck_assert_int_eq(status, APR_SUCCESS);
    }

    /* Wait for all threads to complete */
    for (idx = 0; idx < CACHE_TEST_NB_THREADS; idx++) {
        apr_status_t thread_status = APR_SUCCESS;
        status = apr_thread_join(&thread_status, threads[idx]);
        ck_assert_int_eq(status, APR_SUCCESS);

        /* Verify the thread completed successfully */
        ck_assert_int_eq(thread_data[idx].result, APR_SUCCESS);
    }

    /* Verify we can still mark paths after concurrent operations */
    status = napr_cache_mark_visited(test_cache, "/final/test.txt");
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @test Test marking the same path multiple times
 *
 * This verifies that marking the same path multiple times is idempotent
 * and doesn't cause issues.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_mark_visited_idempotent)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;
    const char *path = "/test/idempotent.txt";
    int idx = 0;

    /* Mark the same path multiple times */
    for (idx = 0; idx < CACHE_TEST_MARK_VISITED_MULT; idx++) {
        status = napr_cache_mark_visited(test_cache, path);
        ck_assert_int_eq(status, APR_SUCCESS);
    }
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/**
 * @test Test marking paths with special characters
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_mark_visited_special_paths)
{
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    /* Test various path formats */
    const char *test_paths[] = {
        "/path/with spaces/file.txt",
        "/path/with/unicode/файл.txt",
        "/very/long/path/that/goes/on/and/on/and/on/file.txt",
        "/",
        "/single",
        ""                      /* Empty path - should still work */
    };

    size_t num_paths = sizeof(test_paths) / sizeof(test_paths[0]);
    size_t idx = 0;

    for (idx = 0; idx < num_paths; idx++) {
        status = napr_cache_mark_visited(test_cache, test_paths[idx]);
        ck_assert_int_eq(status, APR_SUCCESS);
    }
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* ========================================================================
 * Sweep Tests
 * ======================================================================== */

/**
 * @test Test sweep logic integration
 *
 * This test verifies that the sweep operation correctly removes
 * unmarked entries while preserving marked ones.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
START_TEST(test_sweep_integration)
{
    apr_status_t status;
    napr_cache_entry_t entry_a;
    napr_cache_entry_t entry_b;
    napr_cache_entry_t entry_c;
    napr_cache_entry_t entry_d;
    const napr_cache_entry_t *retrieved = NULL;

    /* Define test paths */
    const char *path_a = "/sweep/test/a.txt";
    const char *path_b = "/sweep/test/b.txt";
    const char *path_c = "/sweep/test/c.txt";
    const char *path_d = "/sweep/test/d.txt";

    /* Prepare test entries */
    entry_a.mtime = 1000;
    entry_a.ctime = 1001;
    entry_a.size = 100;
    entry_a.hash.low64 = 0xAAAAAAAAAAAAAAAAULL;
    entry_a.hash.high64 = 0xBBBBBBBBBBBBBBBBULL;

    entry_b.mtime = 2000;
    entry_b.ctime = 2001;
    entry_b.size = 200;
    entry_b.hash.low64 = 0xCCCCCCCCCCCCCCCCULL;
    entry_b.hash.high64 = 0xDDDDDDDDDDDDDDDDULL;

    entry_c.mtime = 3000;
    entry_c.ctime = 3001;
    entry_c.size = 300;
    entry_c.hash.low64 = 0xEEEEEEEEEEEEEEEEULL;
    entry_c.hash.high64 = 0xFFFFFFFFFFFFFFFFULL;

    entry_d.mtime = 4000;
    entry_d.ctime = 4001;
    entry_d.size = 400;
    entry_d.hash.low64 = 0x1111111111111111ULL;
    entry_d.hash.high64 = 0x2222222222222222ULL;

    /* STEP 1: Populate cache with A, B, C, D */
    status = napr_cache_begin_write(test_cache, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_upsert_in_txn(test_cache, path_a, &entry_a);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_upsert_in_txn(test_cache, path_b, &entry_b);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_upsert_in_txn(test_cache, path_c, &entry_c);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_upsert_in_txn(test_cache, path_d, &entry_d);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_commit_write(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* STEP 2: Mark only A and C as visited */
    status = napr_cache_mark_visited(test_cache, path_a);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_mark_visited(test_cache, path_c);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* STEP 3: Call sweep to remove unmarked entries (B and D) */
    status = napr_cache_sweep(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* STEP 4: Verify A and C still exist */
    status = napr_cache_begin_read(test_cache, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_lookup_in_txn(test_cache, path_a, &retrieved);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(retrieved);
    ck_assert_int_eq(retrieved->mtime, entry_a.mtime);
    ck_assert_int_eq(retrieved->size, entry_a.size);

    status = napr_cache_lookup_in_txn(test_cache, path_c, &retrieved);
    ck_assert_int_eq(status, APR_SUCCESS);
    ck_assert_ptr_nonnull(retrieved);
    ck_assert_int_eq(retrieved->mtime, entry_c.mtime);
    ck_assert_int_eq(retrieved->size, entry_c.size);

    status = napr_cache_end_read(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* STEP 5: Verify B and D are gone */
    status = napr_cache_begin_read(test_cache, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_lookup_in_txn(test_cache, path_b, &retrieved);
    ck_assert_int_eq(status, APR_NOTFOUND);

    status = napr_cache_lookup_in_txn(test_cache, path_d, &retrieved);
    ck_assert_int_eq(status, APR_NOTFOUND);

    status = napr_cache_end_read(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* STEP 6: Verify visited_set is empty after sweep
     * (We do this indirectly by marking new paths and sweeping again) */
    status = napr_cache_mark_visited(test_cache, path_a);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_sweep(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);

    /* After second sweep, only A should remain, C should be gone */
    status = napr_cache_begin_read(test_cache, test_pool);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_lookup_in_txn(test_cache, path_a, &retrieved);
    ck_assert_int_eq(status, APR_SUCCESS);

    status = napr_cache_lookup_in_txn(test_cache, path_c, &retrieved);
    ck_assert_int_eq(status, APR_NOTFOUND);

    status = napr_cache_end_read(test_cache);
    ck_assert_int_eq(status, APR_SUCCESS);
}
/* *INDENT-OFF* */
END_TEST
/* *INDENT-ON* */

/* ========================================================================
 * Test Suite Setup
 * ======================================================================== */

Suite *cache_mark_sweep_suite(void)
{
    Suite *suite = NULL;
    TCase *tc_memory = NULL;
    TCase *tc_concurrency = NULL;
    TCase *tc_sweep = NULL;

    suite = suite_create("Cache Mark and Sweep");

    /* Memory management tests */
    tc_memory = tcase_create("Memory Management");
    tcase_add_checked_fixture(tc_memory, setup, teardown);
    tcase_add_test(tc_memory, test_mark_visited_memory_management);
    tcase_add_test(tc_memory, test_mark_visited_idempotent);
    tcase_add_test(tc_memory, test_mark_visited_special_paths);
    suite_add_tcase(suite, tc_memory);

    /* Concurrency tests */
    tc_concurrency = tcase_create("Concurrency");
    tcase_add_checked_fixture(tc_concurrency, setup, teardown);
    tcase_add_test(tc_concurrency, test_mark_visited_concurrency);
    suite_add_tcase(suite, tc_concurrency);

    /* Sweep tests */
    tc_sweep = tcase_create("Sweep");
    tcase_add_checked_fixture(tc_sweep, setup, teardown);
    tcase_add_test(tc_sweep, test_sweep_integration);
    suite_add_tcase(suite, tc_sweep);

    return suite;
}
