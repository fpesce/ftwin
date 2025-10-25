This provides the detailed TDD prompts for Iterations 3 through 7, covering the B+ Tree implementation, Copy-on-Write mechanism, MVCC, and Free Space Management, filling the gap requested.

### Phase 1: `napr_db` (Continued)

#### Iteration 3: B+ Tree Read Path (Search)

##### Prompt 3.1: Slotted Page Navigation and Binary Search

```text
Objective: Implement the internal helper functions to navigate and search within B+ Tree pages (Branch and Leaf).

Context: `napr_db` uses a "slotted page" design (Spec 2.3). We need robust functions to access nodes (the fixed-size pointers) and the data they point to (variable-size keys/values).

Prerequisite: Iterations 1 and 2 complete.

Task 1: Create/Update `src/napr_db_internal.h` with page accessors.
1. Implement optimized macros or static inline functions:
   - `PAGE_GET_NODE(page, index)`: Returns a pointer to the node (BranchNode or LeafNode) at the given index (located immediately after the header).
   - `NODE_GET_KEY_PTR(node)`: Given a node, returns a pointer to the actual key data within the page (using the node's offset).
   - `NODE_GET_VALUE_PTR(node)`: Given a leaf node, returns the pointer to the value data.

Task 2: Implement Page Search in `src/napr_db_tree.c` (new file).
1. Implement `db_page_search(DB_PageHeader *page, napr_db_val_t *key, uint16_t *index_out)`:
   - Performs a binary search on the nodes array.
   - Handles both Branch and Leaf pages appropriately based on `page->flags`.
   - Returns `APR_SUCCESS` if an exact match is found, `APR_NOTFOUND` otherwise.
   - Crucially, `*index_out` must be set to the position of the match, or the position where the key *should* be inserted (the insertion point).

Task 3: Create `check/check_db_page.c`.
1. Manually construct mock `DB_PageHeader` structures in memory (Leaf and Branch).
2. Populate them with several sorted keys, simulating the slotted layout (header, nodes, packed data).
3. Test Accessors: Verify pointers and offsets are correct.
4. Test `db_page_search`:
   - Test searching for existing keys.
   - Test searching for non-existent keys.
   - Verify the returned `index_out` correctly identifies the insertion point in all cases (before first, between keys, after last).
```

##### Prompt 3.2: Tree Traversal and `napr_db_get`

```text
Objective: Implement the tree traversal logic to find a key and implement the public `napr_db_get` function with zero-copy semantics.

Prerequisite: Prompt 3.1 complete.

Task 1: Implement Tree Traversal in `src/napr_db_tree.c`.
1. Implement `db_find_leaf_page(napr_db_txn_t *txn, napr_db_val_t *key, DB_PageHeader **leaf_page_out)`:
   - Start traversal from the transaction's snapshot root (`txn->root_pgno`).
   - Loop:
     - Load the current page from the memory map (using PGNO).
     - If Branch: Use `db_page_search` to find the correct child PGNO. Update current PGNO and continue loop.
     - If Leaf: Stop traversal. Set `*leaf_page_out`.

Task 2: Implement `napr_db_get` in `src/napr_db.c`.
1. Implement `napr_db_get(txn, key, data)`:
   - Call `db_find_leaf_page` to locate the correct leaf.
   - Call `db_page_search` on the leaf page.
   - If found:
     - Get the node and use accessors to find the value pointer/size.
     - Populate the output `data` struct (Zero-copy).
   - If not found: Return `APR_NOTFOUND`.

Task 3: Create `check/check_db_read.c`.
1. TDD challenge: We cannot insert data programmatically yet. The tests must manually construct a small, multi-level B+ tree directly in memory/file.
2. Construct a tree (e.g., Root (Branch) -> Leaf 1, Leaf 2).
3. Start a read transaction pointing to this structure.
4. Test `napr_db_get`: Verify keys in Leaf 1 and 2 can be found and that the returned pointers are correct. Test misses.
```

#### Iteration 4: B+ Tree Write Path (CoW and Basic Insertion)

##### Prompt 4.1: Page Allocation and Dirty Page Tracking

```text
Objective: Implement the mechanisms for allocating new pages and tracking modifications (dirty pages) within a write transaction, the foundation of CoW.

Prerequisite: Iteration 3 complete.

Task 1: Update `src/napr_db_internal.h`.
1. Update `napr_db_txn_t`: Add a structure to track dirty pages (e.g., an APR hash table mapping original PGNO to the in-memory dirty copy buffer).

Task 2: Implement Page Management in `src/napr_db_page.c` (new file) or `src/napr_db_tree.c`.
1. Implement `db_page_alloc(napr_db_txn_t *txn, uint32_t count, pgno_t *pgno_out)`:
   - Basic allocation strategy: Increment the environment's `last_pgno` counter. (We will refine this in Iteration 7 for FreeDB reuse).
   - Return the starting page number.
2. Implement `db_page_get_writable(napr_db_txn_t *txn, DB_PageHeader *original_page, DB_PageHeader **dirty_copy_out)` (The Core CoW step):
   - Check if the page is already in the transaction's dirty list. If yes, return the existing copy.
   - If no: Allocate memory (using the transaction's pool).
   - Copy the content of `original_page` into the new buffer.
   - Add the new copy to the dirty list.
   - Set `*dirty_copy_out`.

Task 3: Create `check/check_db_cow.c`.
1. Start a write transaction.
2. Test `db_page_alloc`: Verify `last_pgno` increases correctly.
3. Test `db_page_get_writable`:
   - Request a writable copy of an existing page. Verify a new buffer is returned (different address from the mmap).
   - Modify the buffer.
   - Request the same page again. Verify the *same* modified buffer is returned.
```

##### Prompt 4.2: Basic Insertion (`napr_db_put`) and CoW Path Copying

```text
Objective: Implement `napr_db_put` for the simple case (no page splits) and ensure the entire path from the modification to the root is copied (CoW).

Prerequisite: Prompt 4.1 complete.

Task 1: Implement Insertion Helpers in `src/napr_db_tree.c`.
1. Implement `db_page_insert(DB_PageHeader *page, uint16_t index, napr_db_val_t *key, napr_db_val_t *value, pgno_t child_pgno)`:
   - Assumes `page` is dirty and has enough free space.
   - Shifts the node array to make room at `index`.
   - Packs the new key/value data into the data area (growing downwards).
   - Updates the node at `index` with offsets/sizes.
   - Updates the page header (item_count, upper bound).

Task 2: Implement CoW Traversal and `napr_db_put`.
1. Refactor Traversal: The traversal logic (`db_find_leaf_page`) needs modification for writes. It must track the path taken (a "cursor stack" of parents).
2. Implement `napr_db_put(txn, key, data, flags)`:
   - Perform traversal to find the insertion point on the leaf, recording the path.
   - Check if the leaf has enough space. (If not, return `APR_ENOSPC` for now; handled in Iteration 5).
   - CRITICAL (CoW Path Copying): Iterate up the recorded path from the leaf to the root. Call `db_page_get_writable` for each page. This ensures the transaction operates on its own copied version of the tree structure.
   - Call `db_page_insert` on the dirty leaf.

Task 3: Create `check/check_db_write.c`.
1. Start a write transaction.
2. Insert several keys (`napr_db_put`) that do not fill the first page.
3. Verify (within the same transaction) that the data is visible via `napr_db_get`.
4. Abort the transaction. Verify the data is gone (Test Atomicity).
```

##### Prompt 4.3: Transaction Commit and Durability

```text
Objective: Implement the full commit logic, ensuring data durability and atomic meta page updates (Spec 4.3).

Prerequisite: Prompt 4.2 complete.

Task 1: Implement the full logic for `napr_db_txn_commit(txn)` in `src/napr_db.c`.
1. Allocate Physical PGNOs: Iterate the dirty list. Assign new physical PGNOs using `db_page_alloc`.
2. Update Pointers: Crucial step. Ensure that if a dirty page A points to a child page B (which is also dirty), A's pointer is updated to B's *newly allocated* PGNO. Identify the new root PGNO.
3. Writeback Phase: Write the content of all dirty pages to their newly allocated locations in the file/memory map.
4. Durability Step 1 (Data Flush): Ensure data pages are flushed to disk (`msync` or `apr_file_sync`).
5. Meta Page Selection: Identify the "stale" meta page (the one not used as the snapshot root).
6. Meta Page Update: Update the stale meta page with the new root PGNO, the new TXNID, and the new `last_pgno`. Calculate checksum.
7. Atomic Commit Point (Durability Step 2): Write the updated meta page and synchronously flush it (`apr_file_sync`).
8. Update Environment: Update `env->live_meta` to the newly committed meta page.
9. Release Writer Lock (implemented in Iteration 2).

Task 2: Update `check/check_db_write.c`.
1. Test Persistence:
   - Insert data, Commit.
   - Close the environment. Re-open the environment.
   - Verify data exists and is correct.
2. Test Isolation: Start Read Txn R1. Start Write Txn W1, insert data, commit. Verify R1 does *not* see the new data. Start Read Txn R2. Verify R2 sees the new data.
```

#### Iteration 5: B+ Tree Scaling (Splitting)

##### Prompt 5.1: Leaf Page Splitting

```text
Objective: Implement the logic to split a leaf page when it becomes full during insertion.

Prerequisite: Iteration 4 complete.

Task 1: Implement Splitting Logic in `src/napr_db_tree.c`.
1. Implement `db_split_leaf(napr_db_txn_t *txn, DB_PageHeader *left_page, DB_PageHeader **right_page_out, napr_db_val_t *divider_key_out)`:
   - Allocate a new dirty page for the right sibling.
   - Determine the split point (median key) to balance the data.
   - Move the upper half of the keys/data from `left_page` to the new `right_page`.
   - Update headers for both pages.
   - Identify the `divider_key_out` (the first key on the new right page).

Task 2: Update `napr_db_put` integration.
1. Modify the insertion logic: If the target page is full, initiate the split.
2. Determine which page (left or right) the new key belongs to and insert it there.
3. The resulting `divider_key` and the PGNO of the new right page must be inserted into the parent page (tracked via the traversal stack).

Task 3: Create `check/check_db_split.c`.
1. Test Leaf Split: Insert keys sequentially until the first page fills.
2. Verify that a split occurs.
3. Verify the data is balanced between the two new pages.
4. (Integration testing of the parent update will rely on the next prompt).
```

##### Prompt 5.2: Branch Page Splitting and Recursion

```text
Objective: Implement branch page splitting and ensure the splitting process recurses up the tree, including handling root splits.

Prerequisite: Prompt 5.1 complete.

Task 1: Implement Branch Splitting in `src/napr_db_tree.c`.
1. Implement `db_split_branch(...)`: Similar logic to leaf split, but moves branch nodes (keys and pointers). CRITICAL: The divider key in a branch split is "pushed up" to the parent, not copied into the right sibling.
2. Implement Recursive Insertion: Refactor the insertion logic to handle the propagation. When inserting the divider key into the parent (from the stack), check if the parent is also full. If so, recursively split the parent.

Task 3: Handle Root Splitting.
1. If the recursion reaches the root and the root must split:
   - Allocate a new root page (Branch type).
   - The old root is split into two children.
   - The new root contains only the divider key and pointers to the two new children.
   - Update the transaction's root pointer.

Task 4: Update `check/check_db_split.c`.
1. Stress Test Branch Splits: Insert enough keys (sequentially or randomly) to force the tree height to increase (e.g., 100,000+ keys).
2. Verify the tree remains balanced and all data is accessible via `napr_db_get`.
3. Verify the root split occurred correctly.
```

#### Iteration 6: Cursors and Deletion

##### Prompt 6.1: Cursor Initialization and Positioning

```text
Objective: Implement the cursor structure and basic positioning operations (FIRST, LAST, SET).

Context: Due to the lack of sibling pointers (Spec 2.3.3), the cursor must maintain a stack of the traversal path (parent pages and indices) to enable iteration.

Prerequisite: Iteration 5 complete.

Task 1: Update `src/napr_db_internal.h`.
1. Define the concrete `struct napr_db_cursor_t`.
   - Must include a stack structure (e.g., an array of `{DB_PageHeader *page, uint16_t index}`).
   - Pointer to the transaction.

Task 2: Implement Cursor Management in `src/napr_db_cursor.c` (new file).
1. Implement `napr_db_cursor_open` and `napr_db_cursor_close`.
2. Implement internal helpers for stack management (`cursor_push`, `cursor_pop`).
3. Implement `db_cursor_seek(cursor, key, op)`:
   - Traverses the tree similarly to `db_find_leaf_page`, but pushes the page and index onto the cursor stack at each level.
   - Handle `NAPR_DB_FIRST`: Traverse down the leftmost path.
   - Handle `NAPR_DB_LAST`: Traverse down the rightmost path.
   - Handle `NAPR_DB_SET`/`NAPR_DB_SET_RANGE`: Search for the key.

Task 3: Implement `napr_db_cursor_get(cursor, key, data, op)` (Positioning).
1. Handle positioning operations by calling `db_cursor_seek`.
2. Retrieve the key/value at the current cursor position (top of the stack).

Task 4: Create `check/check_db_cursor.c`.
1. Populate the database with a known dataset spanning multiple pages.
2. Test `NAPR_DB_FIRST` and `NAPR_DB_LAST`.
3. Test `NAPR_DB_SET` (exact match) and `NAPR_DB_SET_RANGE` (inexact match).
```

##### Prompt 6.2: Cursor Iteration (NEXT/PREV)

```text
Objective: Implement the complex logic for sequential iteration (NEXT/PREV) using the cursor stack.

Prerequisite: Prompt 6.1 complete.

Task 1: Implement Iteration Logic in `src/napr_db_cursor.c`.
1. Implement `db_cursor_next(cursor)`:
   - Get the current position (top of stack).
   - Try incrementing the index on the current leaf page.
   - If index is still within bounds: Success.
   - If index goes off the end of the leaf:
     - Ascend: Pop the stack to the parent (Branch page).
     - If parent is NULL (stack empty): Return `APR_NOTFOUND` (End of DB).
     - Find the next available index on the parent branch.
     - If parent index goes off the end: Recursively ascend further.
     - Descend: Once the next sibling subtree is found, descend down its leftmost path, pushing onto the stack until a new leaf is reached.
2. Implement `db_cursor_prev(cursor)`: Symmetric logic to `cursor_next`, but decrementing indices and descending down the rightmost path when crossing boundaries.

Task 2: Update `napr_db_cursor_get` to handle `NAPR_DB_NEXT` and `NAPR_DB_PREV`.

Task 3: Update `check/check_db_cursor.c`.
1. Test Full Iteration: Use FIRST followed by NEXT loop. Verify all keys are returned in lexicographical order.
2. CRITICAL: Test Boundary Conditions: Specifically test iteration across page boundaries (verifies the ascent/descent logic).
3. Test Backward Iteration.
```

##### Prompt 6.3: Deletion (`napr_db_del`)

```text
Objective: Implement data removal.

Note: We implement simple deletion without full B+ tree rebalancing (merging), which is often omitted in this architecture.

Prerequisite: Prompt 6.2 complete.

Task 1: Implement Deletion Helpers in `src/napr_db_tree.c`.
1. Implement `db_page_delete(DB_PageHeader *page, uint16_t index)`:
   - Remove the node at `index` by shifting the node array.
   - Remove the corresponding data by compacting the data area (slotted page compaction).
   - Update page header (item_count, upper bound).

Task 2: Implement `napr_db_del` in `src/napr_db.c`.
1. Implement `napr_db_del(txn, key, data)`:
   - Perform CoW traversal (similar to `napr_db_put`) to locate the key and prepare the path for modification.
   - If found: Call `db_page_delete` on the dirty leaf page.

Task 3: Create `check/check_db_delete.c`.
1. Insert A, B, C.
2. Delete B. Commit.
3. Verify A, C exist, B returns `APR_NOTFOUND`.
4. Test deleting the first and last keys on a page, verifying structure integrity.
```

#### Iteration 7: MVCC and Free Space Management

##### Prompt 7.1: MVCC Reader Tracking

```text
Objective: Implement the MVCC Reader Tracking Table to monitor active read transactions and their snapshot TXNIDs (Spec 3.2).

Prerequisite: Iteration 6 complete.

Task 1: Update `src/napr_db_internal.h`.
1. Define the `DB_ReaderSlot` structure: process ID, thread ID, TXNID.
2. CRITICAL (Spec 3.2): Ensure `sizeof(DB_ReaderSlot)` is aligned to a CPU cache line (e.g., 64 bytes) using padding/alignment attributes to prevent false sharing.
3. Update `napr_db_env_t` to include the Reader Table (array of slots) and a mutex to protect it.

Task 2: Update Transaction Lifecycle in `src/napr_db.c`.
1. Initialize the Reader Table and its mutex in `napr_db_env_open`.
2. Update `napr_db_txn_begin` (Read-only path):
   - Acquire Reader Table lock.
   - Find an empty slot.
   - Register the current thread/process and the snapshot TXNID.
   - Release the lock.
3. Update `napr_db_txn_commit/abort` (Read-only path):
   - Clear the corresponding slot (thread-safe).
4. Implement `db_get_oldest_reader_txnid(env)`: Scans the Reader Table and returns the minimum active TXNID.

Task 3: Create `check/check_db_mvcc.c`.
1. Verify `DB_ReaderSlot` size/alignment with `_Static_assert`.
2. Test Reader Registration: Start multiple concurrent read transactions. Verify the Reader Table accurately reflects their state and TXNIDs.
3. Test `db_get_oldest_reader_txnid` correctness.
```

##### Prompt 7.2: Free Space Management (The Free DB)

```text
Objective: Implement the mechanism to track pages freed by transactions using the dedicated Free DB (Spec 2.4).

Context: Freed pages cannot be reused immediately due to MVCC. The "Free DB" is a separate B+ Tree (Key=TXNID, Value=Array of freed PGNOs).

Prerequisite: Prompt 7.1 complete.

Task 1: Generalize B+ Tree Implementation.
1. Refactor B+ Tree functions (read/write) to operate on a specific database root (either `main_db_root` or `free_db_root` from the meta page).

Task 2: Update Write Transaction Logic.
1. Update `napr_db_txn_t`: Add a list to track pages freed during the current transaction.
2. Update CoW logic: When a page is replaced via CoW, add its original `pgno_t` to the transaction's freed list.
3. Update `napr_db_txn_commit`:
   - Before the final meta page update, insert the list of freed pages into the Free DB.
   - Key = Current TXNID; Value = Serialized list of freed PGNOs.
   - This insertion must be part of the same atomic transaction.

Task 4: Update `check/check_db_mvcc.c`.
1. Test Free List Population: Perform a write transaction involving CoW (e.g., update an existing key). Commit.
2. Verify (using inspection tools or dedicated read functions) that an entry corresponding to the TXNID and the old page PGNOs now exists in the Free DB.
```

##### Prompt 7.3: Safe Page Reclamation

```text
Objective: Refine the page allocation logic to safely reuse pages from the Free DB based on MVCC rules (Spec 2.4).

Prerequisite: Prompt 7.2 complete.

Task 1: Refine Page Allocation in `src/napr_db_page.c` or `src/napr_db_tree.c`.
1. Refine `db_page_alloc(txn, ...)`:
   - Determine the oldest active reader TXNID using `db_get_oldest_reader_txnid(env)`.
   - Query the Free DB: Search for entries with TXNID < Oldest Reader TXNID.
   - If reusable pages are found:
     - Select a page.
     - Remove it from the Free DB (This is a write operation within the current transaction).
     - Return the reused PGNO.
   - If no reusable pages are found: Fall back to extending the file (incrementing `last_pgno`).

Task 2: Update `check/check_db_mvcc.c`.
1. Test Safe Reclamation (Complex Scenario):
   - Start: DB state V1.
   - Start Read Txn R1 (Snapshot V1).
   - Start Write Txn W1. Modify Page P. Commit (V2). Page P (V1) is added to Free DB.
   - Start Write Txn W2 (needs allocation).
   - Verify W2 *cannot* reuse Page P (V1) because R1 is still active. It extends the file.
   - End R1.
   - Start Write Txn W3 (needs allocation).
   - Verify W3 *can* reuse Page P (V1).
```
