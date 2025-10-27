# Cursor Iteration Bug - Investigation Summary

## Problem
Cursor NEXT/PREV iteration jumps backwards when crossing page boundaries in a multi-level B+tree. Test failure: iteration goes from key0302 back to key0202.

## Root Cause - FOUND!

**Location**: `src/napr_db.c` lines 1048-1054 in `napr_db_put()`

**The Bug**: `handle_root_split()` is called **unconditionally** after every split propagation:

```c
status = propagate_split_up_tree(txn, path, path_len, &current_key, &right_child_pgno);
if (status != APR_SUCCESS) {
    return status;
}

/* If propagation reached the root, we need a new root */
return handle_root_split(txn, path[0], right_child_pgno, &current_key);  // ← ALWAYS called!
```

**What happens**:
1. Leaf page splits
2. Split propagates up, successfully inserts divider into existing parent (which has room)
3. `propagate_split_up_tree` returns APR_SUCCESS
4. We STILL call `handle_root_split()` which creates a brand new 2-entry root
5. The parent we just inserted into is abandoned
6. Result: Every split creates a new root, tree never grows properly, parents always have 2 keys

This explains ALL the symptoms:
- Parents always have exactly 2 keys before every split insertion
- `db_split_branch()` is never called (no branch ever fills up - we keep making new roots)
- Tree structure is corrupted with overlapping key ranges
- Iteration jumps backwards when crossing boundaries

## Debug Evidence
```
CURSOR: Leaving leaf page, last key was: key0302
CURSOR: At branch, current index=2, key=key0202  <- Intermediate branch
CURSOR: At branch, current index=0, key=key0000  <- Root level
CURSOR: Moving to next sibling, index=1, key=key0202 <- Goes BACKWARDS!
```

This shows a 3-level tree where Root[1] points to a subtree starting at key0202, even though Root[0]'s subtree extends to key0302.

## Investigation Path

### Dead Ends (split logic was actually correct):
1. ✗ Using right[0].key as divider - this was actually correct
2. ✗ Including split_point in left/right page - tried all variations, all correct
3. ✗ Search/traversal logic - also correct with "minimum key" semantics

### Breakthrough:
4. ✓ Fixed `upper` pointer bug (recalculate after split) - necessary but not sufficient
   - After split, left page's `upper` wasn't reset, making page appear full
   - This bug exists but wasn't the root cause of iteration failures

5. ✓ **FOUND THE REAL BUG**: Unconditional `handle_root_split()` call
   - Debug showed parent ALWAYS has 2 keys before every split
   - Added fprintf to `db_split_branch()` - NEVER fires!
   - Realized: branches never split because new roots created every time
   - Traced to unconditional root split at end of `napr_db_put()`

## The Fix

Modify `propagate_split_up_tree()` to return a status indicating whether the split reached the root:
- Return special code (e.g., APR_INCOMPLETE) when split absorbed by intermediate parent
- Return APR_SUCCESS only when split reaches root and needs new root creation
- Only call `handle_root_split()` when `propagate_split_up_tree()` returns APR_SUCCESS

## Files Involved
- `src/napr_db.c` - **PRIMARY BUG** split propagation logic (lines 1048-1054, 1057-1116)
- `src/napr_db_tree.c` - split logic (correct, but has `upper` pointer bug at lines 534-543, 678-687)
- `src/napr_db_cursor.c` - iteration logic (correct)
