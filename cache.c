#include "cache.h"

#ifndef ATOMIC_SET
#define ATOMIC_SET(ptr, val) (bupc_atomicU64_set_relaxed((ptr), (val)))
#endif

// lock for inserts and prime for hashing
#define CACHE_LOCK_MASK ((uint64_t)0x4000000000000000)
#define CACHE_PRIME ((uint64_t)1099511628211)

// shared index and data tables
static shared [CACHE_SIZE] cache_index_t cache_index[TOTAL_CACHE_SIZE];
static shared [CACHE_SIZE] cache_data_t cache_data[TOTAL_CACHE_SIZE];

// pointer to local portion od data tables
static uint64_t data_offset;
static void *data_p;

// the handle used to synchronize on cache requests
static upc_handle_t request_h;
static cache_index_t request_data;

// function called when idle..
static void (*idle_callback)(void);

// rotating 64-bit FNV-1a hash
static inline uint64_t cache_hash(uint64_t a, uint64_t b, uint64_t c) {
	uint64_t hash = 14695981039346656037LLU;

	hash = (hash ^ (a >> 32));
	hash = (hash ^ a) * CACHE_PRIME;
	hash = (hash ^ b) * CACHE_PRIME;
	hash = (hash ^ c) * CACHE_PRIME;

	return hash;
}

void cache_init(void *idle_callb) {
	// find pointer to local portion of data table
	data_offset = MYTHREAD * CACHE_SIZE;
	data_p = upc_cast(&cache_data[data_offset]);

	// store callback function pointer
	idle_callback = idle_callb;
}

void cache_request(uint64_t a, uint64_t b, uint64_t c) {
	// remove lock from 'a' (if present)
	a &= ~CACHE_LOCK_MASK;

	// find address of bucket
	const uint64_t h = cache_hash(a, b, c);
	const uint64_t i = h % TOTAL_CACHE_SIZE;

	// fetch index entry from table
	request_h = upc_memget_nb(&request_data, &cache_index[i], sizeof(cache_index_t));
}

void cache_sync() {
	while (!upc_sync_attempt(request_h)) { 
		idle_callback(); 
	}
}

int cache_check(uint64_t a, uint64_t b, uint64_t c, uint64_t *res) {
	// synchronize on the request
	cache_sync();

	if (request_data & CACHE_INDEX_OCC) {
		// remove lock from 'a' (if present)
		a &= ~CACHE_LOCK_MASK;

		// find hash value of cache entry
		const uint64_t h = cache_hash(a & ~CACHE_LOCK_MASK, b, c);

		if ((h & CACHE_INDEX_HASH) == (request_data & CACHE_INDEX_HASH)) {
			// find index of data entry
			const uint64_t i = request_data & CACHE_INDEX_ID;

			// obtain entry from data table
			cache_data_t entry;
			upc_handle_t h = upc_memget_nb(&entry, &cache_data[i], sizeof(cache_data_t));
			while (!upc_sync_attempt(h)) { idle_callback(); }

			// abort if locked
			if (entry.a & CACHE_LOCK_MASK) return 0;

			// abort if key different
			if (entry.a != a || entry.b != b || entry.c != c) return 0;

			*res = entry.res;
			return 1;
		}
	}

	return 0;
}

int cache_get(uint64_t a, uint64_t b, uint64_t c, uint64_t *res) {
	cache_request(a, b, c);
	return cache_check(a, b, c, res);
}

void cache_put(uint64_t a, uint64_t b, uint64_t c, uint64_t res) {
	// remove lock from 'a' (if present)
	a &= ~CACHE_LOCK_MASK;

	// find index of bucket (both of index and data table)
	const uint64_t h = cache_hash(a, b, c);
	const uint64_t i = h % TOTAL_CACHE_SIZE;
	const uint64_t data_i = (h >> 34) % CACHE_SIZE;

	// find address of data entry (in local memory)
	cache_data_t *entry_p = (cache_data_t*)data_p + data_i;

	// build an index entry
	cache_index_t entry = CACHE_INDEX_OCC;
	entry |= h & CACHE_INDEX_HASH;
	entry |= (data_offset + data_i) & CACHE_INDEX_ID;

	// apply the lock
	entry_p->a &= CACHE_LOCK_MASK;

	// write entry to the cache
	ATOMIC_SET(&cache_index[i], entry);

	// write cache entry to data table (and remove lock when done)
	entry_p->res = res;
	entry_p->c = c;
	entry_p->b = b;
	entry_p->a = a;
}
