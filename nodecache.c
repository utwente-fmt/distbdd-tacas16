#include "nodecache.h"

static shared [NODECACHE_SIZE] nodecache_entry_t nodecache[THREADS * NODECACHE_SIZE];

static uint64_t start = 0;
static uint64_t range = 0;
static size_t lowest = 0;
static size_t count = 0;

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
	lowest = THREADS + 1;

	size_t i; for (i = 0; i < THREADS; i++) {
		if (bupc_thread_distance(MYTHREAD, i) < BUPC_THREADS_NEAR) {
			if (lowest > THREADS) lowest = i;
			count++;
		}
	}

	start = lowest * NODECACHE_SIZE;
	range = count * NODECACHE_SIZE;

	const size_t owner_s = upc_threadof(&nodecache[start]);
	const size_t owner_e = upc_threadof(&nodecache[start + range - 1]);

	assert(bupc_thread_distance(MYTHREAD, owner_s) < BUPC_THREADS_NEAR);
	assert(bupc_thread_distance(MYTHREAD, owner_e) < BUPC_THREADS_NEAR);
}

void nodecache_put(BDD ref, bddnode_t *node) {
	if (htable_is_local(ref)) {
		return;
	}

	const uint64_t addr = nodecache_hash(ref) % range;
	const uint64_t cur = nodecache[start + addr].ref;

	if (CAS64(&nodecache[start + addr].ref, cur, ref | NODECACHE_LOCK) == cur) {
		upc_memput(&nodecache[start + addr].node, node, sizeof(bddnode_t));
		nodecache[start + addr].ref = ref;
		NODECACHE_RECORD_INSERT();
	}
}

int nodecache_get(BDD ref, bddnode_t *node) {
	const uint64_t addr = nodecache_hash(ref) % range;
	nodecache_entry_t entry = nodecache[start + addr];

	NODECACHE_RECORD_ATTEMPT();

	if (entry.ref == ref) {
		*node = entry.node;
		NODECACHE_RECORD_HIT();
		return 1;
	}

	NODECACHE_RECORD_MISS();

	return 0;
}

void nodecache_show_stats() {
	#ifdef NODECACHE_STATS
	printf("%i/%i - nodecache attempts:%lu, hits:%lu, misses:%lu, inserts:%lu, span:%lu\n",
		MYTHREAD, THREADS, _attempts, _hits, _misses, _inserts, count);
	#endif
}
