/**
 * @file chunk.h
 * @brief Definition of the 2MB Superblock (Chunk) with a triple-layer shield.
 */

#ifndef LZ_CHUNK_H
#define LZ_CHUNK_H

#include "common.h"
#include <stdatomic.h>

/* Forward declaration to avoid circular dependencies with tlh.h */
struct lz_tlh_s;

/* ========================================================================= *
 * Security Constants
 * ========================================================================= */

/** @brief "LZMALLOC" in Little Endian Hexadecimal ASCII. */
#define LZ_CHUNK_MAGIC_V2 0x434F4C4C414D5A4CULL 

/** @brief Global seed generated at process startup (Layer 5). */
extern uintptr_t g_lz_global_secret; 

/* ========================================================================= *
 * Chunk Structure (Exactly 64 Bytes / 1 Cache Line)
 * ========================================================================= */

typedef struct lz_chunk_header_s {
    struct lz_chunk_header_s* next; 
    struct lz_tlh_s* owning_tlh;    

    uint32_t node_id;               
    uint32_t is_lsm_region;         

    uint64_t magic;                 
    uint64_t canary;                
    uint32_t checksum;              

    uint8_t _pad[20];
} LZ_CACHE_ALIGNED lz_chunk_header_t;

_Static_assert(sizeof(lz_chunk_header_t) == LZ_CACHE_LINE_SIZE, "Chunk header must be exactly 64 bytes");

/* ========================================================================= *
 * Security Macros
 * ========================================================================= */

/** @brief Calculates a fast checksum (XOR/Shift) for critical metadata. */
static LZ_ALWAYS_INLINE uint32_t lz_calc_checksum(lz_chunk_header_t* c) {
    uintptr_t owner = (uintptr_t)c->owning_tlh;
    uint32_t hash = c->node_id ^ (c->is_lsm_region << 16);
    hash ^= (uint32_t)(owner & 0xFFFFFFFF);
    hash ^= (uint32_t)(owner >> 32);
    return hash;
}

#endif // LZ_CHUNK_H