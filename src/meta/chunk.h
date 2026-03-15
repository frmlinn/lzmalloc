/**
 * @file chunk.h
 * @brief Superblock (Chunk) Header Layout.
 * @details Defines the strict structural layout for the 2MB huge-page chunk metadata. 
 * The header is designed with mechanical sympathy to fit exactly within a 
 * single hardware cache line, separating "hot" and "cold" fields to minimize 
 * cache misses.
 */
#ifndef LZ_META_CHUNK_H
#define LZ_META_CHUNK_H

#include <stdint.h>
#include "compiler.h"
#include "lz_config.h"

/** @brief Little-endian ASCII for "LZMALLOC" used for structural validation. */
#define LZ_CHUNK_MAGIC_V2 0x434F4C4C414D5A4CULL

/** @brief Memory engine identifiers. */
#define LZ_CHUNK_TYPE_SLAB   0 /**< Small object engine (<= 32KB). */
#define LZ_CHUNK_TYPE_SPAN   1 /**< Medium object engine (<= 1MB). */
#define LZ_CHUNK_TYPE_DIRECT 2 /**< Large object engine (> 1MB). */

/**
 * @struct lz_chunk_header_s
 * @brief 2MB Superblock Header.
 * @details Resides at offset 0x0 of every hyper-aligned 2MB memory block.
 * Occupies exactly one cache line (64 bytes) to ensure O(1) metadata access 
 * without triggering additional DRAM fetches.
 */
typedef struct lz_chunk_header_s {
    /* --- HOT DATA: Offset 0x0 (Frequently accessed during alloc/free) --- */
    
    /** @brief Magic identifier for integrity checks. */
    uint64_t magic;
    
    /** @brief Physical core ID that owns this chunk. Used for RSEQ affinity checks. */
    uint32_t core_id;
    
    /** @brief Engine type (Slab, Span, or Direct). */
    uint32_t chunk_type;

    /* --- COLD DATA: Offset 0x10 (Accessed during slow-paths or VMM recycling) --- */
    
    /** @brief Relative index used for the Treiber Stack in Virtual Arena Mode. */
    uint32_t next_id;
    
    /** @brief Padding to maintain 8-byte alignment for the security canary. */
    uint32_t _reserved; 
    
    /** @brief Randomized security canary to detect metadata corruption. */
    uint64_t canary;

    /* --- HARDWARE PADDING: Cache Line Saturation --- */
    
    /** @brief Padding to ensure the struct size matches the L1 cache line exactly. */
    uint8_t _padding[LZ_CACHE_LINE_SIZE - 32];

} LZ_CACHELINE_ALIGNED lz_chunk_t;

/**
 * @brief Topological assertion.
 * Ensures the metadata does not exceed or underflow a single cache line, 
 * preventing false sharing or wasted cache capacity.
 */
_Static_assert(sizeof(lz_chunk_t) == LZ_CACHE_LINE_SIZE, 
    "Topological Violation: lz_chunk_t does not saturate the cache line exactly.");

#endif /* LZ_META_CHUNK_H */