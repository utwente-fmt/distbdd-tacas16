#ifndef CACHE_H
#define CACHE_H

#include <stdio.h>
#include <upc_relaxed.h>
#include <upc_castable.h>
#include <upc_nb.h>
#include "atomic.h"

// different types of cache operations
#define CACHE_ITE 0
#define CACHE_RELNEXT 1
#define CACHE_AND 2
#define CACHE_XOR 3
#define CACHE_SATCOUNT 4

// bitmaps for the index table
#define CACHE_INDEX_OCC  ((uint64_t)0x8000000000000000) // 1 bit
#define CACHE_INDEX_HASH ((uint64_t)0x7FFFF80000000000) // 20 bits
#define CACHE_INDEX_ID   ((uint64_t)0x000007FFFFFFFFFF) // 43 bits

// index entries: 2^24 * 8 bytes (128 MB per thread)
// data entries: 2^24 * 32 bytes (512 GB per thread)
#define CACHE_SIZE (uint64_t)16777216
#define TOTAL_CACHE_SIZE (uint64_t)(CACHE_SIZE * THREADS)

typedef uint64_t cache_index_t; // 8 bytes

typedef struct cache_entry {
	uint64_t a;
	uint64_t b;
	uint64_t c;
	uint64_t res;
} cache_data_t; // 32 bytes

void cache_sync();
void cache_init(void *idle_callb);
void cache_request(uint64_t a, uint64_t b, uint64_t c);
int cache_check(uint64_t a, uint64_t b, uint64_t c, uint64_t *res);
int cache_get(uint64_t a, uint64_t b, uint64_t c, uint64_t *res);
void cache_put(uint64_t a, uint64_t b, uint64_t c, uint64_t res);

#endif
