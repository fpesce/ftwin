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
    else if (key1_size > key2_size) {
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
    uint16_t left;
    uint16_t right;
    int cmp;

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
        else if (cmp < 0) {
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
