#ifndef VARCHAIN_h
#define VARCHAIN_h

#include "htable.h"

typedef struct varchain varchain_t;

struct varchain {
	uint64_t level;
	varchain_t *next;
};

varchain_t* vars_to_chain(BDDVAR* arr, size_t length);
varchain_t** chain_to_array(varchain_t *current);
uint64_t chain_count(varchain_t *current);
varchain_t* interleave_chains(varchain_t* a, varchain_t* b);
void chain_print(varchain_t *a);

#endif
