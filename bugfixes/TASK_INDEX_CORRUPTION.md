# Task Index Corruption Bug

## Summary
Segmentation fault when running `./ftwin -a -J ~` (home directory scan) with cache integration changes. The crash occurs due to memory corruption where task index field contains string data instead of integer values.

## Symptoms
- **Crash location**: `src/ft_process.c:35` - `ft_file_t *file = fsize->chksum_array[task->index].file;`
- **Corrupted data**: `task->index = 0x6c746436` (ASCII "ltd6")
- **Only affects large directories**: Works on `~/Work` and `src/`, crashes on `~/go` and `~`
- **Exit code**: 139 (SIGSEGV)

## Environment
- **Branch**: `napr_cache_integration_prompt_10`
- **Commit**: `3f85666` (Napr cache integration prompt 9)
- **Master branch**: No crash (confirms regression)

## Reproduction
```bash
rm -rf ~/.cache/ftwin/
./ftwin -a -J ~
# Segmentation fault (core dumped)

# Works fine:
./ftwin -a -J ~/Work  # Small directory - no crash
./ftwin -a -J src/    # Small directory - no crash
```

## Investigation Timeline

### Initial Discovery
- GDB backtrace showed crash at `hashing_worker_callback` in thread pool worker
- `task->index` contained corrupted value: 1815431222 (0x6c746436)
- ASCII interpretation: "ltd6" - matches string fragment from file path

### Cache-Related Testing
1. Reduced mapsize from 10GB → 100MB (didn't fix)
2. Disabled cache lookup in `handle_cache_lookup()` - still crashes
3. Disabled cache updates in `update_cache_with_results()` - still crashes
4. Tested on master branch without cache code - works fine

### Struct Changes Analysis
The cache integration added 28 bytes to `ft_file_t` structure:
```c
// New fields added:
ft_hash_t cached_hash;     // 16 bytes (XXH128_hash_t)
int is_cache_hit;          // 4 bytes
apr_time_t ctime;          // 8 bytes
```

### Memory Layout Impact
The struct size increase appears to shift memory layout such that an existing buffer overflow/corruption now hits task structures instead of padding/unused memory.

### AddressSanitizer Results
```bash
# Built with: ./configure CFLAGS="-fsanitize=address -fno-omit-frame-pointer -g"

# Small directory (src/):
- Works fine, only shows minor PCRE memory leaks

# Large directory (~/go):
- ERROR: AddressSanitizer: SEGV on unknown address
- Multiple threads corrupted simultaneously
- High value address dereference
```

### String Fragment Analysis
Found "ltd6" in codebase:
```
/home/ubuntu/go/pkg/mod/github.com/yuin/goldmark@v1.7.8/util/html5entities.go:
    "ltdot": {Name: "ltdot", CodePoints: []int{8918}, ...}
```

This confirms the corruption involves file path strings overwriting task metadata.

## Technical Details

### Thread Pool Workflow
1. **File collection** (`ft_traverse_path`): Scans directories, creates `ft_file_t` entries, allocates to `conf->pool`
2. **Task creation** (`dispatch_hashing_tasks`): Allocates `hashing_task_t` from `gc_pool`
   ```c
   hashing_task_t *task = apr_palloc(gc_pool, sizeof(hashing_task_t));
   task->fsize = fsize;
   task->index = idx;  // ← Gets corrupted
   ```
3. **Worker threads** (`hashing_worker_callback`): Access `fsize->chksum_array[task->index]`

### Memory Pools
- `conf->pool`: Long-lived allocations (entire program lifetime)
  - File paths (`file->path`)
  - `ft_file_t` structures
  - `ft_fsize_t` structures
  - `chksum_array` allocations

- `gc_pool`: Short-lived allocations (per-phase)
  - `hashing_task_t` structures ← **Corruption occurs here**
  - Transaction pools
  - Worker thread data

### Corruption Pattern
```
Expected:
  task->fsize = 0x7fffee967e18  (valid pointer)
  task->index = 42               (valid array index)

Actual (corrupted):
  task->fsize = 0x7fffee967e18  (still valid!)
  task->index = 0x6c746436      (ASCII "ltd6" - invalid)
```

**Key observation**: Only `task->index` is corrupted, `task->fsize` pointer remains valid. This suggests:
- Not a complete pool corruption
- Partial overwrite of task structure
- Likely 4-byte write at offset +8 in struct

## Hypotheses

### 1. Pool Corruption (Most Likely)
APR pool memory management issue when handling many allocations:
- `gc_pool` gets corrupted after many `apr_palloc()` calls
- Subsequent allocations return overlapping memory
- String data from path handling overwrites task structures

### 2. Race Condition
Multiple threads accessing/modifying same task structures:
- Task structures allocated from shared `gc_pool`
- No synchronization protecting task allocation
- Worker threads might write to each other's tasks

### 3. Buffer Overflow in Path Handling
Path string operations overflow into adjacent memory:
- Cache operations call `apr_pstrdup()` extensively
- `napr_cache_mark_visited()` duplicates every path
- Long paths (Go module names) might trigger overflow

## Code References

### Task Allocation
`src/ft_process.c:365-367`:
```c
hashing_task_t *task = apr_palloc(gc_pool, sizeof(hashing_task_t));
task->fsize = fsize;
task->index = idx;
```

### Crash Site
`src/ft_process.c:33-35`:
```c
hashing_task_t *task = (hashing_task_t *) task_data;
ft_fsize_t *fsize = task->fsize;
ft_file_t *file = fsize->chksum_array[task->index].file;  // ← SEGFAULT
```

### Cache Path Duplication
`src/napr_cache.c:378`:
```c
duplicated_path = apr_pstrdup(cache->pool, path);
```

### File Collection
`src/ft_traverse.c:151-154`:
```c
ft_file_t *file = apr_palloc(conf->pool, sizeof(struct ft_file_t));
file->path = apr_pstrdup(conf->pool, filename);
file->size = finfo->size;
file->mtime = finfo->mtime;
```

## Next Steps

### 1. Enable APR_POOL_DEBUG
Rebuild APR with pool debugging to track allocations:
```bash
# APR debug mode makes each allocation call malloc
# This allows valgrind to track individual allocations
```

### 2. Create Minimal Test Case
Unit test that reproduces corruption with controlled input:
- Loop with 10,000 file insertions
- Use random/long path names
- Monitor task structure integrity

### 3. Investigate Pool Lifecycle
- Verify `gc_pool` creation/destruction timing
- Check for premature pool destruction
- Ensure task structures remain valid during worker execution

### 4. Add Validation Checks
```c
// Before accessing array:
if (task->index >= fsize->nb_files) {
    DEBUG_ERR("CORRUPTION: task->index=%u exceeds nb_files=%u",
              task->index, fsize->nb_files);
    return APR_EGENERAL;
}
```

## Related Files
- `src/ft_process.c` - Task allocation and worker callback
- `src/ft_traverse.c` - File collection and path handling
- `src/napr_cache.c` - Cache path duplication
- `src/ft_types.h` - Structure definitions
- `src/napr_threadpool.c` - Thread pool implementation

## Debugging Attempts

### Minimal Test Case
Created `bugfixes/test_task_corruption.c` to reproduce the issue:
- Allocates 10,000 files with long paths (Go module style)
- Creates 10,000 tasks from gc_pool
- Launches 24 worker threads
- **Result**: No corruption detected

**Conclusion**: The minimal test suggests the issue requires:
- Actual file I/O operations
- Real hashing/processing logic
- Or specific pool management patterns from the full codebase

### APR_POOL_DEBUG Mode
Rebuilt with `CPPFLAGS="-DAPR_POOL_DEBUG"` to enable APR pool debugging:
- Makes each allocation call malloc directly
- Allows valgrind to track individual allocations
- **Result**: Valgrind shows no memory errors before crash (timeout signal)

**Observation**: The corruption may be happening in a way that valgrind/APR_POOL_DEBUG doesn't detect, suggesting:
- Use-after-free from pool destruction timing
- Race condition between pool allocation and thread access
- Or corruption in non-APR memory (e.g., mmap'd database regions)

## Recommended Investigation Path

### 1. Add Runtime Validation
Insert checks before array access:
```c
// src/ft_process.c:33-35
hashing_task_t *task = (hashing_task_t *) task_data;

/* VALIDATION: Check for corruption */
if (task->index >= 10000) {  /* Reasonable upper bound */
    char errbuf[ERR_BUF_SIZE];
    char ascii[5] = {0};
    memcpy(ascii, &task->index, 4);

    fprintf(stderr, "CORRUPTION DETECTED:\n");
    fprintf(stderr, "  task=%p\n", (void*)task);
    fprintf(stderr, "  task->fsize=%p\n", (void*)task->fsize);
    fprintf(stderr, "  task->index=%u (0x%08x)\n", task->index, task->index);
    fprintf(stderr, "  As ASCII: '%c%c%c%c'\n", ascii[0], ascii[1], ascii[2], ascii[3]);

    /* Print nearby memory */
    unsigned char *mem = (unsigned char*)task;
    fprintf(stderr, "  Memory dump (32 bytes):\n    ");
    for (int i = 0; i < 32; i++) {
        fprintf(stderr, "%02x ", mem[i]);
        if ((i+1) % 16 == 0) fprintf(stderr, "\n    ");
    }
    fprintf(stderr, "\n");

    abort();
}

ft_fsize_t *fsize = task->fsize;
ft_file_t *file = fsize->chksum_array[task->index].file;
```

### 2. Check Pool Lifecycle
Verify gc_pool isn't destroyed while threads are accessing it:
```c
// src/ft_process.c: dispatch_hashing_tasks
napr_threadpool_wait(threadpool);  // ← Are we waiting here?

// Then later:
apr_pool_destroy(gc_pool);  // ← Can this happen too early?
```

### 3. Investigate mmap/Database Interaction
Since corruption involves paths and the cache uses mmap'd database:
- Check if database mmap regions overlap with pool memory
- Verify database file size vs. mapsize consistency
- Test without database initialization

## CRITICAL BREAKTHROUGH

### Single-Threaded Test (Rules Out Race Conditions)
Tested with `-j 1` (single worker thread) - **corruption still occurs!**

```bash
./ftwin -a -J -j 1 ~/go
```

**Result**: Corruption detected even in single-threaded mode, definitively ruling out race conditions.

### Corruption Details from Validation
```
Task pointer:     0x7f26f16e21e8
Task->fsize:      0x7f26f16e2218 (valid pointer, 48 bytes offset)
Task->index:      49894708 (0x02f95534) - CORRUPTED

Memory dump:
  18 22 6e f1 26 7f 00 00  <- fsize pointer (VALID)
  34 55 f9 02 aa 32 06 00  <- index (CORRUPTED)
  34 55 f9 02 aa 32 06 00  <- REPEATED PATTERN!
  76 01 00 00 00 00 00 00  <- 374

Fsize structure (corrupted):
  fsize->chksum_array: 0x2f6f672f75746e75
  As ASCII: "/go/utnu" <- PATH STRING!
```

### Root Cause Hypothesis

**Subpool allocation corrupting parent pool!**

The worker callback creates subpools from gc_pool for file I/O:
```c
// src/ft_process.c:89
apr_status_t status = apr_pool_create(&subpool, h_ctx->pool);  // h_ctx->pool is gc_pool
```

Meanwhile, tasks are allocated from the same gc_pool:
```c
// src/ft_process.c:420
hashing_task_t *task = apr_palloc(gc_pool, sizeof(hashing_task_t));
```

**The problem**:
1. Tasks allocated from gc_pool
2. Worker creates subpool from gc_pool
3. Subpool allocates strings for file paths during processing
4. Subpool allocations corrupt parent gc_pool memory
5. Task structures in gc_pool get overwritten with path strings

This explains why:
- Corruption contains path fragments ("/go/utnu")
- Happens even in single-threaded mode (not a race)
- Valgrind doesn't catch it (valid pool operations, just wrong memory layout)
- Only triggers with many files (many subpool allocations)

## THE FIX

### Solution
Changed task allocation from `gc_pool` (short-lived) to `conf->pool` (long-lived):

**File**: `src/ft_process.c:208`

```c
// BEFORE (buggy):
hashing_task_t *task = apr_palloc(gc_pool, sizeof(hashing_task_t));

// AFTER (fixed):
hashing_task_t *task = apr_palloc(conf->pool, sizeof(hashing_task_t));
```

### Why This Works
The issue occurred because:
1. Tasks were allocated from `gc_pool`
2. Worker threads created subpools from the same `gc_pool` for file I/O
3. Subpool allocations (especially path string duplications) corrupted parent `gc_pool` memory
4. Task structures in `gc_pool` got overwritten with path strings like "/go/utnu"

By allocating tasks from `conf->pool` instead:
- Tasks live in long-lived memory separate from temporary allocations
- Subpool operations from `gc_pool` cannot corrupt task structures
- Both allocation patterns can coexist safely

### Verification
```bash
# Test no longer crashes:
./ftwin -a -J ~         # Exit code: 0 ✓

# Performance with cache:
./ftwin -a ~/Work       # First run: 1.637s
./ftwin -a ~/Work       # Second run: 0.974s (40% faster) ✓

# All tests pass:
make check              # 163 tests PASS ✓
```

## Timeline
- **2025-10-28**: Initial bug report and investigation
- **2025-10-28**: Confirmed struct size correlation
- **2025-10-28**: Ruled out direct cache code involvement
- **2025-10-28**: Identified memory corruption pattern
- **2025-10-28**: Created minimal test case (no reproduction)
- **2025-10-28**: Tested with APR_POOL_DEBUG and valgrind
- **2025-10-28**: Added runtime validation - corruption detected
- **2025-10-28**: **CRITICAL**: Tested with `-j 1` - **rules out race conditions**
- **2025-10-28**: **ROOT CAUSE**: Subpool allocations corrupting parent gc_pool
- **2025-10-29**: **FIX APPLIED**: Changed task allocation to conf->pool
- **2025-10-29**: **VERIFIED**: All tests pass, no crashes, cache provides 40% speedup
