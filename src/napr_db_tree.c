/**
 * @file napr_db_tree.c
 * @brief B+ Tree operations for napr_db
 *
 * Implements tree traversal, search, insertion, and deletion operations.
 * Uses the slotted page design for efficient variable-length key/value storage.
 */

#include "napr_db_internal.h"
#include <string.h>

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
apr_status_t db_page_search(DB_PageHeader * page, const napr_db_val_t * key, uint16_t *index_out)
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
apr_status_t db_find_leaf_page(napr_db_txn_t * txn, const napr_db_val_t * key, DB_PageHeader ** leaf_page_out)
{
    pgno_t current_pgno;
    DB_PageHeader *page;
    uint16_t index;
    apr_status_t status;

    if (!txn || !key || !leaf_page_out) {
        return APR_EINVAL;
    }

    /* Start at the root page from the transaction's snapshot */
    current_pgno = txn->root_pgno;

    /* Traverse the tree */
    while (1) {
        /* Calculate page address in memory map */
        page = (DB_PageHeader *) ((char *) txn->env->map_addr + (current_pgno * PAGE_SIZE));

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
