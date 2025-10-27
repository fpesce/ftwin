# Free DB Bus Error - Investigation Summary

## Problem
Bus error (SIGBUS) occurs when running test suite after implementing Free DB functionality. The crash happens in `db_find_leaf_page_with_path_in_tree()` when trying to access page flags during Free DB traversal.

**Test Failure**: DB_Delete test suite crashes with exit status 135 (128 + 7 = SIGBUS)

## Stack Trace
```
Thread 1 "check_ftwin" received signal SIGBUS, Bus error.
0x00005555555aae8c in db_find_leaf_page_with_path_in_tree (txn=0x7ffff42c10a0, root_pgno=3, key=0x7fffffffd020, path_out=0x7fffffffd070, path_len_out=0x7fffffffcfc0, leaf_page_out=0x7fffffffcfd8) at src/napr_db_tree.c:856
856	        if (page->flags & P_LEAF) {
#0  0x00005555555aae8c in db_find_leaf_page_with_path_in_tree (txn=0x7ffff42c10a0, root_pgno=3, key=0x7fffffffd020, path_out=0x7fffffffd070, path_len_out=0x7fffffffcfc0, leaf_page_out=0x7fffffffcfd8) at src/napr_db_tree.c:856
#1  0x00005555555a7979 in populate_free_db (txn=0x7ffff42c10a0, new_free_db_root_out=0x7fffffffd1c8) at src/napr_db.c:879
#2  0x00005555555a8406 in napr_db_txn_commit (txn=0x7ffff42c10a0) at src/napr_db.c:1165
#3  0x000055555559ed5b in delete_first_and_last_keys (env=0x7ffff42ff028) at check/check_db_delete.c:213
#4  0x000055555559e457 in test_delete_boundaries_fn (_i=0) at check/check_db_delete.c:143
```

## Root Cause Analysis

### Issue 1: Invalid Free DB Root Page Number
The `free_db_root_pgno` value (3) is pointing to a page that either:
1. Doesn't exist (beyond file size)
2. Was freed from the main DB tree
3. Contains garbage data from uninitialized meta page field

**Key Observation**: The crash occurs in the DB_Delete test, which was written before Free DB was implemented. These tests don't initialize or use the Free DB, but now every transaction commit tries to populate it.

### Issue 2: Page Accessibility Problem
`db_get_page()` in `db_find_leaf_page_with_path_in_tree()` returns a pointer calculated from:
```c
page = (DB_PageHeader *) ((char *) txn->env->map_addr + (current_pgno * PAGE_SIZE));
```

If `current_pgno` (Free DB root = 3) is beyond the actual database size, this pointer points to unmapped memory, causing SIGBUS when dereferenced.

### Issue 3: Meta Page Field Initialization
The `DB_MetaPage.free_db_root` field was added in this iteration. Old test databases might have:
- Garbage values in this field (if database file existed before)
- Valid but incorrect page numbers from previous test runs

## Debug Evidence

1. **Validation Added** (src/napr_db.c:878-884):
   ```c
   pgno_t max_valid_pgno = (txn->flags & NAPR_DB_RDONLY) ?
                           txn->env->live_meta->last_pgno : txn->new_last_pgno;
   if (txn->free_db_root_pgno > max_valid_pgno) {
       return APR_EINVAL;
   }
   ```
   This validation was added but **still crashes** - indicating the root_pgno passes validation but the page is still inaccessible.

2. **Root Pgno = 3**:
   - Small number suggests it's within the database file
   - But the page at that location may not be a valid Free DB page
   - Could be a main DB page with incompatible structure

3. **Crash Location**:
   - Happens at the first page flag access: `if (page->flags & P_LEAF)`
   - This is the very first operation after `db_get_page()` returns
   - Suggests the returned pointer itself is invalid or misaligned

## Investigation Path

### Attempts Made:
1. ✓ Added validation for `free_db_root_pgno > max_valid_pgno` (src/napr_db.c:878-884)
   - Prevents out-of-bounds page numbers
   - But doesn't catch all cases of invalid Free DB roots

2. ✗ Cleaned test databases with `rm -f /tmp/test_*.db`
   - **Still crashes** - rules out stale test database files
   - The issue occurs with freshly created databases

3. ✗ Used `new_last_pgno` for write transactions in validation
   - More accurate for validating page numbers during commit
   - Still doesn't prevent the crash

## Hypotheses

### Most Likely: Page from Main DB Used as Free DB Root
The Free DB root page number might be:
- A page that was allocated for the main DB tree
- Stored incorrectly in the meta page
- Has main DB leaf/branch structure, not Free DB structure
- When we try to insert into it as a Free DB page, the structure is incompatible

**Why this matters**: The Free DB entries have different structure:
- Key: 8-byte TXNID
- Value: Array of pgno_t values
- Main DB has variable-length keys/values

### Secondary: Memory Mapping Issue
The page might be within the file size but:
- Not yet flushed to disk (dirty page not in dirty_pages hash)
- In a region that's not currently mapped
- Aligned incorrectly causing SIGBUS on access

### Tertiary: Race Condition in Meta Page Update
The `free_db_root` field in the meta page might be:
- Not properly initialized during `init_meta_page()`
- Overwritten during concurrent operations
- Read before being written during first commit

## Potential Fixes

### Option 1: Stricter Page Validation
Before traversing Free DB, validate that the root page:
```c
// Get the page first
DB_PageHeader *root_page = db_get_page(txn, txn->free_db_root_pgno);

// Validate it's accessible and has valid flags
if (!root_page || (root_page->pgno != txn->free_db_root_pgno)) {
    // Invalid page - treat as empty Free DB
    txn->free_db_root_pgno = 0;
}

// Validate flags are sensible (P_LEAF or P_BRANCH, not garbage)
if (!(root_page->flags & (P_LEAF | P_BRANCH))) {
    // Corrupt page - treat as empty Free DB
    txn->free_db_root_pgno = 0;
}
```

### Option 2: Safe Page Access Wrapper
Create a function that safely accesses pages with bounds checking:
```c
apr_status_t db_get_page_safe(napr_db_txn_t *txn, pgno_t pgno, DB_PageHeader **page_out) {
    // Validate page number
    if (pgno > current_max_pgno) return APR_EINVAL;

    // Check if mapped region is accessible
    if (pgno * PAGE_SIZE >= txn->env->map_size) return APR_EINVAL;

    // Get page
    *page_out = db_get_page(txn, pgno);

    // Validate page structure
    if ((*page_out)->pgno != pgno) return APR_EINVAL;

    return APR_SUCCESS;
}
```

### Option 3: Reset Free DB Root on Validation Failure
Instead of returning an error, treat invalid Free DB as empty:
```c
if (txn->free_db_root_pgno > max_valid_pgno) {
    // Invalid Free DB - treat as empty and reinitialize
    txn->free_db_root_pgno = 0;
    *new_free_db_root_out = 0;

    // Now fall through to empty Free DB initialization
}
```

## Files Involved
- `src/napr_db.c` - **PRIMARY**: `populate_free_db()` (lines 794-984), validation at 878-884
- `src/napr_db_tree.c` - `db_find_leaf_page_with_path_in_tree()` (line 856 crash site)
- `src/napr_db.c` - `init_meta_page()` (line 100, Free DB root initialization)
- `src/napr_db.c` - `napr_db_txn_begin()` (lines 538, 543, captures free_db_root_pgno from meta)

## Implementation Attempts and Results

### Attempt 1: Basic Bounds Validation ❌
**Location**: src/napr_db.c:879-887

**Implementation**:
```c
pgno_t max_valid_pgno = (txn->flags & NAPR_DB_RDONLY) ?
                        txn->env->live_meta->last_pgno : txn->new_last_pgno;
if (txn->free_db_root_pgno > max_valid_pgno) {
    txn->free_db_root_pgno = 0;
    goto initialize_empty_free_db;
}
```

**Result**: SIGBUS still occurs. The root_pgno (3) passes validation because it's within bounds, but the page structure is invalid.

### Attempt 2: Goto-Based Recovery ❌
**Location**: src/napr_db.c:823-865, 891-896

**Implementation**:
- Moved label `initialize_empty_free_db` outside the if block
- Added error handling: if `db_find_leaf_page_with_path_in_tree()` fails, goto reinitialize
```c
status = db_find_leaf_page_with_path_in_tree(txn, txn->free_db_root_pgno, &key, path, &path_len, &leaf_page);
if (status != APR_SUCCESS) {
    txn->free_db_root_pgno = 0;
    goto initialize_empty_free_db;
}
```

**Result**: SIGBUS still occurs. The crash happens *inside* the traversal function before it can return an error.

### Attempt 3: Page Number Validation in Traversal ⚠️
**Location**: src/napr_db_tree.c:852-857

**Implementation**:
Added validation before every page access in the tree traversal loop:
```c
pgno_t max_pgno = (txn->flags & NAPR_DB_RDONLY) ?
                  txn->env->live_meta->last_pgno : txn->new_last_pgno;
if (current_pgno > max_pgno) {
    return APR_EINVAL;
}
```

**Result**: SIGBUS still occurs. This suggests the issue is not just page number bounds, but something about the page structure or memory mapping.

## Key Observations

1. **Validation Passes But Crash Occurs**: The Free DB root (pgno=3) is within valid page number range, but accessing it causes SIGBUS.

2. **Crash Location Consistency**: Always crashes at line 856 in `db_find_leaf_page_with_path_in_tree()` when accessing `page->flags`.

3. **Test Context**: Crashes in DB_Delete test (`delete_first_and_last_keys()`), which predates Free DB implementation.

4. **Page Number = 3**: This is suspiciously low and could be:
   - A page from the main DB tree (incompatible structure)
   - A page that hasn't been properly initialized
   - A stale value from uninitialized meta page field

## Root Cause Hypothesis (UPDATED)

The most likely cause is **memory mapping/alignment issue**:

### Theory: Page Is Mapped But Misaligned or Invalid
Even though pgno=3 is within the file bounds:
- The page might be at an address that's not properly aligned for structure access
- The memory region might not be fully committed/faulted in
- The page might be in a dirty state without proper header initialization
- SIGBUS specifically indicates a memory alignment or bus access error (different from SIGSEGV)

### Evidence Supporting This Theory:
1. **SIGBUS vs SIGSEGF**: SIGBUS typically indicates:
   - Misaligned memory access
   - Access to a memory-mapped region that doesn't have backing storage
   - Attempt to access memory beyond the physical limits

2. **Page 3 Characteristics**:
   - Meta page = 0, 1
   - First data page = 2
   - Page 3 would be the second data page
   - Could be allocated but not yet written/flushed

3. **Test Environment**: Tests run on freshly created databases where pages might not be fully initialized.

## Potential Root Cause: Uninitialized New Pages

**New Theory**: When `db_page_alloc()` allocates page 3 for the Free DB, it might not be properly initializing the page in memory before it's written to the meta page as the Free DB root. Then on the next transaction, we try to read this "allocated but uninitialized" page.

### Files to Investigate:
1. **src/napr_db.c** - `db_page_alloc()` implementation
   - Does it initialize the page header?
   - Does it add it to dirty_pages immediately?
   - Is the page structure valid after allocation?

2. **Meta Page Updates** - `commit_meta_page()`
   - Is free_db_root being written before the Free DB pages are flushed?
   - Race condition between meta page update and page writes?

## Next Steps (REVISED)

1. **Add Debug Logging**:
   ```c
   fprintf(stderr, "POPULATE_FREE_DB: root_pgno=%lu, max_valid=%lu\n",
           txn->free_db_root_pgno, max_valid_pgno);
   ```

2. **Check db_page_alloc() Implementation**:
   - Verify it properly initializes allocated pages
   - Ensure pages are added to dirty_pages hash immediately

3. **Verify Page Structure After Allocation**:
   - In `initialize_empty_free_db`, after allocating the page, verify its structure before storing in meta
   - Add assertions for page alignment and initialization

4. **Consider Disabling Free DB Temporarily**:
   - Add a compile-time flag to disable Free DB for debugging
   - Or modify tests to not trigger Free DB population

5. **Use Valgrind/AddressSanitizer**:
   ```bash
   valgrind --track-origins=yes ./check_ftwin
   ```
   This might reveal uninitialized memory or alignment issues.

## Status

✅ **RESOLVED** - The bug has been fixed and all tests pass.

## Solution

The bug was caused by a commit sequencing issue in `napr_db_txn_commit()` (src/napr_db.c):

### Root Cause
The original commit sequence was:
1. `write_dirty_pages_to_disk(txn)` - Writes main DB dirty pages to disk
2. `populate_free_db(txn, &new_free_db_root)` - Creates NEW dirty pages for Free DB
3. `commit_meta_page(txn, new_root_pgno, new_free_db_root)` - Writes meta page with free_db_root

**Problem**: The Free DB pages created in step 2 were never written to disk! The meta page referenced page 3 as the Free DB root, but page 3 didn't exist in the file, causing SIGBUS on the next database open.

### Fix Applied
**Solution 1**: Move `populate_free_db()` BEFORE `write_dirty_pages_to_disk()` (src/napr_db.c:1160-1186)

New commit sequence:
1. `populate_free_db(txn, &new_free_db_root)` - Creates Free DB dirty pages
2. `build_pgno_map(txn, &pgno_map)` - Builds page number mapping
3. `update_dirty_page_pointers(txn, pgno_map, &new_root_pgno)` - Updates pointers in ALL dirty pages (including Free DB)
4. `extend_database_file(txn)` - Extends file to include all new pages
5. `write_dirty_pages_to_disk(txn)` - Writes ALL dirty pages (main DB + Free DB)
6. `commit_meta_page(txn, new_root_pgno, new_free_db_root)` - Commits meta page

This ensures that Free DB pages are included in the single `write_dirty_pages_to_disk()` call.

**Solution 2**: Fix MVCC update semantics (src/napr_db.c:1409-1420)

Added logic to distinguish between:
- **Same-transaction duplicates**: Leaf page already dirty → return `APR_EEXIST`
- **MVCC updates**: Leaf page NOT dirty → allow via CoW

```c
if (status == APR_SUCCESS) {
    pgno_t leaf_pgno = path[path_len - 1];
    DB_PageHeader *check_dirty = apr_hash_get(txn->dirty_pages, &leaf_pgno, sizeof(pgno_t));
    if (check_dirty) {
        return APR_EEXIST;  /* Same transaction duplicate */
    }
    /* Different transaction - will CoW and update */
}
```

### Verification
- Minimal test (`test_free_db_minimal.c`) now passes without SIGBUS
- All 144 unit tests pass (100% success rate)
- File size correctly includes Free DB pages after commit
- Database reopens successfully with populated Free DB

### Files Modified
1. `src/napr_db.c` (lines 1160-1186): Reordered commit sequence
2. `src/napr_db.c` (lines 1409-1420): Added MVCC update logic
