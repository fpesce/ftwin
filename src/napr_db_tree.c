/**
 * @file napr_db_tree.c
 * @brief B+ Tree operations for napr_db
 *
 * Implements tree traversal, search, insertion, and deletion operations.
 * Uses the slotted page design for efficient variable-length key/value storage.
 */

#include "napr_db_internal.h"
#include <string.h>
#include <stdlib.h>
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
apr_status_t db_page_search(DB_PageHeader *page, const napr_db_val_t *key, uint16_t *index_out)
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
apr_status_t db_find_leaf_page(napr_db_txn_t *txn, const napr_db_val_t *key, DB_PageHeader **leaf_page_out)
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
         * For branch pages in a B+ tree with "minimum key" semantics:
         * - Each entry (key[i], child[i]) means: key[i] is the MINIMUM key in child[i]'s subtree
         * - Binary search returns the index where search_key would be inserted
         * - This is the FIRST entry with key >= search_key
         * - Special cases:
         *   - If index == 0: search_key < all keys, follow child[0]
         *   - If index == num_keys: search_key > all keys, follow child[num_keys-1]
         *   - Otherwise: follow child[index-1] (last child whose min_key <= search_key)
         *
         * Exception: If exact match, follow that child
         */

        if (status == APR_NOTFOUND && index > 0) {
            /* No exact match - follow the child with largest min_key <= search_key */
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
apr_status_t db_page_get_writable(napr_db_txn_t *txn, DB_PageHeader *original_page, DB_PageHeader **dirty_copy_out)
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
apr_status_t db_page_insert(DB_PageHeader *page, uint16_t index, const napr_db_val_t *key, const napr_db_val_t *data, pgno_t child_pgno)
{
    uint16_t *slots = NULL;
    uint16_t node_size = 0;
    uint16_t free_space = 0;
    uint16_t new_offset = 0;
    uint16_t idx = 0;

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
    for (idx = page->num_keys; idx > index; idx--) {
        slots[idx] = slots[idx - 1];
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
 * @brief Delete a node from a page by index.
 *
 * This function removes a node from a slotted page by:
 * 1. Removing the slot entry (shifts remaining slots)
 * 2. Compacting the data area if needed (shifts data to reclaim space)
 * 3. Updating page header (num_keys, upper pointer)
 *
 * @param page Page containing the node to delete (must be writable/dirty)
 * @param index Index of the node to delete
 * @return APR_SUCCESS on success, APR_EINVAL if index out of bounds
 */
apr_status_t db_page_delete(DB_PageHeader *page, uint16_t index)
{
    uint16_t *slots = NULL;
    uint16_t delete_offset = 0;
    uint16_t delete_size = 0;
    uint16_t idx = 0;

    if (!page || index >= page->num_keys) {
        return APR_EINVAL;
    }

    slots = db_page_slots(page);
    delete_offset = slots[index];

    /* Calculate size of node being deleted */
    if (page->flags & P_LEAF) {
        DB_LeafNode *node = (DB_LeafNode *) ((char *) page + delete_offset);
        delete_size = (uint16_t) (sizeof(DB_LeafNode) + node->key_size + node->data_size);
    }
    else if (page->flags & P_BRANCH) {
        DB_BranchNode *node = (DB_BranchNode *) ((char *) page + delete_offset);
        delete_size = (uint16_t) (sizeof(DB_BranchNode) + node->key_size);
    }
    else {
        return APR_EINVAL;
    }

    /* Remove slot by shifting remaining slots left */
    for (idx = index; idx < page->num_keys - 1; idx++) {
        slots[idx] = slots[idx + 1];
    }

    /* Compact data area: shift all data after deleted node upward */
    if (delete_offset > page->upper) {
        /* Data to move is from upper to delete_offset */
        uint16_t move_size = delete_offset - page->upper;
        if (move_size > 0) {
            memmove((char *) page + page->upper + delete_size, (char *) page + page->upper, move_size);
        }

        /* Update all slot offsets that pointed above the deleted node */
        for (idx = 0; idx < page->num_keys - 1; idx++) {
            if (slots[idx] < delete_offset) {
                slots[idx] += delete_size;
            }
        }
    }

    /* Update page header */
    page->num_keys--;
    page->lower -= sizeof(uint16_t);
    page->upper += delete_size;

    return APR_SUCCESS;
}

/**
 * @brief Split a leaf page when it becomes full.
 *
 * This function implements the B+ tree leaf split operation:
 * 1. Allocate a new page for the right sibling
 * 2. Find the split point (median)
 * 3. Move upper half of entries to the new page
 * 4. Update both page headers
 * 5. Return the divider key (first key of right page)
 *
 * @param txn Write transaction handle
 * @param left_page The original full page (modified in-place)
 * @param right_page_out Output: newly allocated right sibling page
 * @param divider_key_out Output: divider key for parent insertion
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_split_leaf(napr_db_txn_t *txn, DB_PageHeader *left_page, DB_PageHeader **right_page_out, napr_db_val_t *divider_key_out)
{
    apr_status_t status = APR_SUCCESS;
    DB_PageHeader *right_page = NULL;
    pgno_t right_pgno = 0;
    pgno_t *pgno_key = NULL;
    uint16_t split_point = 0;
    uint16_t idx = 0;
    uint16_t *right_slots = NULL;
    DB_LeafNode *first_right_node = NULL;

    if (!txn || !left_page || !right_page_out || !divider_key_out) {
        return APR_EINVAL;
    }

    /* Verify this is a leaf page */
    if (!(left_page->flags & P_LEAF)) {
        return APR_EINVAL;
    }

    /* Verify this is a write transaction */
    if (txn->flags & NAPR_DB_RDONLY) {
        return APR_EINVAL;
    }

    /* Allocate a new page for the right sibling */
    status = db_page_alloc(txn, 1, &right_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Allocate memory for the right page */
    right_page = apr_pcalloc(txn->pool, PAGE_SIZE);
    if (!right_page) {
        return APR_ENOMEM;
    }

    /* Initialize the right page header */
    right_page->pgno = right_pgno;
    right_page->flags = P_LEAF;
    right_page->num_keys = 0;
    right_page->lower = sizeof(DB_PageHeader);
    right_page->upper = PAGE_SIZE;
    right_page->padding = 0;

    /* Determine split point (median) */
    split_point = left_page->num_keys / 2;

    /* Get right page slot array */
    right_slots = db_page_slots(right_page);

    /* Move entries from split_point onwards to the right page */
    for (idx = split_point; idx < left_page->num_keys; idx++) {
        DB_LeafNode *src_node = db_page_leaf_node(left_page, idx);
        uint16_t node_size = sizeof(DB_LeafNode) + src_node->key_size + src_node->data_size;
        uint16_t new_offset = 0;
        DB_LeafNode *dest_node = NULL;
        uint16_t right_idx = idx - split_point;

        /* Allocate space in right page (grows downward from upper) */
        new_offset = right_page->upper - node_size;
        dest_node = (DB_LeafNode *) ((char *) right_page + new_offset);

        /* Copy the node */
        memcpy(dest_node, src_node, node_size);

        /* Update right page slot array */
        right_slots[right_idx] = new_offset;

        /* Update right page header */
        right_page->num_keys++;
        right_page->lower += sizeof(uint16_t);
        right_page->upper = new_offset;
    }

    /* Update left page header to reflect the reduced number of keys */
    left_page->num_keys = split_point;
    left_page->lower = sizeof(DB_PageHeader) + (split_point * sizeof(uint16_t));

    /* CRITICAL BUG FIX: Recalculate upper pointer to reflect only remaining entries
     * After moving entries [split_point..end] to right page, their data is still
     * in left page's data area. We need to find the minimum offset among entries
     * [0..split_point-1] to reclaim that space.
     */
    {
        uint16_t *left_slots = db_page_slots(left_page);
        uint16_t min_offset = PAGE_SIZE;
        for (uint16_t i = 0; i < split_point; i++) {
            uint16_t offset = left_slots[i];
            if (offset < min_offset) {
                min_offset = offset;
            }
        }
        left_page->upper = min_offset;
    }

    /* Store the right page in dirty pages hash */
    pgno_key = apr_palloc(txn->pool, sizeof(pgno_t));
    if (!pgno_key) {
        return APR_ENOMEM;
    }
    *pgno_key = right_pgno;
    apr_hash_set(txn->dirty_pages, pgno_key, sizeof(pgno_t), right_page);

    /* Set divider key (first key of right page) */
    first_right_node = db_page_leaf_node(right_page, 0);
    divider_key_out->data = db_leaf_node_key(first_right_node);
    divider_key_out->size = first_right_node->key_size;

    /* Return the right page */
    *right_page_out = right_page;

    return APR_SUCCESS;
}

/**
 * @brief Split a branch page when it becomes full.
 *
 * This function implements the B+ tree branch split operation:
 * 1. Allocate a new page for the right sibling
 * 2. Find the split point (median)
 * 3. Move upper half of entries to the new page
 * 4. CRITICAL: The divider key is "pushed up" to the parent, NOT copied to right page
 * 5. Update both page headers
 *
 * @param txn Write transaction handle
 * @param left_page The original full page (modified in-place)
 * @param right_page_out Output: newly allocated right sibling page
 * @param divider_key_out Output: divider key for parent insertion (pushed up)
 * @return APR_SUCCESS on success, error code on failure
 */
apr_status_t db_split_branch(napr_db_txn_t *txn, DB_PageHeader *left_page, DB_PageHeader **right_page_out, napr_db_val_t *divider_key_out)
{
    apr_status_t status = APR_SUCCESS;
    DB_PageHeader *right_page = NULL;
    pgno_t right_pgno = 0;
    pgno_t *pgno_key = NULL;
    uint16_t split_point = 0;
    uint16_t idx = 0;
    uint16_t *right_slots = NULL;
    DB_BranchNode *divider_node = NULL;

    if (!txn || !left_page || !right_page_out || !divider_key_out) {
        return APR_EINVAL;
    }

    /* Verify this is a branch page */
    if (!(left_page->flags & P_BRANCH)) {
        return APR_EINVAL;
    }

    /* Verify this is a write transaction */
    if (txn->flags & NAPR_DB_RDONLY) {
        return APR_EINVAL;
    }

    /* Allocate a new page for the right sibling */
    status = db_page_alloc(txn, 1, &right_pgno);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Allocate memory for the right page */
    right_page = apr_pcalloc(txn->pool, PAGE_SIZE);
    if (!right_page) {
        return APR_ENOMEM;
    }

    /* Initialize the right page header */
    right_page->pgno = right_pgno;
    right_page->flags = P_BRANCH;
    right_page->num_keys = 0;
    right_page->lower = sizeof(DB_PageHeader);
    right_page->upper = PAGE_SIZE;
    right_page->padding = 0;

    /* Determine split point (median) */
    split_point = left_page->num_keys / 2;

    /* Get right page slot array */
    right_slots = db_page_slots(right_page);

    /* Move entries from split_point onwards to the right page
     * CRITICAL: In a B+ tree with "minimum key" semantics, we must preserve
     * all child pointers. The entry at split_point is included in the RIGHT page.
     * Its key will also be used as the divider pushed to the parent.
     */
    for (idx = split_point; idx < left_page->num_keys; idx++) {
        DB_BranchNode *src_node = db_page_branch_node(left_page, idx);
        uint16_t node_size = sizeof(DB_BranchNode) + src_node->key_size;
        uint16_t new_offset = 0;
        DB_BranchNode *dest_node = NULL;
        uint16_t right_idx = idx - split_point;

        /* Allocate space in right page (grows downward from upper) */
        new_offset = right_page->upper - node_size;
        dest_node = (DB_BranchNode *) ((char *) right_page + new_offset);

        /* Copy the node */
        memcpy(dest_node, src_node, node_size);

        /* Update right page slot array */
        right_slots[right_idx] = new_offset;

        /* Update right page header */
        right_page->num_keys++;
        right_page->lower += sizeof(uint16_t);
        right_page->upper = new_offset;
    }

    /* Update left page header to reflect the reduced number of keys
     * CRITICAL: For B+tree branches with "minimum key" semantics:
     * - Left page gets entries [0..split_point-1]
     * - Right page gets entries [split_point+1..end]
     * - Entry at split_point is NOT in either page - its key becomes the divider,
     *   and its child pointer is LOST (which is the bug!)
     *
     * TODO: Fix this properly by including split_point in one of the pages
     */
    left_page->num_keys = split_point;
    left_page->lower = sizeof(DB_PageHeader) + (split_point * sizeof(uint16_t));

    /* CRITICAL BUG FIX: Recalculate upper pointer to reflect only remaining entries
     * After moving entries [split_point..end] to right page, their data is still
     * in left page's data area. We need to find the minimum offset among entries
     * [0..split_point-1] to reclaim that space.
     */
    if (split_point > 0) {
        uint16_t *left_slots = db_page_slots(left_page);
        uint16_t min_offset = PAGE_SIZE;
        for (uint16_t i = 0; i < split_point; i++) {
            uint16_t offset = left_slots[i];
            if (offset < min_offset) {
                min_offset = offset;
            }
        }
        left_page->upper = min_offset;
    }
    else {
        /* Edge case: all entries moved to right */
        left_page->upper = PAGE_SIZE;
    }

    /* Set divider key = minimum key of right page (right[0].key)
     * This will be pushed up to the parent to guide searches.
     */
    if (right_page->num_keys > 0) {
        divider_node = db_page_branch_node(right_page, 0);
        divider_key_out->data = db_branch_node_key(divider_node);
        divider_key_out->size = divider_node->key_size;
    }
    else {
        /* This shouldn't happen in a proper split, but handle it */
        return APR_EGENERAL;
    }

    /* Store the right page in dirty pages hash */
    pgno_key = apr_palloc(txn->pool, sizeof(pgno_t));
    if (!pgno_key) {
        return APR_ENOMEM;
    }
    *pgno_key = right_pgno;
    apr_hash_set(txn->dirty_pages, pgno_key, sizeof(pgno_t), right_page);

    /* Return the right page */
    *right_page_out = right_page;

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
apr_status_t db_find_leaf_page_with_path(napr_db_txn_t *txn, const napr_db_val_t *key, pgno_t *path_out, uint16_t *path_len_out, DB_PageHeader **leaf_page_out)
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
