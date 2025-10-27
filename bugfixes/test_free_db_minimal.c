/**
 * @file test_free_db_minimal.c
 * @brief Minimal test to reproduce Free DB SIGBUS
 *
 * This is a standalone test that isolates the Free DB bug without running
 * the entire test suite. Compile with:
 *   gcc -g -O0 -I./src -I/usr/include/apr-1.0 \
 *       test_free_db_minimal.c src/napr_db.c src/napr_db_tree.c src/napr_db_cursor.c \
 *       -lapr-1 -laprutil-1 -o test_free_db_minimal
 *
 * Run with: ./test_free_db_minimal
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "napr_db.h"
#include "napr_db_internal.h"

#define TEST_DB_PATH "/tmp/test_free_db_minimal.db"
#define TEST_MAPSIZE (1024UL * 1024UL * 10UL)   /* 10MB */

int main(void)
{
    apr_pool_t *pool = NULL;
    napr_db_env_t *env = NULL;
    napr_db_txn_t *txn1 = NULL;
    napr_db_txn_t *txn2 = NULL;
    napr_db_val_t key = { 0 };
    napr_db_val_t data = { 0 };
    apr_status_t status = APR_SUCCESS;
    char key_buf[16] = { 0 };
    char data_buf[32] = { 0 };

    /* Initialize APR */
    apr_initialize();
    apr_pool_create(&pool, NULL);

    /* Remove old test database */
    apr_file_remove(TEST_DB_PATH, pool);

    printf("=== Minimal Free DB Test ===\n");

    /* Create and open environment */
    printf("1. Creating environment...\n");
    status = napr_db_env_create(&env, pool);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to create env: %d\n", status);
        return 1;
    }

    status = napr_db_env_set_mapsize(env, TEST_MAPSIZE);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to set mapsize: %d\n", status);
        return 1;
    }

    status = napr_db_env_open(env, TEST_DB_PATH, NAPR_DB_CREATE | NAPR_DB_INTRAPROCESS_LOCK);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to open env: %d\n", status);
        return 1;
    }
    printf("   Environment opened successfully\n");

    /* Transaction 1: Insert initial data (no CoW, no Free DB) */
    printf("2. Transaction 1: Inserting initial data...\n");
    status = napr_db_txn_begin(env, 0, &txn1);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to begin txn1: %d\n", status);
        return 1;
    }

    snprintf(key_buf, sizeof(key_buf), "key1");
    key.data = key_buf;
    key.size = strlen(key_buf) + 1;
    snprintf(data_buf, sizeof(data_buf), "value1");
    data.data = data_buf;
    data.size = strlen(data_buf) + 1;

    status = napr_db_put(txn1, &key, &data);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to put in txn1: %d\n", status);
        return 1;
    }

    printf("   Committing txn1 (should NOT populate Free DB)...\n");
    printf("   freed_pages count: %d\n", txn1->freed_pages ? txn1->freed_pages->nelts : -1);
    status = napr_db_txn_commit(txn1);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to commit txn1: %d\n", status);
        return 1;
    }
    printf("   Transaction 1 committed successfully\n");

    /* Transaction 2: Insert many keys to trigger page split (triggers CoW) */
    printf("3. Transaction 2: Inserting many keys to trigger split (triggers CoW)...\n");
    status = napr_db_txn_begin(env, 0, &txn2);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to begin txn2: %d\n", status);
        return 1;
    }

    printf("   txn2->free_db_root_pgno = %lu\n", txn2->free_db_root_pgno);
    printf("   txn2->new_last_pgno = %lu\n", txn2->new_last_pgno);
    printf("   Inserting 100 keys to force page splits...\n");

    /* Insert many keys to force page splits, which will free old pages */
    for (int i = 2; i <= 100; i++) {
        snprintf(key_buf, sizeof(key_buf), "key%04d", i);
        key.data = key_buf;
        key.size = strlen(key_buf) + 1;
        snprintf(data_buf, sizeof(data_buf), "value%04d", i);
        data.data = data_buf;
        data.size = strlen(data_buf) + 1;

        status = napr_db_put(txn2, &key, &data);
        if (status != APR_SUCCESS) {
            fprintf(stderr, "ERROR: Failed to put key%04d in txn2: %d\n", i, status);
            return 1;
        }
    }

    printf("   After inserts, freed_pages count: %d\n", txn2->freed_pages ? txn2->freed_pages->nelts : -1);
    if (txn2->freed_pages && txn2->freed_pages->nelts > 0) {
        printf("   Freed pages: ");
        for (int i = 0; i < txn2->freed_pages->nelts; i++) {
            pgno_t *freed = (pgno_t *) txn2->freed_pages->elts + i;
            printf("%lu ", *freed);
        }
        printf("\n");
    }

    printf("   Committing txn2 (SHOULD populate Free DB)...\n");
    printf("   This is where the SIGBUS typically occurs...\n");
    fflush(stdout);
    status = napr_db_txn_commit(txn2);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to commit txn2: %d\n", status);
        return 1;
    }
    printf("   Transaction 2 committed successfully\n");

    /* Verify Free DB was populated */
    printf("4. Verifying Free DB root is set...\n");
    printf("   live_meta->free_db_root = %lu\n", env->live_meta->free_db_root);

    /* Close and reopen database (simulate what test suite does) */
    printf("5. Closing and reopening database...\n");
    status = napr_db_env_close(env);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to close env: %d\n", status);
        return 1;
    }

    /* Reopen */
    printf("6. Reopening database with existing Free DB...\n");
    status = napr_db_env_create(&env, pool);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to create env on reopen: %d\n", status);
        return 1;
    }

    status = napr_db_env_set_mapsize(env, TEST_MAPSIZE);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to set mapsize on reopen: %d\n", status);
        return 1;
    }

    status = napr_db_env_open(env, TEST_DB_PATH, NAPR_DB_INTRAPROCESS_LOCK);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to open env on reopen: %d\n", status);
        return 1;
    }
    printf("   Database reopened successfully\n");
    printf("   live_meta->free_db_root = %lu\n", env->live_meta->free_db_root);
    printf("   live_meta->last_pgno = %lu\n", env->live_meta->last_pgno);

    /* Try to do another transaction that triggers CoW */
    printf("7. Transaction 3: Another transaction to trigger Free DB usage...\n");
    napr_db_txn_t *txn3 = NULL;
    status = napr_db_txn_begin(env, 0, &txn3);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to begin txn3: %d\n", status);
        return 1;
    }

    printf("   txn3->free_db_root_pgno = %lu\n", txn3->free_db_root_pgno);
    printf("   txn3->new_last_pgno = %lu\n", txn3->new_last_pgno);

    /* Insert more keys to trigger more splits and Free DB activity */
    printf("   Inserting 50 more keys...\n");
    for (int i = 200; i < 250; i++) {
        snprintf(key_buf, sizeof(key_buf), "key%04d", i);
        key.data = key_buf;
        key.size = strlen(key_buf) + 1;
        snprintf(data_buf, sizeof(data_buf), "value%04d", i);
        data.data = data_buf;
        data.size = strlen(data_buf) + 1;

        status = napr_db_put(txn3, &key, &data);
        if (status != APR_SUCCESS) {
            fprintf(stderr, "ERROR: Failed to put key%04d in txn3: %d\n", i, status);
            return 1;
        }
    }

    printf("   After inserts, freed_pages count: %d\n", txn3->freed_pages ? txn3->freed_pages->nelts : -1);

    printf("   Committing txn3 (will add to existing Free DB)...\n");
    printf("   THIS IS THE CRITICAL TEST - does it crash?\n");
    fflush(stdout);
    status = napr_db_txn_commit(txn3);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to commit txn3: %d\n", status);
        return 1;
    }
    printf("   Transaction 3 committed successfully!\n");
    printf("   live_meta->free_db_root = %lu\n", env->live_meta->free_db_root);

    /* Clean up */
    printf("8. Final cleanup...\n");
    status = napr_db_env_close(env);
    if (status != APR_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to close env: %d\n", status);
        return 1;
    }

    apr_pool_destroy(pool);
    apr_terminate();

    printf("\n=== TEST PASSED - NO SIGBUS! ===\n");
    return 0;
}
