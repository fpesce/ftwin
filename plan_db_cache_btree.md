# Implementation Guide: `napr_db` and `napr_cache` for `ftwin`

This document outlines the specification and iterative implementation plan for the `napr_db` storage engine and the `napr_cache` library within the `ftwin` repository.

It is designed to guide development using a Test-Driven Development (TDD) approach, broken down into specific, sequential prompts.

---

## ‼️ CRITICAL INSTRUCTIONS FOR AGENT ‼️

You must strictly adhere to the iterative plan outlined below.

**RULE #1: ONE PROMPT AT A TIME.**
Read this document to understand the overall context and goals. However, when responding to a specific prompt, you **must restrict your implementation strictly to the tasks defined in that prompt.**

**RULE #2: DO NOT IMPLEMENT FUTURE STEPS.**
Do not anticipate features, optimizations, or write code related to future iterations, even if the full plan is visible here.

**RULE #3: ADHERE TO TDD AND APR.**
The implementation must follow the sequence defined, using C and Apache Portable Runtime (APR) best practices, and adhering to the TDD approach described in the prompts.

---

## 1. Project Overview and Goals

The objective is to build two interconnected C libraries using APR:

1.  **`napr_db` (Storage Engine):** A high-performance, embedded, transactional key-value store based on the LMDB archetype. It is optimized for read-intensive workloads, providing ACID compliance through an efficient, simplified architecture.
2.  **`napr_cache` (Filesystem Hash Cache):** A persistent cache built on `napr_db`. It is designed to optimize duplicate file finding (`ftwin`) by storing filesystem metadata and computed file hashes (XXH128), avoiding redundant computations across runs.

## 2. Core Architectural Principles

Understanding these principles is essential for correctly implementing the prompts.

### 2.1. `napr_db` (LMDB Archetype)

*   **Memory-Mapped I/O & Zero-Copy:** The entire database file is mapped into the virtual address space. The API returns direct pointers to this map, eliminating buffer copying for reads (Zero-Copy).
*   **OS Delegation:** Caching is delegated to the OS page cache; `napr_db` implements no internal buffer pool.
*   **B+ Tree Indexing:** The core indexing structure. Crucially, leaf nodes **do not** contain sibling pointers. This is required by the CoW model but complicates cursor implementation (range scans).
*   **Copy-on-Write (CoW) Transactions:** Modifications are written to new pages; old pages are never overwritten. A transaction commits by atomically updating one of two Meta Pages. This provides ACID compliance and crash safety without a Write-Ahead Log (WAL).
*   **Multi-Version Concurrency Control (MVCC):** Provides snapshot isolation. Read transactions operate on a static, immutable snapshot, making them completely lock-free.
*   **Single-Writer Model (SWMR):** Write transactions are strictly serialized using a global mutex. This mutex must be configurable (inter-process or intra-process).

### 2.2. `napr_cache`

*   **Layered Architecture:** Built directly on top of `napr_db`.
*   **Data Model:** Keys are absolute file paths. Values are a fixed-size (40-byte) structure (`mtime`, `ctime`, `size`, `XXH128_hash_t`).
*   **Zero-Copy Constraint:** The 40-byte structure is stored directly in the memory map without serialization. **The cache file is platform-dependent.**
*   **Process Exclusivity:** Only one process can access the cache at a time, enforced by an exclusive file lock (`apr_file_lock`).
*   **Optimized Threading:** Designed for single-process, multi-threaded use. It requires `napr_db` to be initialized using the faster intra-process (`apr_thread_mutex_t`) writer lock optimization.
*   **Mark-and-Sweep Consistency:** A strategy to remove stale entries.
    *   **Mark Phase:** Concurrent threads record visited paths in a thread-safe, in-memory APR hash table (`visited_set`). Paths must be duplicated into a long-lived pool.
    *   **Sweep Phase:** A single transaction iterates the database, deleting entries not found in the `visited_set`.

## 3. Iterative Development Plan (Roadmap)

The implementation follows this sequential roadmap.

### Phase 1: `napr_db` (Storage Engine)

*   **Iteration 1: Environment and On-Disk Structure:** Define API, structures (MetaPage, PageHeader), constants, and environment lifecycle (creation, mapping, meta page selection).
*   **Iteration 2: Transaction Framework and Synchronization:** Implement transaction lifecycle (begin/commit/abort) and SWMR model with configurable locking (intra-process vs. inter-process).
*   **Iteration 3: B+ Tree Read Path (Search):** Implement `db_get` with zero-copy semantics.
*   **Iteration 4: B+ Tree Write Path (CoW and Basic Insertion):** Implement `db_put` (without splitting) and the full `db_txn_commit` logic (CoW mechanism, writeback, atomic meta page update).
*   **Iteration 5: B+ Tree Scaling (Splitting):** Implement leaf, branch, and root splitting.
*   **Iteration 6: Cursors and Deletion:** Implement Cursor handle (using a stack traversal), cursor operations, and `db_del`.
*   **Iteration 7: MVCC and Free Space Management:** Implement the Reader Tracking Table and the "Free DB" (B+ Tree for freed pages by TXN ID) for safe page reclamation.

### Phase 2: `napr_cache` (Filesystem Hash Cache)

*   **Iteration 8: `napr_cache` Core Implementation:** Define 40-byte data model and API. Implement `napr_cache_open` (Process exclusivity lock, initialize `napr_db` with 10GB mapsize and intra-process locking). Implement core operations (upsert, lookup).
*   **Iteration 9: Mark-and-Sweep Strategy:** Implement thread-safe `napr_cache_mark_visited` (with correct memory management/duplication) and serialized `napr_cache_sweep`.

## 4. TDD Implementation Prompts

Follow these prompts sequentially. **Remember Rule #1: Implement only the current prompt.**

### Phase 1: `napr_db` Prompts

#### Prompt 1.1: `napr_db` API, Data Structures, and Constants

**Objective:** Define the public API, core data structures, and on-disk format for `napr_db` (LMDB Archetype).

**Context:** We are implementing `napr_db` in C using APR, following strict TDD. The format is 64-bit little-endian. Page size is 4096 bytes.

**Task 1: Create `src/napr_db.h`.**
1. Include APR headers (apr_pools.h, apr_errno.h, apr_file_io.h).
2. Define `napr_db_val_t` (size and data pointer).
3. Declare opaque handles: `napr_db_env_t`, `napr_db_txn_t`, `napr_db_cursor_t`.
4. Define flags: `NAPR_DB_RDONLY`, `NAPR_DB_CREATE`. Crucially, also define `NAPR_DB_INTRAPROCESS_LOCK` (required later for `napr_cache` optimization).
5. Define cursor operations (e.g., `NAPR_DB_FIRST`, `NAPR_DB_NEXT`).
6. Declare function prototypes.

**Task 2: Create `src/napr_db_internal.h`.**
1. Define types: `pgno_t` (uint64_t), `txnid_t` (uint64_t).
2. Define constants: `PAGE_SIZE` (4096), `DB_MAGIC` (0xDECAFBAD), `DB_VERSION` (1).
3. Define Page Flags (P_BRANCH, P_LEAF, P_OVERFLOW, P_FREE).
4. Define the on-disk structures: `DB_PageHeader`, `DB_MetaPage`, `DB_BranchNode`, `DB_LeafNode`.

**Task 3: Create `check/check_db_layout.c`.**
1. Write tests using `_Static_assert` and `offsetof()` to verify the `sizeof()` and layout of all on-disk structures match the specification exactly. This is critical for zero-copy access.

#### Prompt 1.2: Environment Lifecycle (Initialization and Mapping)

**Objective:** Implement the environment lifecycle (`create`, `set_mapsize`, `open`, `close`), focusing on file initialization, memory mapping, and meta page selection.

**Prerequisite:** Prompt 1.1 complete.

**Task 1: Update `src/napr_db_internal.h`.**
1. Define the concrete `struct napr_db_env_t`. Include: pool, mapsize, flags, file handle (`apr_file_t*`), MMAP (`apr_mmap_t*`, `map_addr`), pointers to Meta Page 0, 1, and `live_meta`.

**Task 2: Create `check/check_db_env.c`.**
1. Test `create`/`set_mapsize`/`close`.
2. Test `open` (New DB): Use `NAPR_DB_CREATE`. Verify file creation. Verify Meta Pages 0 and 1 are initialized correctly (Magic, TXNIDs 0/1, last_pgno=1).
3. Test `open` (Existing DB): Re-open the DB. Verify MMAP is established. Verify the correct meta page (highest TXNID) is selected.

**Task 3: Implement functions in `src/napr_db.c`.**
1. Implement `napr_db_env_create`, `napr_db_env_set_mapsize`.
2. Implement `napr_db_env_open`:
   - Open file.
   - If new: Initialize and write Meta Pages 0 and 1. Ensure durability (`apr_file_sync`).
   - Memory Mapping: Use `apr_mmap_create`.
   - Loading: Validate Magic/Version. Select highest valid TXNID as `live_meta`.
3. Implement `napr_db_env_close`: Delete mmap, close file handle.

#### Prompt 2.1: Synchronization and Transaction Lifecycle

**Objective:** Implement the transaction lifecycle and the SWMR synchronization model, ensuring support for configurable locking (Intra-process vs Inter-process).

**Prerequisite:** Prompt 1.2 complete.

**Task 1: Update `src/napr_db_internal.h`.**
1. Update `napr_db_env_t` to hold both potential mutex types:
   - `apr_thread_mutex_t *writer_thread_mutex;`
   - `apr_proc_mutex_t *writer_proc_mutex;`
2. Define the concrete `struct napr_db_txn_t` (env, pool, txnid, snapshot root pgno, flags).

**Task 2: Update `check/check_db_env.c`.**
1. Test `open` with `NAPR_DB_INTRAPROCESS_LOCK`: Verify `writer_thread_mutex` is initialized.
2. Test `open` default: Verify `writer_proc_mutex` is initialized.

**Task 3: Create `check/check_db_txn.c`.**
1. Test Read/Write Txn lifecycle.
2. Test SWMR (Concurrency): Start Write Txn 1. In a second thread/process, attempt Write Txn 2. Verify it blocks until Txn 1 commits/aborts.

**Task 4: Update `src/napr_db.c`.**
1. In `napr_db_env_open`: Initialize the appropriate mutex based on the `NAPR_DB_INTRAPROCESS_LOCK` flag.
2. Implement internal helpers `db_writer_lock(env)` and `db_writer_unlock(env)` that abstract the mutex type.
3. Implement `napr_db_txn_begin`:
   - Allocate handle.
   - If Write: Call `db_writer_lock(env)`.
   - Capture snapshot from `env->live_meta`.
   - If Write: Increment TXNID.
4. Implement `napr_db_txn_abort`/`napr_db_txn_commit` (Skeletal):
   - If Write: Call `db_writer_unlock(env)`.

**(Further prompts are required to complete Iterations 3 through 7, covering the B+ Tree implementation, CoW, Splitting, Cursors, and MVCC/FreeDB.)**

---

### Phase 2: `napr_cache` Prompts

*These prompts assume a functional, linked `napr_db` implementation.*

#### Prompt 8.1: `napr_cache` Data Model, API, and Structure

**Objective:** Define the API, data model, and internal structure for the `napr_cache` library.

**Context:** We are starting `napr_cache`, built on `napr_db`. It uses APR and `libxxhash`.

**Task 1: Create `src/napr_cache.h`.**
1. Include headers (APR types, `napr_db.h`, `xxhash.h`).
2. Define `napr_cache_entry_t`: mtime, ctime (apr_time_t), size (apr_off_t), hash (XXH128_hash_t).
3. Add comments warning about platform dependency.
4. Declare opaque handle `napr_cache_t` and the full API.

**Task 2: Create `src/napr_cache.c`.**
1. Include necessary headers (`apr_file_lock.h`, `apr_strings.h`, `apr_thread_mutex.h`, `apr_hash.h`).
2. Define the concrete `struct napr_cache_t`: pool, db_env, lock_file_handle, visited_set, visited_mutex.
3. Provide stubs for all API functions.

**Task 3: Create `check/check_cache_model.c`.**
1. CRITICAL Test: Verify that `sizeof(napr_cache_entry_t)` is exactly 40 bytes. Use `_Static_assert`. This confirms correct packing required for zero-copy.

#### Prompt 8.2: Initialization, Process Lock, and DB Integration

**Objective:** Implement `napr_cache_open` and `napr_cache_close`, focusing on the process exclusivity lock and `napr_db` initialization with specific configurations.

**Prerequisite:** Prompt 8.1 complete.

**Task 1: Create `check/check_cache_init.c`.**
1. Test Open/Close Lifecycle.
2. Test Process Exclusivity:
   - Open Cache 1.
   - Attempt to open the same file again (Cache 2).
   - Verify the second attempt fails (e.g., `APR_EBUSY`/`APR_EAGAIN`) due to the lock.
   - Close Cache 1. Open Cache 2. Verify success.

**Task 2: Implement `napr_cache_open` and `napr_cache_close` in `src/napr_cache.c`.**
1. `napr_cache_open`:
   - Allocate handle.
   - Process Locking (Must happen first): Determine lock file path (append ".lock"). Open file. Acquire exclusive, non-blocking lock: `apr_file_lock(..., APR_FLOCK_EXCLUSIVE | APR_FLOCK_NONBLOCK)`. Handle failure.
   - Initialize DB Environment:
     - `napr_db_env_create`.
     - `napr_db_env_set_mapsize` with 10 Gigabytes.
     - `napr_db_env_open`. CRITICAL: Use the `NAPR_DB_INTRAPROCESS_LOCK` flag for required optimization.
   - Initialize Mark-and-Sweep: Create `visited_mutex` and `visited_set` (`apr_hash_make`).
   - Error Handling: Ensure lock is released if subsequent initialization fails.
2. `napr_cache_close`:
   - Call `napr_db_env_close`.
   - Release lock (`apr_file_unlock`) and close handle.

#### Prompt 8.3: CRUD Operations (Upsert and Lookup)

**Objective:** Implement the data access functions (Lookup and Upsert) by wrapping `napr_db` transactions and ensuring zero-copy safety.

**Prerequisite:** Prompt 8.2 complete.

**Task 1: Create `check/check_cache_access.c`.**
1. Test CRUD Lifecycle:
   - Begin Write Txn, Upsert entry A, Commit.
   - Begin Read Txn, Lookup A.
   - Verify `APR_SUCCESS` and that the returned `napr_cache_entry_t*` data is correct (confirms zero-copy serialization).
   - End Read Txn.
2. Test Lookup Miss: Verify `APR_NOTFOUND`.

**Task 2: Implement functions in `src/napr_cache.c`.**
1. Implement Transaction Wrappers (`begin_read/write`, `end_read`, `commit/abort_write`) around `napr_db_txn_*`.
2. Implement `napr_cache_upsert_in_txn`:
   - Prepare Key `napr_db_val_t` (path, strlen(path)).
   - Prepare Value `napr_db_val_t` (entry, sizeof(napr_cache_entry_t)).
   - Call `napr_db_put`.
3. Implement `napr_cache_lookup_in_txn`:
   - Call `napr_db_get`.
   - If successful: CRITICAL VALIDATION: Verify `data_val.size == sizeof(napr_cache_entry_t)`. If not, return corruption error (e.g., APR_EGENERAL).
   - Set the output `*entry_ptr` to `data_val.data` (zero-copy).

#### Prompt 9.1: Mark Phase (Thread Safety and Memory Management)

**Objective:** Implement the thread-safe `napr_cache_mark_visited` function, ensuring correct memory management.

**Prerequisite:** Iteration 8 complete.

**Task 1: Create `check/check_cache_mark_sweep.c`.**
1. Test Memory Management (CRITICAL):
   - Create a short-lived scratch pool. Allocate a path string from it.
   - Mark it visited.
   - Destroy the scratch pool (invalidating the original string).
   - Verify that the internal `visited_set` still contains a valid, accessible copy of the string. This proves the path was correctly duplicated into the main pool.
2. Test Concurrency: Spawn multiple APR threads and concurrently call `mark_visited` to stress the mutex and verify all entries are recorded without races.

**Task 2: Implement `napr_cache_mark_visited` in `src/napr_cache.c`.**
1. Acquire `visited_mutex` (`apr_thread_mutex_lock`).
2. CRITICAL: Duplicate the path into the cache's main pool: `apr_pstrdup(cache->pool, path)`. Handle allocation failure (APR_ENOMEM).
3. Insert the duplicated path into `visited_set` (`apr_hash_set`).
4. Release mutex (`apr_thread_mutex_unlock`). Ensure release occurs on all paths (success and failure).

#### Prompt 9.2: Sweep Phase

**Objective:** Implement `napr_cache_sweep` to remove stale entries in a single transaction.

**Prerequisite:** Prompt 9.1 complete.

**Task 1: Update `check/check_cache_mark_sweep.c`.**
1. Test Sweep Logic Integration:
   - Populate cache: A, B, C, D.
   - Mark subset: A, C.
   - Call `napr_cache_sweep()`.
   - Verify Results (via read transactions): A, C exist; B, D are `APR_NOTFOUND`.
   - Verify `visited_set` is empty after the sweep.

**Task 2: Implement `napr_cache_sweep` in `src/napr_cache.c`.**
1. Create a local pool for the transaction/cursor resources.
2. Begin a single write transaction.
3. Open a `napr_db_cursor_t`.
4. Iterate the entire database (e.g., DB_NEXT loop).
5. For each entry: Check if the key exists in `visited_set` using `apr_hash_get(..., key.data, key.size)`.
6. If NOT found: Delete the entry (`napr_db_del(txn, &key, NULL)`).
7. Close the cursor.
8. Commit the transaction.
9. Error Handling: Abort the transaction if errors occur during iteration, deletion, or commit.
10. Finalization: Clear the hash table (`apr_hash_clear(cache->visited_set)`).
11. Destroy the local pool.

