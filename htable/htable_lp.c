#include "htable.h"

// shared index and data arrays
static shared [HTABLE_SIZE] index_t index[THREADS * HTABLE_SIZE];
static shared [HTABLE_SIZE] bddnode_t data[THREADS * HTABLE_SIZE];

// denotes the number of items inserted in the hash table
static long double inserted;

// poiner-related information for the data table
static void *data_p;
static uint64_t data_i;
static uint64_t data_offset;

// function called when idle..
static void (*idle_callback)(void);

static inline uint64_t rotl64(uint64_t x, int8_t r) {
	return ((x << r) | (x >> (64 - r)));
}

static uint64_t rehash16_mul(void *key, const uint64_t seed) {
	const uint64_t prime = 1099511628211;
	const uint64_t *p = (const uint64_t *)key;

	uint64_t hash = seed;
	hash = hash ^ p[0];
	hash = rotl64(hash, 47);
	hash = hash * prime;
	hash = hash ^ p[1];
	hash = rotl64(hash, 31);
	hash = hash * prime;

	return hash ^ (hash >> 32);
}

static uint64_t hash16_mul(void *key) {
	return rehash16_mul(key, 14695981039346656037LLU);
}

static uint64_t hash(void *key) {
	return hash16_mul(key);
}

void htable_init(void *idle_callb) {
	data_offset = MYTHREAD * HTABLE_SIZE;
	data_i = 1;

	// get the local pointer to our portion of the data array
	data_p = upc_cast(&data[data_offset]);
	(char*)data_p += sizeof(bddnode_t);
	assert(data_p != NULL);

	idle_callback = idle_callb;
	inserted = 0;
}

static inline uint64_t calc_chunk_size() {
	// estimate (1 - a) with a the load-factor of the hash table
	double alpha = 1 - (inserted / HTABLE_SIZE);

	// estimate (1 - a)^2
	alpha *= alpha;

	// estimate the theoretical expected chunk size that should contain an empty bucket
	double size = 3.6 * ((1 + alpha) / (2 * alpha));

	// the chunk size is at least 8 and at most 2048
	size = MIN(2048, size);
	size = MAX(8, size);

	return size;
}

static inline void query_chunk(uint64_t h, uint64_t chunk_size, index_t *dest) {
	// calculate the indices of the begin and end entries in the chunk
	const uint64_t index1 = h;
	const uint64_t index2 = index1 + (chunk_size - 1);

	// determine the second 'block' for the first and last buckets of the chunk
	const uint64_t owner1 = STORAGE_THREAD(index1);
	const uint64_t owner2 = STORAGE_THREAD(index2);

	if (owner1 != owner2) {
		const uint64_t size1 = HTABLE_SIZE - STORAGE_BLOCK(index1); 
		const uint64_t size2 = chunk_size - size1;

		upc_handle_t h1 = upc_memget_nb(dest,
			&index[STORAGE_ADDR(index1)], sizeof(index_t) * size1);

		upc_handle_t h2 = upc_memget_nb(&dest[size1],
			&index[STORAGE_ADDR(index1 + size1)], sizeof(index_t) * size2);

		while (!upc_sync_attempt(h1)) { idle_callback(); }
		while (!upc_sync_attempt(h2)) { idle_callback(); }
	}
	else {
		upc_handle_t h = upc_memget_nb(dest, &index[STORAGE_ADDR(index1)], sizeof(index_t) * chunk_size);

		while (!upc_sync_attempt(h)) { idle_callback(); }
	}
}

uint64_t htable_find_or_put(bddnode_t *key) {
	// approximate the chunk size
	const uint64_t chunk_size = calc_chunk_size();
	const uint64_t attempts = MAX(1, 4096 / chunk_size);
	ADD_TO_CSIZE(chunk_size);

	// calculate base hash
	const uint64_t _h = hash(key);

	// copy the given bdd node to the data table
	memcpy(data_p, key, sizeof(bddnode_t));

	// allocate space on the stack for a chunk
	index_t *chunks = alloca(chunk_size * sizeof(index_t));

	// perform a maximum of 10 chunk selection attempts
	int i; for (i = 0; i < attempts; i++) {
		// calculate starting index of bucket
		const uint64_t h = _h + (chunk_size * i);

		// query for the ith chunk
		query_chunk(h, chunk_size, chunks);
		ADD_TO_RTRIPS(1);

		// iterate over the ith chunk
		int j; for (j = 0; j < chunk_size; j++) {

			if (!(chunks[j] & HTABLE_INDEX_OCC)) {
				// find the address of the bucket in the index and data table
				uint64_t data_addr = data_offset + data_i;
				uint64_t index_addr = STORAGE_ADDR(h + j);

				// construct a new bucket..
				uint64_t bucket = HTABLE_INDEX_OCC;
				bucket |= _h & HTABLE_INDEX_HASH;
				bucket |= data_addr & HTABLE_INDEX_ID;

				// try to claim it with CAS
				uint64_t res = CAS64(&index[index_addr], chunks[j], bucket);

				if (res == chunks[j]) {
					// insert successful, update local data pointers
					(char*)data_p += sizeof(bddnode_t); 
					inserted++;
					data_i++;

					// return the address of the new data entry
					return data_addr | HTABLE_INSERTED;
				} 
				else if ((_h & HTABLE_INDEX_HASH) == (res & HTABLE_INDEX_HASH)) {
					// we may have found an existing entry
					uint64_t res_addr = res & HTABLE_INDEX_ID;

					bddnode_t node;

					// find the corresponding node
					if (!nodecache_get(res_addr, &node)) {
						htable_get_data(res_addr, &node);
					}

					// check if the entry has already been added
					if (node.high_lvl_used == key->high_lvl_used) {
						if (node.low_data_comp == key->low_data_comp) {
							return res_addr | HTABLE_FOUND;
						}
					}
				}
			}
			else if ((_h & HTABLE_INDEX_HASH) == (chunks[j] & HTABLE_INDEX_HASH)) {
				// we may have found an existing entry
				uint64_t res_addr = chunks[j] & HTABLE_INDEX_ID;

				bddnode_t node;

				// find the corresponding node
				if (!nodecache_get(res_addr, &node)) {
					htable_get_data(res_addr, &node);
				}

				// check if the entry has already been added
				if (node.high_lvl_used == key->high_lvl_used) {
					if (node.low_data_comp == key->low_data_comp) {
						return res_addr | HTABLE_FOUND;
					}
				}
			}
		}
	}

	printf("%i/%i - error: hash table full, load-factor~%f, chunk-size=%lu, attempts=%lu, inserted=%lf\n",
		MYTHREAD, THREADS, inserted / HTABLE_SIZE, chunk_size, attempts, inserted);
	fflush(stdout);

	upc_global_exit(EXIT_FAILURE);

	return 0;
}

size_t htable_owner(bddnode_t *key) {
	// calculate the hash..
	const uint64_t h = STORAGE_ADDR(hash(key));

	// find the owner..
	return upc_threadof(&index[h]);
}

void htable_get_data(uint64_t index, bddnode_t *node) {
	upc_handle_t h = htable_get_data_async(index, node);
	while (!upc_sync_attempt(h)) { idle_callback(); }
}

upc_handle_t htable_get_data_async(uint64_t index, bddnode_t *dest) {
	return upc_memget_nb(dest, &data[index], sizeof(bddnode_t));
}

void htable_set_data(uint64_t index, bddnode_t *node) {
	upc_memput_nbi(&data[index], node, sizeof(bddnode_t));
}

shared void * htable_data_addr(uint64_t index) {
	return &data[index];
}

int htable_is_local(uint64_t index) {
	const size_t owner = upc_threadof(&data[index]);
	return bupc_thread_distance(MYTHREAD, owner) < BUPC_THREADS_NEAR;
}
