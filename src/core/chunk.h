/**
 * @file chunk.h
 * @brief Definition of the 2MB Superblock (Chunk) metadata shielding.
 */

#ifndef LZ_CHUNK_H
#define LZ_CHUNK_H

#include "common.h"
#include <stdatomic.h>

#define LZ_CHUNK_TYPE_SLAB   0
#define LZ_CHUNK_TYPE_SPAN   1
#define LZ_CHUNK_TYPE_DIRECT 2

/* Forward declaration to prevent circular dependency with the Thread-Local Heap */
struct lz_tlh_s;

/* ========================================================================= *
 * Security Constants
 * ========================================================================= */

/** @def LZ_CHUNK_MAGIC_V2
 * @brief "LZMALLOC" represented in Little Endian Hexadecimal ASCII. */
#define LZ_CHUNK_MAGIC_V2 0x434F4C4C414D5A4CULL 

/* ========================================================================= *
 * Chunk Structure (Strictly 64 Bytes / 1 Cache Line)
 * ========================================================================= */

/**
 * @struct lz_chunk_header_t
 * @brief Metadata residing at the very beginning of every 2MB/32MB Chunk.
 * Packed and padded to perfectly saturate a 64-byte cache line.
 */
typedef struct lz_chunk_header_s {
    struct lz_chunk_header_s* next; /**< 8 bytes: Intrinsically linked list pointer */
    struct lz_tlh_s* owning_tlh;    /**< 8 bytes: Owner Thread-Local Heap pointer */

    uint64_t magic;                 /**< 8 bytes: Static magic identifier */
    uint64_t canary;                /**< 8 bytes: Randomized security canary */
    
    uint32_t node_id;               /**< 4 bytes: Home NUMA node identifier */
    uint32_t checksum;              /**< 4 bytes: Structural integrity hash */

    uint32_t chunk_type;            /**< 4 bytes: 0=Slab, 1=Span, 2=Direct */
    uint8_t _pad[20];               /**< 24 bytes: Padding for 64-byte alignment */
} LZ_CACHE_ALIGNED lz_chunk_header_t;

/* Hard assertion to prevent future commits from breaking the cache-line mathematics */
_Static_assert(sizeof(lz_chunk_header_t) == 64, "Chunk header MUST be exactly 64 bytes");

/* ========================================================================= *
 * Security Macros
 * ========================================================================= */

/**
 * @brief Computes a fast XOR/Shift checksum to validate critical metadata integrity.
 * @param c Pointer to the chunk header.
 * @return The 32-bit checksum hash.
 */
static LZ_ALWAYS_INLINE uint32_t lz_calc_checksum(lz_chunk_header_t* c) {
    uintptr_t owner = (uintptr_t)c->owning_tlh;
    uint32_t hash = c->node_id;
    hash ^= (uint32_t)(owner & 0xFFFFFFFF);
    hash ^= (uint32_t)(owner >> 32);
    return hash;
}

#endif /* LZ_CHUNK_H */