#include "nodecache.h"
 
#ifdef NODECACHE_STATS
static uint64_t _hits = 0;
static uint64_t _misses = 0;
static uint64_t _inserts = 0;
static uint64_t _attempts = 0;
#define NODECACHE_RECORD_HIT() _hits++
#define NODECACHE_RECORD_MISS() _misses++
#define NODECACHE_RECORD_INSERT() _inserts++
#define NODECACHE_RECORD_ATTEMPT() _attempts++
#else
#define NODECACHE_RECORD_HIT()
#define NODECACHE_RECORD_MISS()
#define NODECACHE_RECORD_INSERT()
#define NODECACHE_RECORD_ATTEMPT()
#endif

static inline uint64_t nodecache_hash(uint64_t a) {
	uint64_t hash = 14695981039346656037LLU;

	hash = (hash ^ (a >> 32));
	hash = (hash ^ a) * NODECACHE_PRIME;
	hash = (hash ^ (a >> 3)) * NODECACHE_PRIME;
	hash = (hash ^ (a >> 17)) * NODECACHE_PRIME;

	return hash;
}

void nodecache_init() {
}

void nodecache_put(BDD ref, bddnode_t *node) {
}

int nodecache_get(BDD ref, bddnode_t *node) {
	return 0;
}

void nodecache_show_stats() {
	#ifdef NODECACHE_STATS
	printf("%i/%i - nodecache attempts:%lu, hits:%lu, misses:%lu, inserts:%lu\n",
		MYTHREAD, THREADS, _attempts, _hits, _misses, _inserts);
	#endif
}
