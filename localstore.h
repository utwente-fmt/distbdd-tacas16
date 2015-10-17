#ifndef LOCALSTORE_H
#define LOCALSTORE_H

#include "htable.h"

#define LOCALSTORE_SIZE 262144
#define LOCALSTORE_MASK 0x000000000003FFFF

uint64_t localstore_find_or_put(bddnode_t *key);
void localstore_retrieve(uint64_t index, bddnode_t* dest);
void localstore_set_data(uint64_t index, bddnode_t* dest);

#endif
