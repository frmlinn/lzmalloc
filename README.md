## New roadmap for lzmalloc 0.2.1
The primary objective is to outperform industry leaders `mimalloc` (14.14 ns) and `jemalloc` (14.88 ns). By transitioning from a safe, core-local allocator to a world-class memory engine, we aim to reduce Slab allocation latency from **21.81 ns** to **< 12 ns** while maintaining the preemption-safe architecture of v0.2.0.

---

## Phase 1: Micro-Latency Optimization (The Fast-Path)
*Goal: Achieve cycle-level efficiency by removing atomic instructions from the primary allocation path.*

* **Thread-Local Allocation Buffers (TLABs):**
    * **Concept:** Implement a lightweight `__thread` bump-pointer to serve the majority of requests without locking.
    * **Implementation:** In `lz_malloc`, the thread first attempts to satisfy the request from its private TLAB (single-instruction pointer advance).
    * **Impact:** Zero atomic instructions (CAS) on ~99% of requests. The thread only visits the protected `core_heap` once the TLAB is exhausted to "refill" it with a large memory block.
* **Software Prefetching Logic:**
    * **Concept:** Mask RAM latency by pre-loading next-link metadata into L1 cache before it is needed.
    * **Implementation:** Integrate `__builtin_prefetch` on the next available free-list node or TLAB segment immediately after a successful allocation.
    * **Impact:** Eliminates ~40-60 cycles of L1 D-Cache miss on subsequent `malloc()` calls.

---

## Phase 2: Contention & Scalability (The Throughput Engine)
*Goal: Maximize MPSC (Multi-Producer Single-Consumer) performance for cross-thread frees.*

* **Deferred Remote Batching:**
    * **Concept:** Utilize the `lz_batch_t` structure to group remote deallocations.
    * **Implementation:** Consuming threads store remote pointers in a local TLS buffer. Once the buffer reaches `LZ_BATCH_MAX_SIZE` (64 elements), it performs a single atomic CAS into the producer's `remote_free_head`.
    * **Impact:** 64x reduction in MESI protocol ping-pong and memory bus contention.
* **Amortized Mailbox Reaping:**
    * **Concept:** Prevent latency spikes ($P_{99}$) during "mailbox storms."
    * **Implementation:** Modify reaping logic in `core_heap.c`. Instead of draining the entire mailbox, the allocator processes a fixed quota (e.g., 64 items) per `slow-path` allocation.
    * **Impact:** Flattens the latency tail by distributing the cost of remote frees across multiple calls.

---

## Phase 3: Hardware Sympathy & Cache Tuning
*Goal: Optimize memory layout for modern CPU cache associative sets and OS behavior.*

* **L1/L2 Cache Coloring (Set Associativity Offsetting):**
    * **Concept:** Prevent "Cache Set Thrashing" where multiple allocations compete for the same cache line.
    * **Implementation:** Introduce a geometric offset at the start of each Slab's payload. Instead of all Slabs starting at the same relative offset within a Chunk, each is shifted by a variable number of `LZ_CACHE_LINE_SIZE` increments.
    * **Impact:** Increases cache hit rates by distributing hot data across different associative sets.
* **Thermal Hysteresis (Anti-Chattering):**
    * **Concept:** Retain pre-formatted Chunks to avoid excessive VMM/OS overhead.
    * **Implementation:** Modify `slab_destroy` and `span_free_local`. Empty Chunks are only returned to the VMM if the `core_heap` already maintains a sufficient buffer of idle, formatted blocks.
    * **Impact:** Stabilizes performance during rapid allocation/deallocation bursts.
* **SIMD Vectorized Zeroing:**
    * **Concept:** Utilize AVX2/AVX-512 for ultra-fast memory clearing in `lz_calloc`.
    * **Implementation:** Replace standard `memset` with vectorized store instructions for medium-to-large blocks.

---

## Phase 4: Topology & Kernel Abstraction
*Goal: Scale to high-core-count NUMA systems and eliminate syscall latency.*

* **NUMA-Aware VMM Sharding:**
    * **Concept:** Isolate memory pools by physical CPU socket.
    * **Implementation:** Transition from a global `vmm_pool` to `g_numa_pools`. Utilize `lz_get_current_node` (vDSO) to route VMM requests to the local NUMA stack.
    * **Impact:** Guarantees local RAM access, avoiding the massive latency penalty of cross-socket interconnects (QPI/UPI).
* **Asynchronous Janitor Thread:**
    * **Concept:** Offload slow kernel transitions (`madvise`/`munmap`) to a background task.
    * **Implementation:** Empty Chunks are pushed to a global lock-free ring buffer. A dedicated "Janitor" thread periodically wakes up to execute `lz_vmm_purge_all_caches`.
    * **Impact:** Removes the millisecond-level cost of `MADV_DONTNEED` from the application's critical path.

---