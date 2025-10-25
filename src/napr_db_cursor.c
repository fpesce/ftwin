/**
 * @file napr_db_cursor.c
 * @brief B+ Tree cursor implementation for napr_db
 *
 * Implements cursor creation, positioning, and data retrieval.
 */

#include "napr_db_internal.h"
#include <string.h>

/* Forward declarations for internal helpers */
static apr_status_t db_cursor_seek(napr_db_cursor_t *cursor, const napr_db_val_t *key, napr_db_cursor_op_t operation);

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

        /* TODO: Implement NEXT, PREV, GET_CURRENT in subsequent iterations */
    case NAPR_DB_GET_CURRENT:
    case NAPR_DB_NEXT:
    case NAPR_DB_PREV:
        return APR_ENOTIMPL;

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
