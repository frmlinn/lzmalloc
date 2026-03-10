# lzmalloc: High-Performance, Lock-Free & NUMA-Aware Memory Allocator
[![CI Build](https://github.com/frmlinn/lzmalloc/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_USERNAME/lzmalloc/actions/workflows/ci.yml)
![C11 Standard](https://img.shields.io/badge/standard-C11-blue.svg)
![POSIX Compliant](https://img.shields.io/badge/POSIX-compliant-success.svg)
![Platform](https://img.shields.io/badge/platform-Linux%20x86__64%20%7C%20ARM64-lightgrey.svg)

## Technical Summary

`lzmalloc` is a POSIX-compliant, drop-in replacement memory allocator (`malloc`, `free`, `calloc`, `realloc`) engineered for high-concurrency Linux environments. Implemented in strict C11, it is designed with mechanical sympathy at its core, utilizing 64-byte cache-line alignment to mathematically eliminate cross-core false sharing. 

The allocator minimizes CPU pipeline stalls through vDSO topology routing and maximizes physical memory reclamation via an active Virtual Memory Manager (VMM) using cache hysteresis and `madvise` RSS deflation.

### Key Engineering Features
* **NUMA-Aware Routing:** Prevents inter-socket latency penalties via vDSO (`getcpu`) topology detection.
* **Lock-Free Concurrency:** Eliminates global locks using C11 atomics, Treiber stacks, and deferred atomic batching.
* **O(1) Metadata Resolution:** Lock-free 2-level Radix Tree index for instantaneous pointer resolution.
* **Hardware Sympathy:** Strict cache-line alignment (64-byte) for all control structures.
* **Active Security:** Cryptographic pointer obfuscation (Safe Linking) and 64-byte chunk canaries to mitigate Use-After-Free (UAF) vulnerabilities.

---

## Core Architecture: Triple-Hierarchy Memory Engines

The architecture operates on a segregated fit routing model, protected by cryptographic metadata and an O(1) lock-free index.

* **Thread-Local Heap (TLH):** Cache-aligned routing matrix providing O(1) allocation for 88 logarithmic size classes. Employs intrusive, obfuscated free-lists and deferred atomic batching for cross-thread memory reclamation.
* **Slab Engine (<= 32KB):** Geometric object partitioning within 2MB superblocks (Chunks). Utilizes lazy bump-pointer initialization and isolates metadata to the first cache line.
* **Span Engine (32KB - 1MB):** Contiguous 4KB page allocator. Uses a 512-bit binned bitmap within 2MB chunks to eliminate internal fragmentation for medium-sized objects.
* **Virtual Memory Manager (VMM):** Direct OS `mmap` router for large objects (> 1MB) and 2MB extent provisioning. Manages NUMA-isolated Treiber stacks for chunk caching and executes global RSS deflation (`MADV_DONTNEED` / `MADV_FREE`).
* **Radix Tree Index:** A lock-free 2-level index (13-bit root, 14-bit leaves) enabling O(1) metadata resolution from arbitrary virtual addresses.



---

## Benchmark Comparison: Allocator Latency & Throughput

Baseline evaluation against standard industry allocators under high-concurrency workloads.

| Metric | glibc (ptmalloc) | jemalloc | mimalloc | **lzmalloc V2.1** |
| :--- | :--- | :--- | :--- | :--- |
| **Slab Alloc Latency (ns)** | 80.48 | 14.88 | 14.14 | **21.81*** |
| **Slab Free Latency (ns)** | 12.34 | 13.93 | 10.61 | **11.22** |
| **Throughput (M ops/s)** | 17.01 | 9.39 | 26.80 | **24.18** |
| **Chaos Alloc Time (s)** | 7.35 | 2.46 | 3.24 | **11.88** |
| **Final RSS (After GC)** | 2.7 MB | 1071 MB | 209 MB | **24.7 MB** |

*\*Note: Slab allocation latency typically oscillates between 21ns and 35ns depending on CPU frequency scaling and L1/L2 cache state. Chaos allocation time for medium objects is currently unoptimized (See Phase 2.1 Roadmap).*

---

## Technical Roadmap & Future Capabilities

**Phase 2: v0.1.1 Core Optimization**
* 2.1 Hardware-Accelerated Spans: Implement bit-leaping via `__builtin_ctzll` to reduce Chaos Alloc Time.
* 2.2 Software TLB: O(1) metadata resolution in TLH to stabilize allocation latency bounds.
* 2.3 Lock-free VMM: Transition to Treiber Stacks for NUMA pools to eliminate spinlock contention.

**Phase 3: Advanced Systems Engineering**
* 3.1 Asynchronous Janitor: Offload `madvise` and `munmap` syscalls to background threads.
* 3.2 rseq Integration: Implement per-core heaps using Linux Restartable Sequences (`rseq`).
* 3.3 Thread Adoption: Recycling of orphan heaps from terminated zombie threads.

**Phase 4: Hardware Security & Modern Kernel**
* 4.1 Memory Tagging: ARM MTE and Intel MPK integration for near-zero-overhead UAF protection.
* 4.2 Mesh-style Compaction: Virtual memory remapping for physical page merging.
* 4.3 Huge Page Awareness: Dynamic 2MB promotion to reduce Translation Lookaside Buffer (TLB) pressure.

**Phase 5: Production Hardening**
* 5.1 Fuzzing: Integration with LLVM libFuzzer and AddressSanitizer (ASan).
* 5.2 Specialized Backends: Support for CXL-attached memory and NVDIMM hardware.

---

## Build, Deployment, and Injection Instructions

### Local Compilation (CMake & LTO)
The build system is managed via CMake with a simplified Makefile wrapper enforcing Link-Time Optimization (LTO) and native architecture tuning (`-march=native`).

1.  **Release Build:** `make release` (Outputs artifact to `build/release/liblzmalloc.so`)
2.  **Debug Build:** `make debug` (Outputs artifact to `build/debug/liblzmalloc.so`)
3.  **Test Suite:** `make test`

### Runtime Injection via LD_PRELOAD
To inject `lzmalloc` into any dynamically linked POSIX application without recompilation, utilize the `LD_PRELOAD` environment variable:

```bash
LD_PRELOAD=./build/release/liblzmalloc.so ./your_target_executable
```

### Docker Orchestration
A containerized matrix is provided via `docker-compose.yml` to ensure reproducible testing environments.

1.  **Interactive debugging:** `docker-compose run dev-sandbox`
2.  **Automated tests:** `docker compose up ci-test-suite`
3.  **Benchmarking:** `docker-compose up benchmark-suite`
