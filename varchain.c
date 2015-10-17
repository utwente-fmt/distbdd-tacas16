#include "varchain.h"

static varchain_t** chain_array(varchain_t *current, int index) {
	if (current == NULL) {
		return malloc(sizeof(varchain_t*) * index);
	}

	varchain_t **array = chain_array(current->next, index + 1);
	array[index] = current;

	return array;
}

varchain_t** chain_to_array(varchain_t *current) {
	return chain_array(current, 0);
}

uint64_t chain_count(varchain_t *current) {
	if (current == NULL) return 0;
	return 1 + chain_count(current->next);
}

varchain_t* vars_to_chain(BDDVAR* arr, size_t length) {
	if (length == 0) {
		return NULL;
	}

	varchain_t *cur = (varchain_t*)malloc(sizeof(varchain_t));
	cur->level = *arr;
	cur->next = vars_to_chain(arr + 1, length - 1);

	return cur;
}

varchain_t* interleave_chains(varchain_t* a, varchain_t* b) {
	if (a == NULL) return b;
	a->next = interleave_chains(b, a->next);
	return a;
}

void chain_print(varchain_t *a) {
	if (a == NULL) return;
	printf("level: %lu\n", a->level);
	chain_print(a->next);
}	