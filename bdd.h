#ifndef BDD_H
#define BDD_H

#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <getopt.h>
#include "wstealer.h"
#include "cache.h"
#include "htable.h"
#include "localstore.h"
#include "varchain.h"
#include "nodecache.h"

typedef struct bdd_input {
	uint64_t metadata;
	BDDVAR lvl;
	BDD a, b, c;
} bdd_input_t;

#define BDD_INPUT_TYPE_ITE ((uint64_t)0x8000000000000000)
#define BDD_INPUT_TYPE_RELPROD ((uint64_t)0x4000000000000000)
#define BDD_INPUT_TYPE_SATCOUNT ((uint64_t)0x2000000000000000)
#define BDD_INPUT_TYPE_GOPAR ((uint64_t)0x1000000000000000)
#define BDD_INPUT_TYPE_PAR ((uint64_t)0x0800000000000000)
#define BDD_INPUT_TYPE_AND ((uint64_t)0x0400000000000000)
#define BDD_INPUT_TYPE_XOR ((uint64_t)0x0200000000000000)
#define BDD_INPUT_LEVEL ((uint64_t)0x07FFFFFFFFFFFFFF)

#define ITE_INPUT(name, _a, _b, _c, _lvl) \
	bdd_input_t name = { .a = _a, .b = _b, .c = _c, .lvl = _lvl, .metadata = BDD_INPUT_TYPE_ITE };

#define AND_INPUT(name, _a, _b, _lvl) \
	bdd_input_t name = { .a = _a, .b = _b, .lvl = _lvl, .metadata = BDD_INPUT_TYPE_AND };

#define XOR_INPUT(name, _a, _b, _lvl) \
	bdd_input_t name = { .a = _a, .b = _b, .lvl = _lvl, .metadata = BDD_INPUT_TYPE_XOR };

#define RELPROD_INPUT(name, _a, _b, _vars, _lvl)  \
	bdd_input_t name = { .a = (_a), .b = (_b), .lvl = (_lvl), .metadata = (_vars) | BDD_INPUT_TYPE_RELPROD };

#define SATCOUNT_INPUT(name, _bdd, _vars, _lvl)  \
	bdd_input_t name = { .a = (_bdd), .lvl = _lvl, .metadata = (_vars) | BDD_INPUT_TYPE_SATCOUNT };

#define GO_PAR_INPUT(name, _cur, _visited, _from, _len)  \
	bdd_input_t name = { .a = (_cur), .b = (_visited), .c = (_from), .metadata = (_len) | BDD_INPUT_TYPE_GOPAR };

#define PAR_INPUT(name, bdd) \
	bdd_input_t name = { .a = bdd, .metadata = BDD_INPUT_TYPE_PAR };

typedef struct shared_set {
	BDD bdd;
	BDD variables;
} shared_set_t;

typedef struct set {
	BDD bdd;
	BDD variables; // all variables in the set (used by satcount)
	varchain_t *varchain;
	varchain_t **vararray;
	uint64_t varcount;
} *set_t;

struct bdd_ser {
	BDD bdd;
	uint64_t assigned;
	uint64_t fix;
};

typedef struct relation {
	BDD bdd;
	BDD variables; // all variables in the relation (used by relprod)
	varchain_t *varchain;
	varchain_t **vararray;
	uint64_t varcount;
} *rel_t;

set_t states;
rel_t *next; // each partition of the transition relation
int next_count; // number of partitions of the transition relation
char* filename;

//shared [*] uint64_t work_count[THREADS];

#define BDD_DEBUG 0
#define BDD_INTERMEDIATE_COUNT 0
#define BDD_WORKCOUNT 0

#define bdd_complement ((uint64_t)0x8000000000000000)
#define bdd_local ((uint64_t)0x2000000000000000)
#define bdd_false ((BDD)0x0000000000000000)
#define bdd_true (bdd_false|bdd_complement)
#define bdd_true_nc ((BDD)0x000000ffffffffff)
#define bdd_invalid ((BDD)0x7fffffffffffffff)

#define bdd_striplocal(s) ((s)&~bdd_local)
#define bdd_isconst(a) ((bdd_strip_marklocal(a) == bdd_striplocal(bdd_false)) || (bdd_striplocal(a) == bdd_true_nc) || bdd_striplocal(a) == bdd_striplocal(bdd_true))
#define bdd_isnode(a)	((bdd_strip_marklocal(a) != bdd_false) && (bdd_strip_marklocal(a) < bdd_true_nc))
#define bdd_stripmark(s) ((s)&~bdd_complement)
#define bdd_strip_marklocal(s) bdd_stripmark(bdd_striplocal(s))
#define bdd_not(a) (((BDD)(a))^bdd_complement)
#define bdd_hasmark(s) (((s)&bdd_complement)?1:0)

#define bdd_transfermark(from, to) ((to) ^ ((from) & bdd_complement))
#define bdd_set_isempty(set) ((set) == bdd_false)
#define bdd_set_next(set) (bdd_low(set))

#define VALID_INDEX(x) ((x) != bdd_invalid && (bdd_isconst(x) || bdd_stripmark(x) < TOTAL_TABLE_SIZE))
#define ASSERT_INDEX(x) assert(VALID_INDEX(x))

void bdd_init();
void bdd_free();
void bdd_printinfo();

varchain_t* create_vars_chain(BDD vars);

BDDSET bdd_set_fromarray(BDDVAR* arr, size_t length);
BDDSET bdd_set_addall(BDDSET set, BDDSET set_to_add);
BDDVAR bdd_var(BDD bdd);

BDD bdd_makenode(BDDVAR level, BDD low, BDD high);
BDD bdd_makenode_local(BDDVAR level, BDD low, BDD high);
BDD bdd_ithvar(BDDVAR var);
BDD bdd_high(BDD bdd);
BDD bdd_low(BDD bdd);

size_t bdd_nodecount(BDD a);

#endif
