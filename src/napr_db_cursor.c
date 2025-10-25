/**
 * @file napr_db_cursor.c
 * @brief B+ Tree cursor implementation for napr_db
 *
 * Implements cursor creation, positioning, and data retrieval.
 */

#include "napr_db_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Forward declarations for internal helpers */
static apr_status_t db_cursor_seek(napr_db_cursor_t *cursor, const napr_db_val_t *key, napr_db_cursor_op_t operation);
static apr_status_t db_cursor_next(napr_db_cursor_t *cursor);
static apr_status_t db_cursor_prev(napr_db_cursor_t *cursor);

/**
 * @brief Get a page, checking dirty pages first for write transactions.
 */
static inline DB_PageHeader *db_get_page(napr_db_txn_t *txn, pgno_t pgno)
{
    DB_PageHeader *page = NULL;
    if (!(txn->flags & NAPR_DB_RDONLY)) {
        page = apr_hash_get(txn->dirty_pages, &pgno, sizeof(pgno_t));
        if (page) {
            return page;
        }
    }
    return (DB_PageHeader *) ((char *) txn->env->map_addr + (pgno * PAGE_SIZE));
}

/**
 * @brief Open a cursor for iteration.
 */
apr_status_t napr_db_cursor_open(napr_db_txn_t *txn, napr_db_cursor_t **cursor_out)
{
    apr_pool_t *pool = NULL;
    napr_db_cursor_t *cursor = NULL;
    apr_status_t status = APR_SUCCESS;

    if (!txn || !cursor_out) {
        return APR_EINVAL;
    }

    /* Create a subpool for the cursor */
    status = apr_pool_create(&pool, txn->pool);
    if (status != APR_SUCCESS) {
        return status;
    }

    /* Allocate cursor from the new pool */
    cursor = apr_pcalloc(pool, sizeof(napr_db_cursor_t));
    if (!cursor) {
        apr_pool_destroy(pool);
        return APR_ENOMEM;
    }

    cursor->pool = pool;
    cursor->txn = txn;
    cursor->top = 0;
    cursor->eof = 0;

    *cursor_out = cursor;
    return APR_SUCCESS;
}

/**
 * @brief Close a cursor.
 */
apr_status_t napr_db_cursor_close(napr_db_cursor_t *cursor)
{
    if (!cursor) {
        return APR_EINVAL;
    }
    apr_pool_destroy(cursor->pool);
    return APR_SUCCESS;
}

/**
 * @brief Push a page and index onto the cursor's stack.
 */
static inline void cursor_push(napr_db_cursor_t *cursor, DB_PageHeader *page, uint16_t index)
{
    if (cursor->top < MAX_TREE_DEPTH) {
        cursor->stack[cursor->top].page = page;
        cursor->stack[cursor->top].index = index;
        cursor->top++;
    }
}

/**
 * @brief Traverse the tree to position the cursor.
 */
static apr_status_t db_cursor_seek(napr_db_cursor_t *cursor, const napr_db_val_t *key, napr_db_cursor_op_t operation)
{
    pgno_t current_pgno = cursor->txn->root_pgno;
    DB_PageHeader *page = NULL;
    uint16_t index = 0;
    apr_status_t status = APR_SUCCESS;

    /* Reset stack */
    cursor->top = 0;
    cursor->eof = 0;

    if (current_pgno == 0) {
        cursor->eof = 1;
        return APR_NOTFOUND;    /* Empty tree */
    }

    while (1) {
        page = db_get_page(cursor->txn, current_pgno);
        if (!page) {
            return APR_EGENERAL;
        }

        if (page->flags & P_LEAF) {
            cursor_push(cursor, page, 0);       /* Push leaf page */
            break;
        }

        if (!(page->flags & P_BRANCH)) {
            return APR_EGENERAL;
        }

        /* Branch page: find child to follow */
        switch (operation) {
        case NAPR_DB_FIRST:
            index = 0;
            break;
        case NAPR_DB_LAST:
            index = page->num_keys - 1;
            break;
        case NAPR_DB_SET:
        case NAPR_DB_SET_RANGE:
            status = db_page_search(page, key, &index);
            if (status == APR_NOTFOUND && index > 0) {
                index--;
            }

            if (index >= page->num_keys) {
                index = page->num_keys - 1;
            }
            break;
        case NAPR_DB_NEXT:
        case NAPR_DB_PREV:
        case NAPR_DB_GET_CURRENT:
        default:
            return APR_EINVAL;
        }

        cursor_push(cursor, page, index);
        current_pgno = db_page_branch_node(page, index)->pgno;
    }

    /* Now at the leaf page, position index within the page */
    page = cursor->stack[cursor->top - 1].page;
    switch (operation) {
    case NAPR_DB_FIRST:
        index = 0;
        break;
    case NAPR_DB_LAST:
        index = page->num_keys > 0 ? page->num_keys - 1 : 0;
        break;
    case NAPR_DB_SET:
        status = db_page_search(page, key, &index);
        if (status != APR_SUCCESS) {
            return status;
        }
        break;
    case NAPR_DB_SET_RANGE:
        status = db_page_search(page, key, &index);
        /* If exact match or insertion point is found, it's valid */
        if (index >= page->num_keys) {
            /* TODO: Handle NEXT logic to find next page */
            return APR_NOTFOUND;
        }
        break;
    case NAPR_DB_NEXT:
    case NAPR_DB_PREV:
    case NAPR_DB_GET_CURRENT:
    default:
        return APR_EINVAL;
    }

    if (page->num_keys == 0) {
        cursor->eof = 1;
        return APR_NOTFOUND;
    }

    cursor->stack[cursor->top - 1].index = index;
    return APR_SUCCESS;
}

/**
 * @brief Advance cursor to the next key.
 *
 * This function implements sequential iteration without sibling pointers
 * by using the cursor's stack to navigate up to parents and down to siblings.
 *
 * Algorithm:
 * 1. Try to increment index on current leaf page
 * 2. If successful (still within bounds), done
 * 3. If off the end of leaf, ascend to parent:
 *    - Pop stack to get parent branch page
 *    - Try to increment parent's index
 *    - If parent index off the end, recursively ascend
 *    - Once valid parent index found, descend leftmost to new leaf
 *
 * @param cursor The cursor to advance
 * @return APR_SUCCESS if moved to next key, APR_NOTFOUND if at end
 */
static apr_status_t db_cursor_next(napr_db_cursor_t *cursor)
{
    DB_PageHeader *page = NULL;
    uint16_t index = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    pgno_t child_pgno;

    if (!cursor || cursor->top == 0 || cursor->eof) {
        return APR_NOTFOUND;
    }

    /* Get current position (leaf page is at top of stack) */
    page = cursor->stack[cursor->top - 1].page;
    index = cursor->stack[cursor->top - 1].index;

    /* Try incrementing index on current leaf */
    index++;
    if (index < page->num_keys) {
        /* Still within current leaf page */
        cursor->stack[cursor->top - 1].index = index;
        return APR_SUCCESS;
    }

    /* Went off the end of leaf - need to ascend to find next sibling */
    cursor->top--;              /* Pop the leaf */

    /* If stack is now empty, we were at root (which was a leaf) */
    if (cursor->top == 0) {
        cursor->eof = 1;
        return APR_NOTFOUND;
    }

    /* Ascend the tree until we find a parent with a next sibling */
    while (cursor->top > 0) {
        page = cursor->stack[cursor->top - 1].page;
        index = cursor->stack[cursor->top - 1].index;

        /* Try to move to next child in this parent */
        index++;
        if (index < page->num_keys) {
            /* Found next sibling - update parent's index */
            cursor->stack[cursor->top - 1].index = index;

            /* Descend down the leftmost path of this sibling subtree */
            child_pgno = db_page_branch_node(page, index)->pgno;

            while (1) {
                page = db_get_page(cursor->txn, child_pgno);
                if (!page) {
                    cursor->eof = 1;
                    return APR_EGENERAL;
                }

                if (page->flags & P_LEAF) {
                    /* Reached leaf - push it with index 0 */
                    cursor_push(cursor, page, 0);
                    if (page->num_keys == 0) {
                        cursor->eof = 1;
                        return APR_NOTFOUND;
                    }
                    return APR_SUCCESS;
                }

                /* Branch page - push and follow leftmost child */
                cursor_push(cursor, page, 0);
                child_pgno = db_page_branch_node(page, 0)->pgno;
            }
        }

        /* This parent also exhausted - ascend further */
        cursor->top--;
    }

    /* Reached root and exhausted all entries */
    cursor->eof = 1;
    return APR_NOTFOUND;
}

/**
 * @brief Move cursor to the previous key.
 *
 * Symmetric to db_cursor_next() but in reverse:
 * 1. Try to decrement index on current leaf page
 * 2. If successful (still >= 0), done
 * 3. If before start of leaf, ascend to parent:
 *    - Pop stack to get parent branch page
 *    - Try to decrement parent's index
 *    - If parent index becomes negative, recursively ascend
 *    - Once valid parent index found, descend rightmost to new leaf
 *
 * @param cursor The cursor to move backward
 * @return APR_SUCCESS if moved to previous key, APR_NOTFOUND if at beginning
 */
/**
 * @brief Descend to the rightmost leaf of a subtree.
 */
static apr_status_t descend_rightmost(napr_db_cursor_t *cursor, pgno_t pgno)
{
    DB_PageHeader *page = NULL;

    while (1) {
        page = db_get_page(cursor->txn, pgno);
        if (!page) {
            cursor->eof = 1;
            return APR_EGENERAL;
        }

        if (page->flags & P_LEAF) {
            if (page->num_keys == 0) {
                cursor->eof = 1;
                return APR_NOTFOUND;
            }
            cursor_push(cursor, page, page->num_keys - 1);
            return APR_SUCCESS;
        }

        if (page->num_keys == 0) {
            cursor->eof = 1;
            return APR_NOTFOUND;
        }
        cursor_push(cursor, page, page->num_keys - 1);
        pgno = db_page_branch_node(page, page->num_keys - 1)->pgno;
    }
}

static apr_status_t db_cursor_prev(napr_db_cursor_t *cursor)
{
    DB_PageHeader *page = NULL;
    uint16_t index = 0;

    if (!cursor || cursor->top == 0 || cursor->eof) {
        return APR_NOTFOUND;
    }

    page = cursor->stack[cursor->top - 1].page;
    index = cursor->stack[cursor->top - 1].index;

    if (index > 0) {
        cursor->stack[cursor->top - 1].index = index - 1;
        return APR_SUCCESS;
    }

    cursor->top--;

    if (cursor->top == 0) {
        cursor->eof = 1;
        return APR_NOTFOUND;
    }

    while (cursor->top > 0) {
        page = cursor->stack[cursor->top - 1].page;
        index = cursor->stack[cursor->top - 1].index;

        if (index > 0) {
            index--;
            cursor->stack[cursor->top - 1].index = index;
            return descend_rightmost(cursor, db_page_branch_node(page, index)->pgno);
        }

        cursor->top--;
    }

    cursor->eof = 1;
    return APR_NOTFOUND;
}

/**
 * @brief Retrieve key-value pairs using cursor.
 */
apr_status_t napr_db_cursor_get(napr_db_cursor_t *cursor, napr_db_val_t *key, napr_db_val_t *data, napr_db_cursor_op_t operation)
{
    apr_status_t status = APR_SUCCESS;

    switch (operation) {
    case NAPR_DB_FIRST:
    case NAPR_DB_LAST:
    case NAPR_DB_SET:
    case NAPR_DB_SET_RANGE:
        status = db_cursor_seek(cursor, (const napr_db_val_t *) key, operation);
        if (status != APR_SUCCESS) {
            return status;
        }
        break;

    case NAPR_DB_NEXT:
        status = db_cursor_next(cursor);
        if (status != APR_SUCCESS) {
            return status;
        }
        break;

    case NAPR_DB_PREV:
        status = db_cursor_prev(cursor);
        if (status != APR_SUCCESS) {
            return status;
        }
        break;

    case NAPR_DB_GET_CURRENT:
        /* Just retrieve current position, no movement needed */
        break;

    default:
        return APR_EINVAL;
    }

    if (cursor->eof || cursor->top == 0) {
        return APR_NOTFOUND;
    }

    /* Retrieve data from current cursor position */
    uint16_t top_idx = cursor->top - 1;
    DB_PageHeader *leaf_page = cursor->stack[top_idx].page;
    uint16_t leaf_index = cursor->stack[top_idx].index;

    if (leaf_index >= leaf_page->num_keys) {
        return APR_NOTFOUND;
    }

    DB_LeafNode *node = db_page_leaf_node(leaf_page, leaf_index);
    if (key) {
        key->data = db_leaf_node_key(node);
        key->size = node->key_size;
    }
    if (data) {
        data->data = db_leaf_node_value(node);
        data->size = node->data_size;
    }

    return APR_SUCCESS;
}
