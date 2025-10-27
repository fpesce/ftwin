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

/* Maximum depth of B+ tree for path tracking */
enum
{ MAX_TREE_DEPTH = 32 };
#include <apr_mmap.h>
#include <apr_thread_mutex.h>
#include <apr_proc_mutex.h>
#include <apr_hash.h>
#include <apr_portable.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Basic types
 */

/** Page number type (64-bit) */
typedef uint64_t pgno_t;

/** Transaction ID type (64-bit) */
typedef uint64_t txnid_t;

/** @brief Structure for passing page numbers to commit_meta_page */
typedef struct DB_CommitPgnos
{
    pgno_t new_root_pgno;       /**< New root page number for main DB */
    pgno_t new_free_db_root_pgno;   /**< New root page number for Free DB */
} DB_CommitPgnos;

/** @brief Structure for passing page numbers to propagate_split_up_tree_in_tree */
typedef struct DB_SplitPgnos
{
    pgno_t *right_child_pgno;   /**< Right child page number */
    pgno_t *new_root_out;       /**< New root page number */
} DB_SplitPgnos;

/*
 * MVCC Reader Tracking
 */

/** Maximum concurrent read transactions */
#define MAX_READERS 126

/** CPU cache line size for alignment (prevents false sharing) */
#define CACHE_LINE_SIZE 64

#define PADDING_SIZE 44
/**
 * @brief Reader slot for MVCC tracking.
 *
 * Each active read transaction registers in a slot with its snapshot TXNID.
 * CRITICAL (Spec 3.2): Structure is cache-line sized (64 bytes) to prevent
 * false sharing between CPU cores when multiple readers access different slots.
 *
 * A slot is considered free when txnid == 0.
 */
typedef struct DB_ReaderSlot
{
    apr_os_proc_t pid;      /**< Process ID (for inter-process tracking) */
    apr_os_thread_t tid;    /**< Thread ID (for intra-process tracking) */
    txnid_t txnid;          /**< Snapshot TXNID (0 = slot is free) */
    uint8_t padding[PADDING_SIZE];    /**< Padding to 64 bytes (4 + 8 + 8 + 44 = 64) */
} __attribute__((packed)) DB_ReaderSlot;

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
#define DB_METAPAGE_FREE_DB_ROOT_OFFSET 32
#define DB_METAPAGE_PAYLOAD_SIZE (4 + 4 + 8 + 8 + 8 + 8)
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

/* Freed Pages default array size  */
#define DB_FREED_PAGES_DFLT_SIZE 16

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
typedef struct __attribute__((packed))
     DB_PageHeader
     {
         pgno_t pgno;   /**< Page number (8 bytes) */
         uint16_t flags;/**< Page type flags (2 bytes) */
         uint16_t num_keys;
                        /**< Number of keys/entries in page (2 bytes) */
         uint16_t lower;/**< Offset to end of slot array (2 bytes) */
         uint16_t upper;/**< Offset to start of node data (2 bytes) */
         uint16_t padding;
                        /**< Padding for alignment (2 bytes) */
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
     typedef struct __attribute__((packed))
     DB_MetaPage
     {
         uint32_t magic;/**< Magic number: DB_MAGIC (4 bytes) */
         uint32_t version;
                        /**< Format version: DB_VERSION (4 bytes) */
         txnid_t txnid; /**< Transaction ID (8 bytes) */
         pgno_t root;   /**< Root page of main B+ tree (8 bytes) */
         pgno_t last_pgno;
                        /**< Last allocated page number (8 bytes) */
         pgno_t free_db_root;
                        /**< Root page of Free DB B+ tree (8 bytes) */
         uint8_t reserved[DB_METAPAGE_RESERVED_SIZE];
                                                  /**< Reserved/padding to PAGE_SIZE */
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
     typedef struct __attribute__((packed))
     DB_BranchNode
     {
         pgno_t pgno;   /**< Child page pointer (8 bytes) */
         uint16_t key_size;
                        /**< Key size in bytes (2 bytes) */
         uint8_t key_data[];
                         /**< Key data (variable length) */
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
     typedef struct __attribute__((packed))
     DB_LeafNode
     {
         uint16_t key_size;
                         /**< Key size in bytes (2 bytes) */
         uint16_t data_size;
                         /**< Data size in bytes (2 bytes) */
         uint8_t kv_data[];
                         /**< Key and data (variable length) */
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
         apr_pool_t *pool;      /**< APR pool for allocations */
         apr_size_t mapsize;    /**< Size of memory map in bytes */
         unsigned int flags;    /**< Environment flags */

         /* File and mapping */
         apr_file_t *file;      /**< Database file handle */
         apr_mmap_t *mmap;      /**< Memory map handle */
         void *map_addr;        /**< Base address of memory map */

         /* Meta pages */
         DB_MetaPage *meta0;    /**< Pointer to Meta Page 0 in mmap */
         DB_MetaPage *meta1;    /**< Pointer to Meta Page 1 in mmap */
         DB_MetaPage *live_meta;/**< Pointer to current active meta page */

         /* Synchronization - SWMR model */
         apr_thread_mutex_t *writer_thread_mutex;
                                              /**< Intra-process writer lock */
         apr_proc_mutex_t *writer_proc_mutex; /**< Inter-process writer lock */

         /* MVCC Reader Tracking */
         DB_ReaderSlot reader_table[MAX_READERS] __attribute__((aligned(CACHE_LINE_SIZE)));
                                                                                        /**< Active reader tracking table */
         apr_thread_mutex_t *reader_table_mutex;
                                              /**< Protects reader table access */
     };

/**
 * @brief Transaction handle (concrete definition).
 *
 * Represents a read or write transaction. Write transactions are
 * serialized via the SWMR mutex. Read transactions are lock-free
 * and operate on a snapshot.
 *
 * For write transactions, the dirty_pages hash table implements
 * Copy-on-Write (CoW) semantics: modified pages are copied to
 * private buffers before changes are made.
 */
     struct napr_db_txn_t
     {
         napr_db_env_t *env;    /**< Environment this transaction belongs to */
         apr_pool_t *pool;      /**< APR pool for transaction allocations */
         txnid_t txnid;         /**< Transaction ID (snapshot version) */
         pgno_t root_pgno;      /**< Root page number for main DB snapshot */
         pgno_t free_db_root_pgno;
                                /**< Root page number for Free DB snapshot */
         unsigned int flags;    /**< Transaction flags (RDONLY, etc.) */

         /* Copy-on-Write tracking for write transactions */
         apr_hash_t *dirty_pages;
                                /**< Hash table: pgno_t -> DB_PageHeader* (dirty copy) */
         pgno_t new_last_pgno;  /**< Last page number for this transaction (for allocation) */

         /* Free space tracking for write transactions */
         apr_array_header_t *freed_pages;
                                      /**< Array of pgno_t freed during this transaction */
     };

/**
 * @brief Cursor handle (concrete definition).
 *
 * A cursor maintains a path from the root to a leaf page, allowing
 * for traversal of the B+ tree. The stack stores the parent pages
 * and the index of the child followed at each level.
 */
     struct napr_db_cursor_t
     {
         napr_db_txn_t *txn;    /**< Transaction this cursor belongs to */
         apr_pool_t *pool;      /**< APR pool for cursor allocations */

         /* Traversal stack */
         struct
         {
             DB_PageHeader *page;
                                /**< Page at this level */
             uint16_t index;    /**< Index of child/key on page */
         } stack[MAX_TREE_DEPTH];

         uint16_t top;          /**< Index of the top of the stack */
         int eof;               /**< Flag: 1 if cursor is at End-of-File */
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
     static inline uint16_t *db_page_slots(DB_PageHeader *page)
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
static inline uint16_t db_page_slot_offset(DB_PageHeader *page, uint16_t index)
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
static inline DB_BranchNode *db_page_branch_node(DB_PageHeader *page, uint16_t index)
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
static inline DB_LeafNode *db_page_leaf_node(DB_PageHeader *page, uint16_t index)
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
static inline uint8_t *db_branch_node_key(DB_BranchNode *node)
{
    return node->key_data;
}

/**
 * @brief Get pointer to the key data in a leaf node.
 *
 * @param node Pointer to the leaf node
 * @return Pointer to the key data bytes
 */
static inline uint8_t *db_leaf_node_key(DB_LeafNode *node)
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
static inline uint8_t *db_leaf_node_value(DB_LeafNode *node)
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
apr_status_t db_page_search(DB_PageHeader *page, const napr_db_val_t *key, uint16_t *index_out);

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
apr_status_t db_find_leaf_page(napr_db_txn_t *txn, const napr_db_val_t *key, DB_PageHeader **leaf_page_out);

/**
 * @brief Find leaf page in an arbitrary tree (used for Free DB).
 *
 * This variant accepts a root page number parameter, allowing it to traverse
 * any B+ tree (main DB or Free DB).
 *
 * @param txn Transaction handle
 * @param root_pgno Root page number of the tree to search
 * @param key Key to search for
 * @param leaf_page_out Output: pointer to the leaf page
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_find_leaf_page_in_tree(napr_db_txn_t *txn, pgno_t root_pgno, const napr_db_val_t *key, DB_PageHeader **leaf_page_out);

/**
 * @brief Find leaf page with path in an arbitrary tree (used for Free DB).
 *
 * This variant accepts a root page number parameter, allowing it to traverse
 * any B+ tree (main DB or Free DB) while recording the path for CoW operations.
 *
 * @param txn Transaction handle
 * @param root_pgno Root page number of the tree to search
 * @param key Key to search for
 * @param path_out Array to store page numbers along the path
 * @param path_len_out Output: length of the path
 * @param leaf_page_out Output: pointer to the leaf page
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_find_leaf_page_with_path_in_tree(napr_db_txn_t *txn, pgno_t root_pgno, const napr_db_val_t *key, pgno_t *path_out, uint16_t *path_len_out, DB_PageHeader **leaf_page_out);

/**
 * @brief Allocate new pages in a write transaction.
 *
 * Allocates one or more contiguous pages by incrementing the last_pgno.
 * This is a simple allocation strategy - free page management will come later.
 *
 * @param txn Write transaction handle
 * @param count Number of contiguous pages to allocate
 * @param pgno_out Output: page number of first allocated page
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_page_alloc(napr_db_txn_t *txn, uint32_t count, pgno_t *pgno_out);

/**
 * @brief Get a writable copy of a page (Copy-on-Write).
 *
 * This is the core CoW mechanism. If the page has already been modified
 * in this transaction, return the existing dirty copy. Otherwise, create
 * a new dirty copy in transaction-private memory.
 *
 * @param txn Write transaction handle
 * @param original_page Pointer to the original page in the memory map
 * @param dirty_copy_out Output: pointer to the writable dirty copy
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_page_get_writable(napr_db_txn_t *txn, DB_PageHeader *original_page, DB_PageHeader **dirty_copy_out);

/**
 * @brief Find the leaf page for a key and record the path from root to leaf.
 *
 * @param txn Transaction handle
 * @param key Key to search for
 * @param path_out Array to store page numbers along the path (must be at least 32 elements)
 * @param path_len_out Output: length of the path
 * @param leaf_page_out Output: pointer to the leaf page
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_find_leaf_page_with_path(napr_db_txn_t *txn, const napr_db_val_t *key, pgno_t *path_out, uint16_t *path_len_out, DB_PageHeader **leaf_page_out);

/**
 * @brief Insert a key/value or key/child pair into a page.
 *
 * @param page Dirty page to insert into (must have enough free space)
 * @param index Index where to insert the new entry
 * @param key Key to insert
 * @param data Value to insert (NULL for branch nodes with child_pgno)
 * @param child_pgno Child page number (for branch nodes, 0 for leaf nodes)
 * @return APR_SUCCESS on success, APR_ENOSPC if not enough space, error code otherwise
 */
apr_status_t db_page_insert(DB_PageHeader *page, uint16_t index, const napr_db_val_t *key, const napr_db_val_t *data, pgno_t child_pgno);

/**
 * @brief Delete a node from a page by index.
 *
 * Removes the specified node from the page and compacts the data area
 * to reclaim space. Updates page header accordingly.
 *
 * @param page Page containing the node to delete (must be writable/dirty)
 * @param index Index of the node to delete (0-based)
 * @return APR_SUCCESS on success, APR_EINVAL if index out of bounds
 */
apr_status_t db_page_delete(DB_PageHeader *page, uint16_t index);

/**
 * @brief Split a leaf page when it becomes full.
 *
 * Splits a full leaf page into two pages, moving approximately half the
 * entries to a new right sibling page. The divider key (first key of the
 * right page) is returned for insertion into the parent.
 *
 * @param txn Write transaction handle
 * @param left_page The original full page (will be modified in-place)
 * @param right_page_out Output: pointer to the newly allocated right sibling page
 * @param divider_key_out Output: the divider key to insert into parent
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_split_leaf(napr_db_txn_t *txn, DB_PageHeader *left_page, DB_PageHeader **right_page_out, napr_db_val_t *divider_key_out);
apr_status_t db_split_branch(napr_db_txn_t *txn, DB_PageHeader *left_page, DB_PageHeader **right_page_out, napr_db_val_t *divider_key_out);

/**
 * @brief Get the oldest active reader TXNID from the reader tracking table.
 *
 * Scans the reader table to find the minimum TXNID among all active readers.
 * This is used to determine which pages can be safely reclaimed (pages freed
 * by transactions older than the oldest reader are no longer visible).
 *
 * @param env Database environment
 * @param oldest_txnid_out Output: oldest active reader TXNID (0 if no active readers)
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_get_oldest_reader_txnid(napr_db_env_t *env, txnid_t *oldest_txnid_out);

/**
 * @brief Try to reclaim a page from the Free DB based on MVCC safety rules.
 * @param txn The write transaction requesting allocation.
 * @param reclaimed_pgno_out Output parameter for the reclaimed page number.
 * @return APR_SUCCESS if reclaimed, APR_NOTFOUND if no safe pages available.
 */
apr_status_t db_reclaim_page_from_free_db(napr_db_txn_t *txn, pgno_t *reclaimed_pgno_out);

#endif /* NAPR_DB_INTERNAL_H */
