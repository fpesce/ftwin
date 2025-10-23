/**
 * @file napr_db_tree.c
 * @brief B+ Tree operations for napr_db
 *
 * Implements tree traversal, search, insertion, and deletion operations.
 * Uses the slotted page design for efficient variable-length key/value storage.
 */

#include "napr_db_internal.h"
#include <string.h>
#include <apr_hash.h>

/**
 * @brief Compare two keys.
 *
 * Performs lexicographic byte comparison of two keys.
 *
 * @param key1_data Pointer to first key data
 * @param key1_size Size of first key
 * @param key2_data Pointer to second key data
 * @param key2_size Size of second key
 * @return <0 if key1 < key2, 0 if equal, >0 if key1 > key2
 */
static int db_key_compare(const uint8_t *key1_data, uint16_t key1_size, const uint8_t *key2_data, uint16_t key2_size)
{
    /* Compare the common length */
    size_t min_size = (key1_size < key2_size) ? key1_size : key2_size;
    int result = memcmp(key1_data, key2_data, min_size);

    if (result != 0) {
        return result;
    }

    /* If common parts are equal, shorter key is smaller */
    if (key1_size < key2_size) {
        return -1;
    }
    if (key1_size > key2_size) {
        return 1;
    }

    return 0;
}

/**
 * @brief Search for a key within a page using binary search.
 *
 * Performs binary search on a Branch or Leaf page to find a key.
 * The search works on the sorted slot array, comparing against the
 * keys stored in the node data area.
 *
 * For exact matches:
 * - Returns APR_SUCCESS and sets *index_out to the match position
 *
 * For non-matches:
 * - Returns APR_NOTFOUND and sets *index_out to the insertion point
 * - The insertion point is where the key would be inserted to maintain order
 * - For Branch pages: index points to the child that should contain the key
 * - For Leaf pages: index points to where the key should be inserted
 *
 * @param page Pointer to the page header (Branch or Leaf)
 * @param key Key to search for
 * @param index_out Output: index of match or insertion point
 * @return APR_SUCCESS if exact match found, APR_NOTFOUND otherwise
 */
apr_status_t db_page_search(DB_PageHeader * page, const napr_db_val_t *key, uint16_t *index_out)
{
    uint16_t left = 0;
    uint16_t right = 0;
    int cmp = 0;

    if (!page || !key || !index_out) {
        return APR_EINVAL;
    }

    /* Handle empty page */
    if (page->num_keys == 0) {
        *index_out = 0;
        return APR_NOTFOUND;
    }

    /* Binary search */
    left = 0;
    right = page->num_keys;
    while (left < right) {
        uint16_t mid = left + (right - left) / 2;

        /* Get the key at mid index based on page type */
        if (page->flags & P_BRANCH) {
            /* Branch page */
            DB_BranchNode *node = db_page_branch_node(page, mid);
            uint8_t *node_key = db_branch_node_key(node);
            cmp = db_key_compare(key->data, (uint16_t) key->size, node_key, node->key_size);
        }
        else if (page->flags & P_LEAF) {
            /* Leaf page */
            DB_LeafNode *node = db_page_leaf_node(page, mid);
            uint8_t *node_key = db_leaf_node_key(node);
            cmp = db_key_compare(key->data, (uint16_t) key->size, node_key, node->key_size);
        }
        else {
            /* Invalid page type */
            return APR_EINVAL;
        }

        if (cmp == 0) {
            /* Exact match found */
            *index_out = mid;
            return APR_SUCCESS;
        }
        if (cmp < 0) {
            /* Search key is smaller, search left half */
            right = mid;
        }
        else {
            /* Search key is larger, search right half */
            left = mid + 1;
        }
    }

    /*
     * No exact match found.
     * 'left' now points to the insertion point.
     *
     * For Branch pages: This is the index of the child to follow.
     * For Leaf pages: This is where the key would be inserted.
     */
    *index_out = left;
    return APR_NOTFOUND;
}

/**
 * @brief Get a page, checking dirty pages first for write transactions.
 *
 * For write transactions, checks if the page has been modified (exists in dirty_pages hash).
 * If so, returns the dirty copy. Otherwise returns the page from the memory map.
 *
 * @param txn Transaction handle
 * @param pgno Page number to retrieve
 * @return Pointer to the page (either dirty copy or mmap)
 */
static inline DB_PageHeader *db_get_page(napr_db_txn_t *txn, pgno_t pgno)
{
    DB_PageHeader *page = NULL;

    /* For write transactions, check dirty pages first */
    if (!(txn->flags & NAPR_DB_RDONLY)) {
        page = apr_hash_get(txn->dirty_pages, &pgno, sizeof(pgno_t));
        if (page) {
            return page;
        }
    }

    /* Return page from memory map */
    return (DB_PageHeader *) ((char *) txn->env->map_addr + (pgno * PAGE_SIZE));
}

/**
 * @brief Find the leaf page that should contain a given key.
 *
 * Traverses the B+ tree from the root down to the appropriate leaf page.
 * This function performs tree traversal following child pointers in branch
 * pages until reaching a leaf.
 *
 * @param txn Transaction handle (contains root_pgno snapshot)
 * @param key Key to search for
 * @param leaf_page_out Output: pointer to the leaf page in the memory map
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_find_leaf_page(napr_db_txn_t *txn, const napr_db_val_t *key, DB_PageHeader ** leaf_page_out)
{
    pgno_t current_pgno = 0;
    DB_PageHeader *page = NULL;
    uint16_t index = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    if (!txn || !key || !leaf_page_out) {
        return APR_EINVAL;
    }

    /* Start at the root page from the transaction's snapshot */
    current_pgno = txn->root_pgno;

    /* Traverse the tree */
    while (1) {
        /* Get page (checks dirty pages first for write txns) */
        page = db_get_page(txn, current_pgno);

        /* Check if this is a leaf page */
        if (page->flags & P_LEAF) {
            /* Found the leaf - stop traversal */
            *leaf_page_out = page;
            return APR_SUCCESS;
        }

        /* Must be a branch page */
        if (!(page->flags & P_BRANCH)) {
            /* Invalid page type */
            return APR_EINVAL;
        }

        /* Search within the branch page to find the correct child */
        status = db_page_search(page, key, &index);

        /*
         * For branch pages in a B+ tree:
         * - Each entry (key[i], child[i]) means: child[i] contains keys >= key[i]
         * - If the search finds an exact match at index i, follow child[i]
         * - If not found, index is the insertion point:
         *   - If index == 0: key < all keys, but there's no "left" child in our design,
         *     so we follow child[0] (which should contain the smallest keys)
         *   - If 0 < index < num_keys: key is between key[index-1] and key[index],
         *     follow child[index-1]
         *   - If index == num_keys: key > all keys, follow child[num_keys-1]
         *
         * Simplification: If search returns APR_NOTFOUND and index > 0, decrement index
         */

        if (status == APR_NOTFOUND && index > 0) {
            index--;
        }

        /* Ensure index is in valid range */
        if (index >= page->num_keys) {
            index = page->num_keys - 1;
        }

        /* Get the child page number from the branch node */
        DB_BranchNode *branch_node = db_page_branch_node(page, index);
        current_pgno = branch_node->pgno;
    }

    /* Should never reach here */
    return APR_EGENERAL;
}

/**
 * @brief Allocate new pages in a write transaction.
 *
 * Allocates one or more contiguous pages by incrementing the transaction's
 * last_pgno counter. This is a simple allocation strategy - the allocated
 * pages will be written to disk on commit.
 *
 * Note: Free page management and reuse will be implemented in later iterations.
 *
 * @param txn Write transaction handle
 * @param count Number of contiguous pages to allocate
 * @param pgno_out Output: page number of first allocated page
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_page_alloc(napr_db_txn_t *txn, uint32_t count, pgno_t *pgno_out)
{
    pgno_t first_pgno = 0;

    if (!txn || !pgno_out || count == 0) {
        return APR_EINVAL;
    }

    /* Verify this is a write transaction */
    if (txn->flags & NAPR_DB_RDONLY) {
        return APR_EINVAL;
    }

    /* Allocate by incrementing last_pgno */
    first_pgno = txn->new_last_pgno + 1;
    txn->new_last_pgno += count;

    *pgno_out = first_pgno;
    return APR_SUCCESS;
}

/**
 * @brief Get a writable copy of a page (Copy-on-Write).
 *
 * This implements the core CoW mechanism for MVCC:
 * - On first modification: allocate a new buffer, copy the original page
 * - On subsequent modifications: return the existing dirty copy
 *
 * The dirty copy is stored in the transaction's dirty_pages hash table,
 * keyed by the original page number. On commit, dirty pages are written
 * to their original locations in the file.
 *
 * @param txn Write transaction handle
 * @param original_page Pointer to the original page in the memory map
 * @param dirty_copy_out Output: pointer to the writable dirty copy
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_page_get_writable(napr_db_txn_t *txn, DB_PageHeader * original_page, DB_PageHeader ** dirty_copy_out)
{
    DB_PageHeader *dirty_copy = NULL;
    pgno_t pgno = 0;
    pgno_t *pgno_key = NULL;

    if (!txn || !original_page || !dirty_copy_out) {
        return APR_EINVAL;
    }

    /* Verify this is a write transaction */
    if (txn->flags & NAPR_DB_RDONLY) {
        return APR_EINVAL;
    }

    /* Get the page number from the original page */
    pgno = original_page->pgno;

    /* Check if we already have a dirty copy of this page */
    dirty_copy = apr_hash_get(txn->dirty_pages, &pgno, sizeof(pgno_t));

    if (dirty_copy) {
        /* Already have a dirty copy - return it */
        *dirty_copy_out = dirty_copy;
        return APR_SUCCESS;
    }

    /* Need to create a new dirty copy */
    dirty_copy = apr_palloc(txn->pool, PAGE_SIZE);
    if (!dirty_copy) {
        return APR_ENOMEM;
    }

    /* Copy the original page to the dirty buffer */
    memcpy(dirty_copy, original_page, PAGE_SIZE);

    /* Create a persistent key for the hash table */
    pgno_key = apr_palloc(txn->pool, sizeof(pgno_t));
    if (!pgno_key) {
        return APR_ENOMEM;
    }
    *pgno_key = pgno;

    /* Store the dirty copy in the hash table */
    apr_hash_set(txn->dirty_pages, pgno_key, sizeof(pgno_t), dirty_copy);

    *dirty_copy_out = dirty_copy;
    return APR_SUCCESS;
}

/**
 * @brief Insert a key/value pair into a page at the specified index.
 *
 * This function assumes:
 * - The page is a dirty copy (writable)
 * - The page has enough free space for the insertion
 * - The page will not split (simple case only)
 *
 * The function uses the slotted page design:
 * - Shifts slot array entries to make room at index
 * - Packs key/value data at the top of the page (growing downward)
 * - Updates page header (num_keys, upper)
 *
 * @param page Pointer to the dirty page
 * @param index Index where to insert the new entry
 * @param key Key to insert
 * @param data Value to insert (NULL for branch nodes with child_pgno)
 * @param child_pgno Child page number (for branch nodes, 0 for leaf nodes)
 * @return APR_SUCCESS on success, APR_ENOSPC if not enough space, error code otherwise
 */
apr_status_t db_page_insert(DB_PageHeader * page, uint16_t index, const napr_db_val_t *key, const napr_db_val_t *data, pgno_t child_pgno)
{
    uint16_t *slots = NULL;
    uint16_t node_size = 0;
    uint16_t free_space = 0;
    uint16_t new_offset = 0;
    uint16_t i = 0;

    if (!page || !key) {
        return APR_EINVAL;
    }

    /* Calculate required space */
    if (page->flags & P_LEAF) {
        /* Leaf node: DB_LeafNode + key + data */
        if (!data) {
            return APR_EINVAL;
        }
        node_size = (uint16_t) (sizeof(DB_LeafNode) + key->size + data->size);
    }
    else if (page->flags & P_BRANCH) {
        /* Branch node: DB_BranchNode + key */
        node_size = (uint16_t) (sizeof(DB_BranchNode) + key->size);
    }
    else {
        return APR_EINVAL;
    }

    /* Check if there's enough free space */
    free_space = page->upper - page->lower;
    if (free_space < (node_size + sizeof(uint16_t))) {
        return APR_ENOSPC;
    }

    /* Get slot array */
    slots = db_page_slots(page);

    /* Shift existing slots to make room at index */
    for (i = page->num_keys; i > index; i--) {
        slots[i] = slots[i - 1];
    }

    /* Allocate space for new node (grows downward from upper) */
    new_offset = page->upper - node_size;

    /* Create the new node */
    if (page->flags & P_LEAF) {
        DB_LeafNode *node = (DB_LeafNode *) ((char *) page + new_offset);
        node->key_size = (uint16_t) key->size;
        node->data_size = (uint16_t) data->size;
        memcpy(node->kv_data, key->data, key->size);
        memcpy(node->kv_data + key->size, data->data, data->size);
    }
    else {                      /* P_BRANCH */
        DB_BranchNode *node = (DB_BranchNode *) ((char *) page + new_offset);
        node->pgno = child_pgno;
        node->key_size = (uint16_t) key->size;
        memcpy(node->key_data, key->data, key->size);
    }

    /* Update slot array */
    slots[index] = new_offset;

    /* Update page header */
    page->num_keys++;
    page->lower += sizeof(uint16_t);
    page->upper = new_offset;

    return APR_SUCCESS;
}

/**
 * @brief Find the leaf page for a key and record the path (for CoW).
 *
 * This is similar to db_find_leaf_page, but it records the path from
 * root to leaf for later CoW propagation. This is necessary for write
 * operations to ensure the entire path is copied.
 *
 * @param txn Transaction handle
 * @param key Key to search for
 * @param path_out Array to store page numbers along the path
 * @param path_len_out Output: length of the path
 * @param leaf_page_out Output: pointer to the leaf page
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_find_leaf_page_with_path(napr_db_txn_t *txn, const napr_db_val_t *key, pgno_t *path_out, uint16_t *path_len_out, DB_PageHeader ** leaf_page_out)
{
    pgno_t current_pgno = 0;
    DB_PageHeader *page = NULL;
    uint16_t index = 0;
    uint16_t depth = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    apr_status_t status;

    if (!txn || !key || !path_out || !path_len_out || !leaf_page_out) {
        return APR_EINVAL;
    }

    /* Start at the root page from the transaction's snapshot */
    current_pgno = txn->root_pgno;

    /* Traverse the tree and record the path */
    while (1) {
        /* Record this page in the path */
        if (depth >= MAX_TREE_DEPTH) {
            return APR_EGENERAL;        /* Tree too deep */
        }
        path_out[depth] = current_pgno;
        depth++;

        /* Get page (checks dirty pages first for write txns) */
        page = db_get_page(txn, current_pgno);

        /* Check if this is a leaf page */
        if (page->flags & P_LEAF) {
            /* Found the leaf - stop traversal */
            *path_len_out = depth;
            *leaf_page_out = page;
            return APR_SUCCESS;
        }

        /* Must be a branch page */
        if (!(page->flags & P_BRANCH)) {
            return APR_EINVAL;
        }

        /* Search within the branch page to find the correct child */
        status = db_page_search(page, key, &index);

        /* For branch pages: handle insertion point logic */
        if (status == APR_NOTFOUND && index > 0) {
            index--;
        }

        /* Ensure index is in valid range */
        if (index >= page->num_keys) {
            index = page->num_keys - 1;
        }

        /* Get the child page number from the branch node */
        DB_BranchNode *branch_node = db_page_branch_node(page, index);
        current_pgno = branch_node->pgno;
    }

    return APR_EGENERAL;
}
