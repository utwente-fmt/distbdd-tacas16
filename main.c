#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <endian.h>
#include "avl.h"
#include "bdd.h"
#include "localstore.h"
#include "nodecache.h"
#include "varchain.h"
#include <sys/time.h>

static size_t ser_done = 0;
static size_t vector_size; // size of vector
static size_t bits_per_integer; // number of bits per integer in the vector

BDDVAR* vector_variables; // maps variable index to BDD variable

AVL(ser_reversed, struct bdd_ser) {
	return left->assigned - right->assigned;
}

static avl_node_t *ser_reversed_set = NULL;

static double wctime() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

BDD serialize_get_reversed(uint64_t value) {
	if (!bdd_isnode(value)) {
		return value;
	}

	struct bdd_ser s, *ss;
	s.assigned = bdd_stripmark(value);
	ss = ser_reversed_search(ser_reversed_set, &s);

	assert(ss != NULL);

	return bdd_transfermark(value, ss->bdd);
}

void serialize_fromfile(FILE *in) {
	size_t count, i;
	assert(fread(&count, sizeof(size_t), 1, in) == 1);

	for (i = 1; i <= count; i++) {
		uint64_t high_level;
		uint64_t low_data_comp;

		assert(fread(&high_level, sizeof(uint64_t), 1, in) == 1);
		assert(fread(&low_data_comp, sizeof(uint64_t), 1, in) == 1);

		BDD node_high = high_level & 0x000000ffffffffff;
		BDD node_low = low_data_comp & 0x000000ffffffffff;
		BDDVAR node_level = (BDDVAR)(high_level >> 40);

		uint64_t node_comp = low_data_comp >> 63;
		uint64_t node_data = (low_data_comp >> 40) & 0x00000000007fffff;

		BDD low = serialize_get_reversed(node_low);
		BDD high = serialize_get_reversed(node_high);

		if (node_comp) {
			high |= bdd_complement;
		}

		struct bdd_ser s;
		s.bdd = bdd_makenode_local(node_level, low, high);
		s.assigned = ++ser_done;

		ser_reversed_insert(&ser_reversed_set, &s);
	}
}

static rel_t rel_load(FILE* f) {
	serialize_fromfile(f);

	size_t bdd;
	size_t vector_size;

	if (fread(&bdd, sizeof(size_t), 1, f) != 1) fprintf(stderr, "Invalid input file!\n");
	if (fread(&vector_size, sizeof(size_t), 1, f) != 1) fprintf(stderr, "Invalid input file!\n");

	BDDVAR *vec_to_bddvar = (BDDVAR*)alloca(sizeof(BDDVAR) * bits_per_integer * vector_size);
	BDDVAR *prime_vec_to_bddvar = (BDDVAR*)alloca(sizeof(BDDVAR) * bits_per_integer * vector_size);

	if (fread(vec_to_bddvar, sizeof(BDDVAR), bits_per_integer * vector_size, f) != bits_per_integer * vector_size)
		fprintf(stderr, "Invalid input file!\n");
	if (fread(prime_vec_to_bddvar, sizeof(BDDVAR), bits_per_integer * vector_size, f) != bits_per_integer * vector_size)
		fprintf(stderr, "Invalid input file!\n");

	rel_t rel = (rel_t)malloc(sizeof(struct relation));
	rel->bdd = serialize_get_reversed(bdd);

	varchain_t* x = vars_to_chain(vec_to_bddvar, bits_per_integer * vector_size);
	varchain_t* x2 = vars_to_chain(prime_vec_to_bddvar, bits_per_integer * vector_size);

	rel->varchain = interleave_chains(x, x2); 
	rel->vararray = chain_to_array(rel->varchain);
	rel->varcount = chain_count(rel->varchain);

	return rel;
}

static void set_load(FILE* f) {
	serialize_fromfile(f); 

	size_t bdd;
	size_t vector_size;

	if (fread(&bdd, sizeof(size_t), 1, f) != 1) fprintf(stderr, "invalid input file!\n");
	if (fread(&vector_size, sizeof(size_t), 1, f) != 1) fprintf(stderr, "invalid input file!\n");

	BDDVAR *vec_to_bddvar = alloca(sizeof(BDDVAR) * bits_per_integer * vector_size);

	if (fread(vec_to_bddvar, sizeof(BDDVAR), bits_per_integer * vector_size, f) != bits_per_integer * vector_size) {
		fprintf(stderr, "Invalid input file!\n");
	}

	states = (set_t)malloc(sizeof(struct set));
	states->bdd = serialize_get_reversed(bdd);

	states->varchain = vars_to_chain(vec_to_bddvar, bits_per_integer * vector_size); 
	states->vararray = chain_to_array(states->varchain);
	states->varcount = chain_count(states->varchain);
}

int read_model() {
	if (filename == NULL) {
		fprintf(stderr, "No model has been given!\n");
		return -1;
	}

	FILE *f = fopen(filename, "r");

	if (f == NULL) {
		fprintf(stderr, "Cannot open file '%s'!\n", filename);
		return -1;
	}

	if (MYTHREAD == 0) {
		printf("Initializing %s!\n", filename);
	}

	if (fread(&vector_size, sizeof(size_t), 1, f) != 1) fprintf(stderr, "Invalid input file!\n");
	if (fread(&bits_per_integer, sizeof(size_t), 1, f) != 1) fprintf(stderr, "Invalid input file!\n");

	if (MYTHREAD == 0) {
		printf("Vector size: %zu\n", vector_size);
		printf("Bits per integer: %zu\n", bits_per_integer);
		printf("Number of BDD variables: %zu\n", vector_size * bits_per_integer);
	}

	vector_variables = (BDDVAR*)malloc(sizeof(BDDVAR) * bits_per_integer * vector_size);

	if (fread(vector_variables, sizeof(BDDVAR), bits_per_integer * vector_size, f) != bits_per_integer * vector_size) {
		fprintf(stderr, "Invalid input file!\n");
	}

	// skip some unnecessary data (mapping of primed vector variables to BDD variables)
	if (fseek(f, bits_per_integer * vector_size * sizeof(BDDVAR), SEEK_CUR) != 0) {
		fprintf(stderr, "Invalid input file!\n");
	}

	if (MYTHREAD == 0) {
		printf("Loading initial states: \n");
	}
	
	set_load(f);

	if (MYTHREAD == 0) {
		printf("Initial states loaded... \n");
	}

	// Read transitions
	if (fread(&next_count, sizeof(int), 1, f) != 1) {
		fprintf(stderr, "Invalid input file!\n");
	}

	if (MYTHREAD == 0) {
		printf("Loading transition relations... \n");
	}

	next = (rel_t*)malloc(sizeof(rel_t) * next_count);

	int i; for (i = 0; i < next_count; i++) {
		next[i] = rel_load(f);
		fflush(stdout);
	}

	if (MYTHREAD == 0) {
		// Report statistics
		printf("%zu integers per state, %zu bits per integer, %d transition groups\n", vector_size, bits_per_integer, next_count);
		printf("BDD nodes:\n");
		printf("Initial states: %zu BDD nodes\n", bdd_nodecount(states->bdd));

		for (i = 0; i < next_count; i++) {
			printf("Transition %d: %zu BDD nodes\n", i, bdd_nodecount(next[i]->bdd));
		}
	}

	return 1;
}

int main(int argc, char *argv[]) {
	// initialize BDD library
	bdd_init();

	// read model file from disk
	filename = argv[optind];
	read_model();

	// perform reachability
	PAR_INPUT(in1, states->bdd);
	BDD result;
	double t = ws_compute(&in1, &result);

	// report computation time
	if (MYTHREAD == 0) {
		printf("PAR Time: %f\n", t);
		bdd_printinfo();
	}

	// print statistics (if enabled)
	nodecache_show_stats();
	ws_statistics();

	// determine size of state space
	SATCOUNT_INPUT(in2, result, 0, 0);
	uint64_t count;
	ws_compute(&in2, &count);

	// print size of state space
	if (MYTHREAD == 0) {
		printf("Final states: %zu states\n", (size_t)count);
		printf("Final states: %zu BDD nodes\n", bdd_nodecount(result));
	}

	upc_barrier;

	return 0;
}
