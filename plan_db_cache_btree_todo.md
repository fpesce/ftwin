```markdown
# Project Checklist: napr_db and napr_cache

This checklist follows the detailed project blueprint, organized by phases and iterations, emphasizing Test-Driven Development (TDD) and adherence to the technical specifications.

## Phase 1: `napr_db` (Storage Engine - LMDB Archetype)

### Iteration 1: Environment and On-Disk Structure

- [x] **API Definition (`src/napr_db.h`)**
    - [x] Include necessary APR headers (pools, errno, file_io).
    - [x] Define `napr_db_val_t` (size and data pointer).
    - [x] Declare opaque handles (`napr_db_env_t`, `napr_db_txn_t`, `napr_db_cursor_t`).
    - [x] Define flags (`NAPR_DB_RDONLY`, `NAPR_DB_CREATE`, `NAPR_DB_INTRAPROCESS_LOCK`).
    - [x] Define cursor operations (e.g., `NAPR_DB_FIRST`, `NAPR_DB_NEXT`).
    - [x] Declare public API function prototypes.
- [x] **Internal Definitions (`src/napr_db_internal.h`)**
    - [x] Define types (`pgno_t` uint64_t, `txnid_t` uint64_t).
    - [x] Define constants (`PAGE_SIZE` 4096, `DB_MAGIC` 0xDECAFBAD, `DB_VERSION` 1).
    - [x] Define Page Flags (P_BRANCH, P_LEAF, P_OVERFLOW, P_FREE).
    - [x] Define On-Disk Structures (`DB_PageHeader`, `DB_MetaPage`).
    - [x] Define B+ Tree Node Structures (`DB_BranchNode`, `DB_LeafNode`).
- [x] **Testing (`check/check_db_layout.c`)**
    - [x] Write tests using `_Static_assert` and `offsetof()` to validate structure sizes and offsets against the specification (Critical for zero-copy).
- [x] **Environment Lifecycle Implementation (`src/napr_db.c`)**
    - [x] Define the concrete `struct napr_db_env_t` (pool, mapsize, file handle, mmap, meta pointers).
    - [x] Implement `napr_db_env_create` and `napr_db_env_set_mapsize`.
    - [x] Implement `napr_db_env_open`:
        - [x] File handling (Open/Create).
        - [x] Initialization (If new: Initialize and write Meta Pages 0 and 1, ensure durability via `apr_file_sync`).
        - [x] Memory Mapping (`apr_mmap_create`).
        - [x] Meta Page Validation (Magic/Version).
        - [x] Meta Page Selection (Select highest valid TXNID as `live_meta`).
    - [x] Implement `napr_db_env_close` (Unmap and close handles).
- [x] **Testing (`check/check_db_env.c`)**
    - [x] Test Create/SetMapsize/Close lifecycle.
    - [x] Test Open (New DB): Verify file creation and initial meta page contents.
    - [x] Test Open (Existing DB): Verify MMAP and correct meta page selection.

### Iteration 2: Transaction Framework and Synchronization

- [x] **Internal Definitions (`src/napr_db_internal.h`)**
    - [x] Update `napr_db_env_t` to hold synchronization primitives (`apr_thread_mutex_t*` and `apr_proc_mutex_t*`).
    - [x] Define the concrete `struct napr_db_txn_t` (env, pool, txnid, snapshot root pgno, flags).
- [x] **Implementation (`src/napr_db.c`)**
    - [x] Initialize the correct mutex in `napr_db_env_open` based on the `NAPR_DB_INTRAPROCESS_LOCK` flag.
    - [x] Implement internal helpers `db_writer_lock(env)` and `db_writer_unlock(env)` to abstract the mutex type.
    - [x] Implement `napr_db_txn_begin`:
        - [x] Acquire writer lock if it's a write transaction (SWMR enforcement).
        - [x] Capture snapshot (root pgno and txnid from `live_meta`).
        - [x] Increment TXNID if write transaction.
    - [x] Implement skeletal `napr_db_txn_commit` and `napr_db_txn_abort` (focus on releasing the writer lock).
- [x] **Testing (`check/check_db_txn.c`)**
    - [x] Test Read/Write transaction lifecycle.
    - [x] Test SWMR Concurrency: Verify that a second write transaction blocks until the first one finishes.

### Iteration 3: B+ Tree Read Path (Search)

- [x] **Implementation (`src/napr_db_internal.h` and `src/napr_db_tree.c`)**
    - [x] Implement helpers for navigating slotted pages (getting node pointers by index, getting key/data pointers from nodes).
    - [x] Implement binary search algorithm within a page (Branch and Leaf).
    - [x] Implement tree traversal (search from root down to the correct leaf).
    - [x] Implement `napr_db_get`:
        - [x] Perform tree search.
        - [x] Return data via `napr_db_val_t` (Zero-copy pointer directly into the memory map).
- [x] **Testing (`check/check_db_page.c` and `check/check_db_read.c`)**
    - [x] Test page accessor functions (leaf and branch pages).
    - [x] Test binary search for existing keys.
    - [x] Test binary search for non-existing keys (insertion points).
    - [x] Test empty page search.
    - [x] Test retrieval of known keys with manually constructed single-level tree.
    - [x] Test retrieval of known keys with manually constructed two-level tree.
    - [x] Test lookup misses (`APR_NOTFOUND`).

### Iteration 4: B+ Tree Write Path (CoW and Basic Insertion)

- [x] **Implementation (`src/napr_db_tree.c`)** - CoW Foundation (Partial)
    - [x] Implement basic page allocation (`db_page_alloc` - simple increment of `last_pgno` for now).
    - [x] Implement dirty page tracking within `napr_db_txn_t` (added `dirty_pages` hash and `new_last_pgno`).
    - [x] Implement the Copy-on-Write (CoW) mechanism (`db_page_get_writable`):
        - [x] When modifying a page, create a dirty copy in memory.
        - [x] Cache dirty copies in transaction's hash table for subsequent accesses.
    - [x] Ensure the path from the modified leaf up to the root uses these copied pages.
    - [x] Implement `napr_db_put` (Basic insertion without handling page splits).
- [x] **Implementation (`src/napr_db.c` and `src/napr_db_tree.c`)**
    - [x] Implement `db_page_insert` helper function for slotted page insertion.
    - [x] Implement `db_find_leaf_page_with_path` to record path during tree traversal.
    - [x] Implement `db_get_page` helper to check dirty pages before accessing mmap.
    - [x] Implement `napr_db_put` for basic insertion (no page splits):
        - [x] Handle empty tree case (allocate first root page).
        - [x] Traverse tree to find insertion point, recording path.
        - [x] CoW path propagation (copy all pages from leaf to root).
        - [x] Insert key/value into dirty leaf page.
    - [x] Implement full `napr_db_txn_commit` logic:
        - [x] Allocate new physical pages for all dirty pages (pgno mapping).
        - [x] Update pointers in dirty branch pages to new physical locations.
        - [x] Extend file if necessary to accommodate new pages.
        - [x] Write dirty pages to their new locations in the file.
        - [x] Durability Step 1: Flush data pages (`apr_file_sync`).
        - [x] Update the stale Meta Page (new root, TXNID, `last_pgno`).
        - [x] Atomic Commit Point (Durability Step 2): Flush the Meta Page (`apr_file_sync`).
- [x] **Testing (`check/check_db_cow.c`)** - CoW Foundation Tests
    - [x] Test `db_page_alloc` increments `last_pgno` correctly (single, multiple, sequential).
    - [x] Test `db_page_alloc` rejects read-only transactions.
    - [x] Test `db_page_get_writable` creates dirty copy on first call.
    - [x] Test `db_page_get_writable` returns same dirty copy on subsequent calls.
    - [x] Test `db_page_get_writable` modifications don't affect original page (isolation).
    - [x] Test `db_page_get_writable` tracks multiple pages independently.
    - [x] Test `db_page_get_writable` rejects read-only transactions.
- [x] **Testing (`check/check_db_write.c`)**
    - [x] Test basic insertion and retrieval (single and multiple keys).
    - [x] Test insertion in sorted and reverse order.
    - [x] Test atomicity (Verify changes are discarded on abort).
    - [x] Test duplicate key rejection.
    - [x] Test read-only transaction rejects writes.
    - [x] Test persistence (Close DB, re-open, verify data).
    - [x] Test MVCC snapshot isolation (R1 doesn't see W1's committed changes, R2 does).

### Iteration 5: B+ Tree Scaling (Splitting)

- [x] **Implementation (`src/napr_db_tree.c`)** - Leaf Split Foundation
    - [x] Implement Leaf Page splitting logic (`db_split_leaf`).
    - [x] Implement Branch Page splitting logic (`db_split_branch`).
    - [x] Integrate splitting into `napr_db_put`:
        - [x] Detect full pages and initiate split.
        - [x] Propagate the new divider key and page pointer up to the parent.
        - [x] Handle recursive splitting.
    - [x] Handle Root splitting (increases tree height).
- [x] **Testing (`check/check_db_split.c`)** - Basic Leaf Split Tests
    - [x] Test basic leaf split functionality (split point, distribution).
    - [x] Test key distribution after split (verify ordering).
    - [x] Stress test insertions to force leaf, branch, and root splits (10K keys).
    - [x] Verify data integrity and tree structure after complex splits.

### Iteration 6: Cursors and Deletion

- [x] **Internal Definitions (`src/napr_db_internal.h`)**
    - [x] Define the concrete `struct napr_db_cursor_t` (Must include a stack to track the traversal path).
- [x] **Implementation (`src/napr_db_cursor.c`)**
    - [x] Implement `napr_db_cursor_open` and `napr_db_cursor_close`.
    - [x] Implement `napr_db_cursor_get` positioning operations:
        - [x] `NAPR_DB_FIRST`, `NAPR_DB_LAST`.
        - [x] `NAPR_DB_SET`, `NAPR_DB_SET_RANGE`.
    - [x] Implement `napr_db_cursor_get` iteration operations (Complex):
        - [x] `NAPR_DB_NEXT`, `NAPR_DB_PREV` (Requires ascending/descending the stack due to lack of sibling pointers).
- [x] **Implementation (`src/napr_db.c` and `src/napr_db_tree.c`)**
    - [x] Implement `db_page_delete` helper (slotted page deletion with compaction).
    - [x] Implement `napr_db_del` (simple deletion without rebalancing).
- [x] **Testing (`check/check_db_cursor.c` and `check/check_db_delete.c`)**
    - [x] Test forward and backward iteration across the entire dataset.
    - [x] Test iteration across page boundaries (verifies stack logic).
    - [x] Test basic deletion (insert A,B,C / delete B / verify A,C exist, B gone).
    - [x] Test deleting first and last keys (boundary conditions).
    - [x] Test deleting non-existent key (returns APR_NOTFOUND).
    - [x] Test deletion rejects read-only transactions.

### Iteration 7: MVCC and Free Space Management

- [x] **Implementation (MVCC Components)**
    - [x] Implement the Reader Tracking Table (in shared memory).
    - [x] CRITICAL: Ensure Reader Table slots are CPU cache-line aligned (e.g., 64 bytes) to prevent false sharing (Spec 3.2).
    - [x] Register reader (TXNID snapshot) in `napr_db_txn_begin` (Read-only).
    - [x] Unregister reader in `napr_db_txn_commit/abort` (Read-only).
    - [x] Implement `db_get_oldest_reader_txnid()` helper function.
- [x] **Testing (`check/check_db_mvcc.c`)**
    - [x] Test `DB_ReaderSlot` size and alignment with `_Static_assert`.
    - [x] Test reader registration with concurrent read transactions.
    - [x] Test `db_get_oldest_reader_txnid()` correctness.
    - [x] Test that write transactions do not register in reader table.
- [x] **Implementation (Free Space Management)** - COMPLETE
    - [x] Add `free_db_root` field to `DB_MetaPage` structure.
    - [x] Update `napr_db_txn_t` to track freed pages and capture Free DB root in snapshot.
    - [x] Update CoW logic (`db_page_get_writable`) to record freed pages.
    - [x] Implement `populate_free_db()` function (full implementation with Free DB insertion).
    - [x] Update write transaction commit logic to call `populate_free_db()`.
    - [x] Complete B+ Tree operations that can operate on different roots (main_db_root or free_db_root).
    - [x] Implement actual Free DB insertion in `populate_free_db()`.
    - [x] Fix commit sequencing to ensure Free DB pages are written to disk.
    - [x] Implement MVCC update semantics (distinguish same-transaction duplicates from cross-transaction updates).
    - [x] Refine Page Allocation (Spec 2.4):
        - [x] Implemented `db_reclaim_page_from_free_db()` helper function.
        - [x] Determine the oldest active reader TXNID from the Reader Table.
        - [x] Query Free DB to reuse pages freed by transactions older than the oldest reader.
        - [x] Fall back to extending the file if no reusable pages are found.
        - [x] Updated `db_page_alloc()` to try Free DB reclamation before extending file.
- [x] **Testing (Free Space Management)** - COMPLETE
    - [x] Test `test_freed_pages_tracking`: Verify freed pages array is populated during CoW operations.
    - [x] Test `test_free_db_initialization`: Verify Free DB root changes from 0 after CoW.
    - [x] Test `test_free_db_entry_storage`: Test storing and retrieving freed pages by TXNID.
    - [x] Test `test_free_db_multiple_entries`: Test multiple Free DB entries.
    - [x] All 145 unit tests pass (100% success rate).
    - [x] Test `test_page_reclamation_safety`: Comprehensive test for MVCC-safe page reclamation. Test inserts 500 keys to create multi-page tree, deletes 250 keys to populate Free DB, then verifies:
        - W2 cannot reclaim pages while R1 is active (extends file instead)
        - W3 can reclaim pages after R1 ends (reuses freed pages)
        - Test properly exercises db_page_alloc() by forcing B-tree splits with sufficient insertions
    - [ ] Test Snapshot Isolation: Verify readers maintain a consistent view despite concurrent writes (FUTURE WORK).

## Phase 2: `napr_cache` (Filesystem Hash Cache)

### Iteration 8: `napr_cache` Core Implementation

- [x] **API Definition (`src/napr_cache.h`)**
    - [x] Include headers (APR, `napr_db.h`, `xxhash.h`).
    - [x] Define `napr_cache_entry_t` (mtime, ctime, size, XXH128_hash_t).
    - [x] Add platform dependency warnings (Spec 4.3).
    - [x] Declare opaque handle `napr_cache_t` and the full API.
- [x] **Internal Definitions (`src/napr_cache.c`)**
    - [x] Define the concrete `struct napr_cache_t` (pool, db_env, lock_file_handle, visited_set, visited_mutex).
- [x] **Testing (`check/check_cache_model.c`)**
    - [x] CRITICAL: Verify `sizeof(napr_cache_entry_t)` is exactly 40 bytes using `_Static_assert` (Spec 4.2).
- [x] **Implementation (`src/napr_cache.c`)** - Partial (Open/Close complete)
    - [x] Implement `napr_cache_open`:
        - [x] Process Exclusivity Lock: Create/Open lock file (e.g., append ".lock"). Acquire `apr_file_lock(..., APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK)` (Spec 3.1).
        - [x] Initialize `napr_db_env`.
        - [x] Set `napr_db` mapsize to 10 Gigabytes (Spec 7.2.1).
        - [x] Open `napr_db` using the `NAPR_DB_INTRAPROCESS_LOCK` flag (Spec 3.2).
        - [x] Initialize Mark-and-Sweep structures (mutex, hash table).
    - [x] Implement `napr_cache_close` (Close DB, release and close lock file).
    - [ ] Implement Transaction Wrappers (`begin_read/write`, `end_read`, `commit/abort_write`).
    - [ ] Implement `napr_cache_upsert_in_txn`.
    - [ ] Implement `napr_cache_lookup_in_txn`:
        - [ ] CRITICAL: Validate that the retrieved data size is exactly `sizeof(napr_cache_entry_t)` before returning the zero-copy pointer.
- [x] **Testing (`check/check_cache_init.c` and `check_cache_access.c`)** - Partial (Init tests complete)
    - [x] Test Open/Close Lifecycle.
    - [x] Test Process Exclusivity: Verify a second instance cannot open the cache if locked.
    - [ ] Test CRUD operations (Upsert, Lookup, Verify data integrity).

### Iteration 9: Mark-and-Sweep Strategy

- [ ] **Implementation (`src/napr_cache.c`)**
    - [ ] Implement `napr_cache_mark_visited` (Spec 5.1):
        - [ ] Acquire `visited_mutex` (Thread-safe).
        - [ ] CRITICAL Memory Management: Duplicate the path string into the main cache pool using `apr_pstrdup(cache->pool, ...)` (Spec 7.2.2).
        - [ ] Insert the duplicated path into `visited_set`.
        - [ ] Release `visited_mutex`.
    - [ ] Implement `napr_cache_sweep` (Spec 5.2):
        - [ ] Begin a single write transaction.
        - [ ] Iterate the entire `napr_db` using a cursor.
        - [ ] Check if the current key exists in `visited_set` (`apr_hash_get`).
        - [ ] If NOT found, delete the entry from the DB.
        - [ ] Commit the transaction.
        - [ ] Clear the `visited_set` (`apr_hash_clear`).
- [ ] **Testing (`check/check_cache_mark_sweep.c`)**
    - [ ] Test Memory Management: Mark a path from a short-lived pool, destroy the pool, verify the internal set still holds a valid copy.
    - [ ] Test Concurrency: Stress test concurrent calls to `mark_visited`.
    - [ ] Test Sweep Logic Integration: Populate (A, B, C). Mark (A, C). Sweep. Verify (A, C) remain and (B) is removed.
```
