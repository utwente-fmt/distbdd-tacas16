#ifndef NODECACHE_H
#define NODECACHE_H

#include "atomic.h"
#include "htable.h"

typedef struct nodecache_entry {
	BDD ref; // 8 bytes
	bddnode_t node; // 16 bytes
} nodecache_entry_t; // total of 24 bytes

// About 200 MB per thread (2^23 entries)
#define NODECACHE_SIZE 8388608 

// Bitmap used for locking
#define NODECACHE_LOCK 0x0800000000000000

// Used for hashing
#define NODECACHE_PRIME ((uint64_t)1099511628211)

// Measure cache hits and misses
// #define NODECACHE_STATS 1

void nodecache_init();
void nodecache_put(BDD ref, bddnode_t *node);
int nodecache_get(BDD ref, bddnode_t *node);
void nodecache_show_stats();

#endif
