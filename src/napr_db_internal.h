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

#include "napr_db.h"
#include <apr_mmap.h>
#include <apr_thread_mutex.h>
#include <apr_proc_mutex.h>
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
 * Size and offset constants for validation
 */

/* Basic types */
#define PGNO_T_SIZE 8
#define TXNID_T_SIZE 8

/* DB_PageHeader layout */
#define DB_PAGEHEADER_SIZE 18
#define DB_PAGEHEADER_PGNO_OFFSET 0
#define DB_PAGEHEADER_FLAGS_OFFSET 8
#define DB_PAGEHEADER_NUM_KEYS_OFFSET 10
#define DB_PAGEHEADER_LOWER_OFFSET 12
#define DB_PAGEHEADER_UPPER_OFFSET 14
#define DB_PAGEHEADER_PADDING_OFFSET 16

/* DB_MetaPage layout */
#define DB_METAPAGE_SIZE PAGE_SIZE
#define DB_METAPAGE_MAGIC_OFFSET 0
#define DB_METAPAGE_VERSION_OFFSET 4
#define DB_METAPAGE_TXNID_OFFSET 8
#define DB_METAPAGE_ROOT_OFFSET 16
#define DB_METAPAGE_LAST_PGNO_OFFSET 24
#define DB_METAPAGE_PAYLOAD_SIZE (4 + 4 + 8 + 8 + 8)
#define DB_METAPAGE_RESERVED_SIZE (PAGE_SIZE - DB_METAPAGE_PAYLOAD_SIZE)

/* DB_BranchNode layout */
#define DB_BRANCHNODE_BASE_SIZE 10
#define DB_BRANCHNODE_PGNO_OFFSET 0
#define DB_BRANCHNODE_KEY_SIZE_OFFSET 8
#define DB_BRANCHNODE_KEY_DATA_OFFSET 10

/* DB_LeafNode layout */
#define DB_LEAFNODE_BASE_SIZE 4
#define DB_LEAFNODE_KEY_SIZE_OFFSET 0
#define DB_LEAFNODE_DATA_SIZE_OFFSET 2
#define DB_LEAFNODE_KV_DATA_OFFSET 4


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
    uint8_t reserved[DB_METAPAGE_RESERVED_SIZE];  /**< Reserved/padding to PAGE_SIZE */
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

/*
 * In-memory structures
 */

/**
 * @brief Database environment handle (concrete definition).
 *
 * Contains all state for an opened database file including
 * the memory map, file handle, and pointers to meta pages.
 */
struct napr_db_env_t
{
    apr_pool_t *pool;           /**< APR pool for allocations */
    apr_size_t mapsize;         /**< Size of memory map in bytes */
    unsigned int flags;         /**< Environment flags */

    /* File and mapping */
    apr_file_t *file;           /**< Database file handle */
    apr_mmap_t *mmap;           /**< Memory map handle */
    void *map_addr;             /**< Base address of memory map */

    /* Meta pages */
    DB_MetaPage *meta0;         /**< Pointer to Meta Page 0 in mmap */
    DB_MetaPage *meta1;         /**< Pointer to Meta Page 1 in mmap */
    DB_MetaPage *live_meta;     /**< Pointer to current active meta page */

    /* Synchronization - SWMR model */
    apr_thread_mutex_t *writer_thread_mutex;  /**< Intra-process writer lock */
    apr_proc_mutex_t *writer_proc_mutex;      /**< Inter-process writer lock */
};

/**
 * @brief Transaction handle (concrete definition).
 *
 * Represents a read or write transaction. Write transactions are
 * serialized via the SWMR mutex. Read transactions are lock-free
 * and operate on a snapshot.
 */
struct napr_db_txn_t
{
    napr_db_env_t *env;         /**< Environment this transaction belongs to */
    apr_pool_t *pool;           /**< APR pool for transaction allocations */
    txnid_t txnid;              /**< Transaction ID (snapshot version) */
    pgno_t root_pgno;           /**< Root page number for this snapshot */
    unsigned int flags;         /**< Transaction flags (RDONLY, etc.) */
};

/*
 * Page accessor helpers for navigating slotted pages
 *
 * The slotted page design stores:
 * - DB_PageHeader at offset 0
 * - Slot array (uint16_t offsets) starting at offset sizeof(DB_PageHeader)
 * - Node data area starting from page end, growing backwards
 *
 * Each slot contains the offset (from page start) to the node data.
 */

/**
 * @brief Get pointer to the slot array in a page.
 *
 * @param page Pointer to the page header
 * @return Pointer to the first slot (uint16_t offset)
 */
static inline uint16_t *db_page_slots(DB_PageHeader * page)
{
    return (uint16_t *) ((char *) page + sizeof(DB_PageHeader));
}

/**
 * @brief Get the offset of a node at a given index.
 *
 * @param page Pointer to the page header
 * @param index Index of the node (0-based)
 * @return Offset from page start to the node data
 */
static inline uint16_t db_page_slot_offset(DB_PageHeader * page, uint16_t index)
{
    uint16_t *slots = db_page_slots(page);
    return slots[index];
}

/**
 * @brief Get pointer to a branch node at a given index.
 *
 * @param page Pointer to the page header
 * @param index Index of the node (0-based)
 * @return Pointer to the DB_BranchNode
 */
static inline DB_BranchNode *db_page_branch_node(DB_PageHeader * page, uint16_t index)
{
    uint16_t offset = db_page_slot_offset(page, index);
    return (DB_BranchNode *) ((char *) page + offset);
}

/**
 * @brief Get pointer to a leaf node at a given index.
 *
 * @param page Pointer to the page header
 * @param index Index of the node (0-based)
 * @return Pointer to the DB_LeafNode
 */
static inline DB_LeafNode *db_page_leaf_node(DB_PageHeader * page, uint16_t index)
{
    uint16_t offset = db_page_slot_offset(page, index);
    return (DB_LeafNode *) ((char *) page + offset);
}

/**
 * @brief Get pointer to the key data in a branch node.
 *
 * @param node Pointer to the branch node
 * @return Pointer to the key data bytes
 */
static inline uint8_t *db_branch_node_key(DB_BranchNode * node)
{
    return node->key_data;
}

/**
 * @brief Get pointer to the key data in a leaf node.
 *
 * @param node Pointer to the leaf node
 * @return Pointer to the key data bytes
 */
static inline uint8_t *db_leaf_node_key(DB_LeafNode * node)
{
    return node->kv_data;
}

/**
 * @brief Get pointer to the value data in a leaf node.
 *
 * The value data follows immediately after the key data.
 *
 * @param node Pointer to the leaf node
 * @return Pointer to the value data bytes
 */
static inline uint8_t *db_leaf_node_value(DB_LeafNode * node)
{
    return node->kv_data + node->key_size;
}

/*
 * Forward declarations for tree operations
 */

/**
 * @brief Search for a key within a page using binary search.
 *
 * @param page Pointer to the page header (Branch or Leaf)
 * @param key Key to search for
 * @param index_out Output: index of match or insertion point
 * @return APR_SUCCESS if exact match found, APR_NOTFOUND otherwise
 */
apr_status_t db_page_search(DB_PageHeader * page, const napr_db_val_t * key, uint16_t *index_out);

/**
 * @brief Find the leaf page that should contain a given key.
 *
 * Traverses the B+ tree from the root to find the appropriate leaf page.
 *
 * @param txn Transaction handle
 * @param key Key to search for
 * @param leaf_page_out Output: pointer to the leaf page
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_find_leaf_page(napr_db_txn_t * txn, const napr_db_val_t * key, DB_PageHeader ** leaf_page_out);

#endif /* NAPR_DB_INTERNAL_H */
