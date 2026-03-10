# lzmalloc v0.1.0

[![CI Build](https://github.com/frmlinn/lzmalloc/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_USERNAME/lzmalloc/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey)](#)

## Technical Summary

`lzmalloc` is a POSIX-compliant, NUMA-aware, lock-free memory allocator implemented in C11. It is engineered for mechanical sympathy, utilizing strict 64-byte cache-line alignment to eliminate cross-core false sharing. The allocator minimizes CPU pipeline stalls through vDSO topology routing and maximizes physical memory reclamation via an active Virtual Memory Manager (VMM) with cache hysteresis and `madvise` deflation.

---

## Architecture Overview

The architecture operates on a triple-hierarchy memory routing model, protected by cryptographic metadata and an O(1) resolution lock-free index.

* **Thread-Local Heap (TLH):** Cache-aligned routing matrix providing O(1) allocation for 88 logarithmic size classes. Employs intrusive, obfuscated free-lists and deferred atomic batching for cross-thread memory reclamation.
* **Slab Engine (<= 32KB):** Geometric object partitioning within 2MB superblocks (Chunks). Utilizes lazy bump-pointer initialization and isolates metadata to the first cache line.
* **Span Engine (32KB - 1MB):** Contiguous 4KB page allocator. Uses a 512-bit binned bitmap within 2MB chunks to eliminate internal fragmentation for medium-sized objects.
* **Virtual Memory Manager (VMM):** Direct OS `mmap` router for large objects (> 1MB) and 2MB extent provisioning. Manages NUMA-isolated Treiber stacks for chunk caching and executes global RSS deflation (`MADV_DONTNEED`/`MADV_FREE`).
* **Radix Tree:** A lock-free 2-level index (13-bit root, 14-bit leaves) enabling O(1) metadata resolution from arbitrary addresses.



---

## Benchmark Comparison Table

| Metric | glibc (ptmalloc) | jemalloc | mimalloc | **lzmalloc V2.1** |
| :--- | :--- | :--- | :--- | :--- |
| **Slab Alloc Latency (ns)** | 80.48 | 14.88 | 14.14 | **21.81*** |
| **Slab Free Latency (ns)** | 12.34 | 13.93 | 10.61 | **11.22** |
| **Throughput (M ops/s)** | 17.01 | 9.39 | 26.80 | **24.18** |
| **Chaos Alloc Time (s)** | 7.35 | 2.46 | 3.24 | **11.88** |
| **Final RSS (After GC)** | 2.7 MB | 1071 MB | 209 MB | **24.7 MB** |

*\*Note: Slab allocation latency typically oscillates between 21ns and 35ns depending on CPU frequency scaling and cache state. Chaos allocation time (Medium Objects) is currently unoptimized.*

---

## Technical Roadmap

### Phase 2: V0.1.1 Core Optimization
* **2.1 Hardware-Accelerated Spans:** Implement bit-leaping via `__builtin_ctzll` to reduce Chaos Alloc Time.
* **2.2 Software TLB:** O(1) metadata resolution in TLH to stabilize allocation latency.
* **2.3 Lock-free VMM:** Transition to Treiber Stacks for NUMA pools to eliminate spinlocks.

### Phase 3: Advanced Systems Engineering
* **3.1 Asynchronous Janitor:** Offload `madvise` and `munmap` to background threads.
* **3.2 rseq Integration:** Implement per-core heaps using Linux Restartable Sequences.
* **3.3 Thread Adoption:** Recycling of orphan heaps from terminated threads.

### Phase 4: Hardware Security & Modern Kernel
* **4.1 Memory Tagging:** ARM MTE and Intel MPK near-zero-overhead UAF protection.
* **4.2 Mesh-style Compaction:** Virtual memory remapping for physical page merging.
* **4.3 Huge Page Awareness:** Dynamic 2MB promotion to reduce TLB pressure.

### Phase 5: Production Hardening
* **5.1 Fuzzing:** Integration with LLVM libFuzzer and ASan.
* **5.2 Specialized Backends:** Support for CXL-attached memory and NVDIMM.

---

## Build and Deployment Instructions

### Local Compilation
The build system is managed via CMake with a simplified Makefile wrapper enforcing LTO and native architecture tuning (`-march=native`).

1.  **Release Build:** `make release` (Outputs to `build/release/liblzmalloc.so`)
2.  **Debug Build:** `make debug` (Outputs to `build/debug/liblzmalloc.so`)
3.  **Test Suite:** `make test`

### Runtime Injection
To inject `lzmalloc` into any dynamically linked POSIX application, utilize the `LD_PRELOAD` environment variable:
```bash
LD_PRELOAD=./build/release/liblzmalloc.so ./your_target_executable
```

### Docker Orchestration
A containerized matrix is provided via `docker-compose.yml` to ensure reproducible testing environments.

1.  **Interactive debugging:** `docker-compose run dev-sandbox`
2.  **Automated tests:** `docker compose up ci-test-suite`
3.  **Benchmarking:** `docker-compose up benchmark-suite`