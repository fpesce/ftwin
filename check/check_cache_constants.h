/**
 * @file check_cache_constants.h
 * @brief Constants for napr_cache tests
 */
#ifndef CHECK_CACHE_CONSTANTS_H
#define CHECK_CACHE_CONSTANTS_H

#include <stdint.h>

/*
 * =================================================================
 * Cache Entry Size and Layout (Critical for Zero-Copy Safety)
 * =================================================================
 */

#define CACHE_ENTRY_SIZE_BYTES 40
#define CACHE_ENTRY_OFFSET_MTIME 0
#define CACHE_ENTRY_OFFSET_CTIME 8
#define CACHE_ENTRY_OFFSET_SIZE 16
#define CACHE_ENTRY_OFFSET_HASH 24
#define CACHE_ENTRY_HASH_SIZE 16
#define CACHE_ENTRY_APR_TIME_SIZE 8
#define CACHE_ENTRY_APR_OFF_SIZE 8

/*
 * =================================================================
 * Test File Paths (Keys in Cache)
 * =================================================================
 */

#define CACHE_TEST_PATH_BUG_SIZE 256
#define CACHE_TEST_PATH_FILE1 "/test/file1.txt"
#define CACHE_TEST_PATH_NONEXISTENT "/nonexistent/file.txt"
#define CACHE_TEST_PATH_MULTI1 "/file1.txt"
#define CACHE_TEST_PATH_MULTI2 "/file2.txt"
#define CACHE_TEST_PATH_MULTI3 "/file3.txt"
#define CACHE_TEST_PATH_UPDATE "/test/update.txt"
#define CACHE_TEST_PATH_PERSIST "/test/persist.txt"

/*
 * =================================================================
 * Test Entry Values - Basic Upsert/Lookup Test
 * =================================================================
 */

#define CACHE_TEST_BASIC_MTIME 100000
#define CACHE_TEST_BASIC_CTIME 100001
#define CACHE_TEST_BASIC_SIZE 12345
#define CACHE_TEST_BASIC_HASH_LOW 0x1234567890ABCDEFULL
#define CACHE_TEST_BASIC_HASH_HIGH 0xFEDCBA0987654321ULL

/*
 * =================================================================
 * Test Entry Values - Multiple Entries Test
 * =================================================================
 */

#define CACHE_TEST_MULTI_BASE_MTIME 200000
#define CACHE_TEST_MULTI_BASE_CTIME 200001
#define CACHE_TEST_MULTI_BASE_SIZE 10000
#define CACHE_TEST_MULTI_SIZE_INCREMENT 1000
#define CACHE_TEST_MULTI_BASE_HASH_LOW 0x1000
#define CACHE_TEST_MULTI_BASE_HASH_HIGH 0x2000
#define CACHE_TEST_MULTI_ENTRY_COUNT 3

/*
 * =================================================================
 * Test Entry Values - Update Test (First Version)
 * =================================================================
 */

#define CACHE_TEST_UPDATE_V1_MTIME 300000
#define CACHE_TEST_UPDATE_V1_CTIME 300001
#define CACHE_TEST_UPDATE_V1_SIZE 5000
#define CACHE_TEST_UPDATE_V1_HASH_LOW 0xAAAA
#define CACHE_TEST_UPDATE_V1_HASH_HIGH 0xBBBB

/*
 * =================================================================
 * Test Entry Values - Update Test (Second Version)
 * =================================================================
 */

#define CACHE_TEST_UPDATE_V2_MTIME 400000
#define CACHE_TEST_UPDATE_V2_CTIME 400001
#define CACHE_TEST_UPDATE_V2_SIZE 6000
#define CACHE_TEST_UPDATE_V2_HASH_LOW 0xCCCC
#define CACHE_TEST_UPDATE_V2_HASH_HIGH 0xDDDD

/*
 * =================================================================
 * Test Entry Values - Persistence Test
 * =================================================================
 */

#define CACHE_TEST_PERSIST_MTIME 500000
#define CACHE_TEST_PERSIST_CTIME 500001
#define CACHE_TEST_PERSIST_SIZE 7777
#define CACHE_TEST_PERSIST_HASH_LOW 0xDEADBEEF
#define CACHE_TEST_PERSIST_HASH_HIGH 0xCAFEBABE

/*
 * =================================================================
 * Mark Concurrency Values
 * =================================================================
 */

#define CACHE_TEST_NB_THREADS 4
#define CACHE_TEST_MARK_PER_THREAD 25
#define CACHE_TEST_MARK_VISITED_MULT 10

/*
 * =================================================================
 * Test Entry Values - Sweep Test Entry A
 * =================================================================
 */

#define CACHE_TEST_SWEEP_A_MTIME 1000
#define CACHE_TEST_SWEEP_A_CTIME 1001
#define CACHE_TEST_SWEEP_A_SIZE 100
#define CACHE_TEST_SWEEP_A_HASH_LOW 0xAAAAAAAAAAAAAAAAULL
#define CACHE_TEST_SWEEP_A_HASH_HIGH 0xBBBBBBBBBBBBBBBBULL

/*
 * =================================================================
 * Test Entry Values - Sweep Test Entry B
 * =================================================================
 */

#define CACHE_TEST_SWEEP_B_MTIME 2000
#define CACHE_TEST_SWEEP_B_CTIME 2001
#define CACHE_TEST_SWEEP_B_SIZE 200
#define CACHE_TEST_SWEEP_B_HASH_LOW 0xCCCCCCCCCCCCCCCCULL
#define CACHE_TEST_SWEEP_B_HASH_HIGH 0xDDDDDDDDDDDDDDDDULL

/*
 * =================================================================
 * Test Entry Values - Sweep Test Entry C
 * =================================================================
 */

#define CACHE_TEST_SWEEP_C_MTIME 3000
#define CACHE_TEST_SWEEP_C_CTIME 3001
#define CACHE_TEST_SWEEP_C_SIZE 300
#define CACHE_TEST_SWEEP_C_HASH_LOW 0xEEEEEEEEEEEEEEEEULL
#define CACHE_TEST_SWEEP_C_HASH_HIGH 0xFFFFFFFFFFFFFFFFULL

/*
 * =================================================================
 * Test Entry Values - Sweep Test Entry D
 * =================================================================
 */

#define CACHE_TEST_SWEEP_D_MTIME 4000
#define CACHE_TEST_SWEEP_D_CTIME 4001
#define CACHE_TEST_SWEEP_D_SIZE 400
#define CACHE_TEST_SWEEP_D_HASH_LOW 0x1111111111111111ULL
#define CACHE_TEST_SWEEP_D_HASH_HIGH 0x2222222222222222ULL

#endif /* CHECK_CACHE_CONSTANTS_H */
