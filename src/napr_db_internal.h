/**
 * @file napr_db_internal.h
 * @brief Internal data structures and definitions for napr_db
 *
 * This file defines the on-disk format and internal structures.
 * The on-disk format is 64-bit little-endian with 4096-byte pages.
 *
 * CRITICAL: Structure layouts must be exact for zero-copy access.
 * All multi-byte fields are little-endian.
 */

#ifndef NAPR_DB_INTERNAL_H
#define NAPR_DB_INTERNAL_H

#include "../include/napr_db.h"
#include <stdint.h>
#include <stddef.h>

/*
 * Basic types
 */

/** Page number type (64-bit) */
typedef uint64_t pgno_t;

/** Transaction ID type (64-bit) */
typedef uint64_t txnid_t;

/*
 * Constants
 */

/** Database page size in bytes */
#define PAGE_SIZE       4096

/** Database file magic number */
#define DB_MAGIC        0xDECAFBAD

/** Database format version */
#define DB_VERSION      1

/*
 * Page type flags
 */

/** Branch (interior) page - contains keys and child page pointers */
#define P_BRANCH        0x01

/** Leaf page - contains keys and data values */
#define P_LEAF          0x02

/** Overflow page - continuation of large value */
#define P_OVERFLOW      0x04

/** Free page - available for reuse */
#define P_FREE          0x08

/*
 * On-disk structures
 *
 * IMPORTANT: All structures are packed and sized for direct memory mapping.
 * Fields are little-endian. Padding is explicit.
 */

/**
 * @brief Page header - appears at the start of every page.
 *
 * Uses slotted page design:
 * - Slot array grows down from 'lower'
 * - Node data grows up from 'upper'
 * - Free space is between lower and upper
 *
 * Layout:
 * +------------------+
 * | DB_PageHeader    |
 * +------------------+
 * | Slot array       | <- grows down
 * | ...              |
 * +------------------+ <- lower
 * | Free space       |
 * +------------------+ <- upper
 * | Node data        | <- grows up
 * | ...              |
 * +------------------+
 */
typedef struct __attribute__((packed)) DB_PageHeader {
    pgno_t pgno;        /**< Page number (8 bytes) */
    uint16_t flags;     /**< Page type flags (2 bytes) */
    uint16_t num_keys;  /**< Number of keys/entries in page (2 bytes) */
    uint16_t lower;     /**< Offset to end of slot array (2 bytes) */
    uint16_t upper;     /**< Offset to start of node data (2 bytes) */
    uint16_t padding;   /**< Padding for alignment (2 bytes) */
} DB_PageHeader;

/**
 * @brief Meta page structure for pages 0 and 1.
 *
 * Two meta pages provide atomic commit: the page with the highest
 * valid txnid is the current state. Transactions alternate between
 * updating page 0 and page 1.
 *
 * Total size must be exactly PAGE_SIZE (4096 bytes).
 */
typedef struct DB_MetaPage
{
    uint32_t magic;     /**< Magic number: DB_MAGIC (4 bytes) */
    uint32_t version;   /**< Format version: DB_VERSION (4 bytes) */
    txnid_t txnid;      /**< Transaction ID (8 bytes) */
    pgno_t root;        /**< Root page of B+ tree (8 bytes) */
    pgno_t last_pgno;   /**< Last allocated page number (8 bytes) */
    uint8_t reserved[4064];  /**< Reserved/padding to PAGE_SIZE */
} DB_MetaPage;

/**
 * @brief Branch (interior) node entry.
 *
 * Branch nodes contain keys and child page pointers.
 * In the B+ tree, branch nodes guide searches to child pages.
 *
 * On-disk layout (variable size):
 * - pgno: child page pointer (8 bytes)
 * - key_size: key length (2 bytes)
 * - key_data: actual key bytes (variable)
 *
 * Note: The key_data is a flexible array member. In practice, entries
 * are stored in the slotted page's data area.
 */
typedef struct __attribute__((packed)) DB_BranchNode {
    pgno_t pgno;        /**< Child page pointer (8 bytes) */
    uint16_t key_size;  /**< Key size in bytes (2 bytes) */
    uint8_t key_data[];  /**< Key data (variable length) */
} DB_BranchNode;

/**
 * @brief Leaf node entry.
 *
 * Leaf nodes contain the actual key-value pairs.
 *
 * On-disk layout (variable size):
 * - key_size: key length (2 bytes)
 * - data_size: value length (2 bytes)
 * - kv_data: key bytes followed by value bytes (variable)
 *
 * The kv_data contains: [key bytes][value bytes]
 *
 * Note: kv_data is a flexible array member. In practice, entries
 * are stored in the slotted page's data area.
 */
typedef struct __attribute__((packed)) DB_LeafNode {
    uint16_t key_size;   /**< Key size in bytes (2 bytes) */
    uint16_t data_size;  /**< Data size in bytes (2 bytes) */
    uint8_t kv_data[];   /**< Key and data (variable length) */
} DB_LeafNode;

#endif /* NAPR_DB_INTERNAL_H */
