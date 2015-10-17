#ifndef HTABLE_H
#define HTABLE_H

#include <stdio.h>
#include <upc_castable.h> 
#include <upc_relaxed.h>
#include <upc_nb.h>
#include <bupc_extensions.h>
#include "atomic.h"

// index entries: 2^26 * 8 bytes (512 MB per thread)
// data entries: 2^26 * 16 bytes (1 GB per thread)
#define HTABLE_SIZE (uint64_t)67108864
#define TOTAL_TABLE_SIZE (HTABLE_SIZE * THREADS)

// bitmaps for the index table
#define HTABLE_INDEX_OCC  ((uint64_t)0x8000000000000000) // 1 bit
#define HTABLE_INDEX_HASH ((uint64_t)0x7FFFF80000000000) // 20 bits
#define HTABLE_INDEX_ID   ((uint64_t)0x000007FFFFFFFFFF) // 43 bits

// return values of find-or-put
#define HTABLE_FOUND    ((uint64_t)0x8000000000000000)
#define HTABLE_INSERTED ((uint64_t)0x4000000000000000)
#define HTABLE_INDEX    ((uint64_t)0x3FFFFFFFFFFFFFFF)

// used to determine the owner and the block of an address
#define STORAGE_BLOCK(addr)   ((addr) & 0x0000000003FFFFFF) // 26 bits
#define STORAGE_THREAD(addr) (((addr) & 0xFFFFFFFFFC000000) >> 26) // all other bits
#define STORAGE_ADDR(addr) ((addr) % TOTAL_TABLE_SIZE)

// allow statistical data to be gathered
// #define HTABLE_USE_STATS 1

#ifdef HTABLE_USE_STATS
extern uint64_t _rtrips;
extern uint64_t _csize;

#define ADD_TO_RTRIPS(n) { _rtrips += n; }
#define ADD_TO_CSIZE(n) { _csize += n; }
#else
#define ADD_TO_RTRIPS(n)
#define ADD_TO_CSIZE(n)
#endif

// buckets in the index table are simply unsigned 64-bit integers..
typedef uint64_t index_t;

// entries in the data table are BDD nodes
typedef struct bddnode {
	uint64_t high_lvl_used;
	uint64_t low_data_comp;
} bddnode_t; // 16 bytes

typedef uint64_t BDD;				// low 40 bits used for index, highest bit for complement, rest 0
// BDDSET uses the BDD node hash table. A BDDSET is an ordered BDD.
typedef uint64_t BDDSET;		// encodes a set of variables (e.g. for exists etc.)
// BDDMAP also uses the BDD node hash table. A BDDMAP is *not* an ordered BDD.
typedef uint64_t BDDMAP;		// encodes a function of variable->BDD (e.g. for substitute)
typedef uint32_t BDDVAR;		// low 24 bits only

#define MASK_HIGH 			0xffffffffff000000 // 40 bits
#define MASK_LEVEL 			0x0000000000fffffc // 22 bits
#define MASK_LOCAL_HIGH 		0x0000000000000002 // 1  bit
#define MASK_LOCKED 			0x0000000000000001 // 1  bit
#define MASK_LOW 			0xffffffffff000000 // 40 bits
#define MASK_DATA 			0x0000000000fffffc // 22 bits
#define MASK_LOCAL_LOW 			0x0000000000000002 // 1 bits
#define MASK_COMP 			0x0000000000000001 // 1  bit

#define NODE_HIGH(n) 		((((n)->high_lvl_used) & MASK_HIGH) >> 24)
#define NODE_LEVEL(n) 		((((n)->high_lvl_used) & MASK_LEVEL) >> 2)
#define NODE_CLAIMED(n)		((((n)->high_lvl_used) & MASK_LOCAL) >> 1)
#define NODE_LOCKED(n) 		((((n)->high_lvl_used) & MASK_LOCKED))
#define NODE_LOW(n) 		((((n)->low_data_comp) & MASK_LOW) >> 24)
#define NODE_DATA(n) 		((((n)->low_data_comp) & MASK_DATA) >> 2)
#define NODE_COMP(n) 		((((n)->low_data_comp) & MASK_COMP))

#include "nodecache.h"

void htable_init();
uint64_t htable_find_or_put(bddnode_t *key);
size_t htable_owner(bddnode_t *key);
void htable_get_data(uint64_t index, bddnode_t *node);
upc_handle_t htable_get_data_async(uint64_t index, bddnode_t *dest);
void htable_set_data(uint64_t index, bddnode_t *node);
shared void * htable_data_addr(uint64_t index);
int htable_is_local(uint64_t index);

#endif
