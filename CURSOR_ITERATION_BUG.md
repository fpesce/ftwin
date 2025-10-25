# Cursor Iteration Bug - Investigation Summary

## Problem
Cursor NEXT/PREV iteration jumps backwards when crossing page boundaries in a multi-level B+tree. Test failure: iteration goes from key0302 back to key0202.

## Root Cause
The issue is in the branch page split logic (`db_split_branch` in `src/napr_db_tree.c`). When a branch page splits:

Current logic:
- Left page keeps entries [0..split_point-1]
- Entry at split_point is "pushed up" as divider
- Right page gets entries [split_point+1..end]

**Problem**: The entry at split_point contains BOTH a key AND a child pointer. When we "push up" only the key, the child pointer is lost, creating gaps in the tree coverage.

## Debug Evidence
```
CURSOR: Leaving leaf page, last key was: key0302
CURSOR: At branch, current index=2, key=key0202  <- Intermediate branch
CURSOR: At branch, current index=0, key=key0000  <- Root level
CURSOR: Moving to next sibling, index=1, key=key0202 <- Goes BACKWARDS!
```

This shows a 3-level tree where Root[1] points to a subtree starting at key0202, even though Root[0]'s subtree extends to key0302.

## Attempted Fixes
1. ✗ Using right[0].key as divider instead of left[split_point].key - didn't fix
2. ✗ Including split_point in left page ([0..split_point]) - didn't fix
3. ✗ Excluding split_point from both pages ([0..split_point-1] and [split_point+1..end]) - didn't fix
4. ✗ Including split_point in right page ([split_point..end]) - didn't fix
5. ✓ Fixed `upper` pointer bug (recalculate after split) - necessary but not sufficient

## Current Status
All variations of split point distribution have been tried without success. The problem may not be in the split logic itself, but in:
- How the divider key is inserted into the parent during propagation
- Search logic interaction with the tree structure
- Some other aspect of the B+tree semantics

## Next Steps
Need to clarify B+tree branch split semantics:
- Should entry at split_point be included in left page?
- How should the divider key relate to actual tree coverage?
- Review LMDB or other B+tree implementations for reference

## Files Involved
- `src/napr_db_tree.c` - split logic (lines 562-671)
- `src/napr_db.c` - split propagation (lines 1057-1166)
- `src/napr_db_cursor.c` - iteration logic (lines 211-369)
