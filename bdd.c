#include "bdd.h"

#define BDD_ISTRUE(bdd) (bdd_striplocal(bdd) == bdd_striplocal(bdd_true))
#define BDD_ISFALSE(bdd) (bdd_striplocal(bdd) == bdd_striplocal(bdd_false))

static int granularity = 4; // default

static inline void increase_work_counter() {
	//work_count[MYTHREAD]++;
}

static inline BDD bdd_setdata(BDD s, uint32_t data) {
	return (s & 0xf00000ffffffffff) | (((uint64_t)(data & 0x003fffff))<<40);
}

static inline BDD node_lowedge(bddnode_t *node) {
	BDD low = NODE_LOW(node);

	if (node->low_data_comp & MASK_LOCAL_LOW) {
		low |= bdd_local;
	}

	return low;
}

static inline BDD node_highedge(bddnode_t *node) {
	BDD high = NODE_HIGH(node) | (NODE_COMP(node) ? bdd_complement : 0LL);

	if (node->high_lvl_used & MASK_LOCAL_HIGH) {
		high |= bdd_local;
	}

	return high;
}

static inline BDD node_low(BDD bdd, bddnode_t *node) {
	return bdd_transfermark(bdd, node_lowedge(node));
}

static inline BDD node_high(BDD bdd, bddnode_t *node) {
	return bdd_transfermark(bdd, node_highedge(node));
}

static inline BDD bdd_or(BDD a, BDD b) {
	AND_INPUT(in, bdd_not(a), bdd_not(b), 0);
	BDD out;
	ws_call(&in, &out);
	return bdd_not(out);
}

static inline BDD bdd_diff(BDD a, BDD b) {
	AND_INPUT(in, a, bdd_not(b), 0);
	BDD out;
	ws_call(&in, &out);
	return out;
}

BDD bdd_low(BDD bdd) {
	if (bdd_isconst(bdd)) {
		return bdd;
	}

	bddnode_t node;
	upc_handle_t h = NULL;

	if (bdd & bdd_local) localstore_retrieve(bdd_strip_marklocal(bdd), &node);
	else h = htable_get_data_async(bdd_strip_marklocal(bdd), &node);

	if (h != NULL) { 
		while (!upc_sync_attempt(h)) { ws_progress(); }
	}

	return node_low(bdd, &node);
}

BDD bdd_high(BDD bdd) {
	if (bdd_isconst(bdd)) {
		return bdd;
	}

	bddnode_t node;
	upc_handle_t h = NULL;

	if (bdd & bdd_local) localstore_retrieve(bdd_strip_marklocal(bdd), &node);
	else h = htable_get_data_async(bdd_strip_marklocal(bdd), &node);

	if (h != NULL) { 
		while (!upc_sync_attempt(h)) { ws_progress(); }
	}

	return node_high(bdd, &node);
}

BDDVAR bdd_var(BDD bdd) {
	if (BDD_ISFALSE(bdd) || BDD_ISTRUE(bdd)) {
		fprintf(stderr, "ERROR: bdd cannot be constant\n");
		exit(EXIT_FAILURE);
	}

	bddnode_t node;
	upc_handle_t h = NULL;

	if (bdd & bdd_local) localstore_retrieve(bdd_strip_marklocal(bdd), &node);
	else h = htable_get_data_async(bdd_stripmark(bdd), &node);

	if (h != NULL) {
		while (!upc_sync_attempt(h)) { ws_progress(); }
	}

	return NODE_LEVEL(&node);
}

uint64_t bdd_nodecount_do_1(BDD a) {
	if (bdd_isconst(a)) {
		return 0;
	}

	/* Retrieve node  */

	bddnode_t na;

	if (a & bdd_local) localstore_retrieve(bdd_strip_marklocal(a), &na);
	else htable_get_data(bdd_strip_marklocal(a), &na);

	// is received node already marked?
	if (NODE_DATA(&na) & 1) {
		return 0;
	}

	/* Update node */

	na.low_data_comp |= 0x0000000000000004; // apply mark

	if (a & bdd_local) localstore_set_data(bdd_strip_marklocal(a), &na);
	else htable_set_data(bdd_strip_marklocal(a), &na);

	/* Recursively count descendants */

	uint64_t result = 1;

	result += bdd_nodecount_do_1(node_lowedge(&na));
	result += bdd_nodecount_do_1(node_highedge(&na));

	return result;
}

void bdd_nodecount_do_2(BDD a) {
	if (bdd_isconst(a)) {
		return;
	}

	/* Retrieve node  */

	bddnode_t na;
	
	if (a & bdd_local) localstore_retrieve(bdd_strip_marklocal(a), &na);
	else htable_get_data(bdd_strip_marklocal(a), &na);

	// skip if not marked..
	if (!(NODE_DATA(&na) & 1)) {
		return;
	}

	/* Update node */

	na.low_data_comp &= ~0x0000000000000004;
	
	if (a & bdd_local) localstore_set_data(bdd_strip_marklocal(a), &na);
	else htable_set_data(bdd_strip_marklocal(a), &na);

	/* Recursively unmark descendants */

	bdd_nodecount_do_2(node_lowedge(&na));
	bdd_nodecount_do_2(node_highedge(&na));
}

size_t bdd_nodecount(BDD a) {
	uint32_t result = bdd_nodecount_do_1(a);
	bdd_nodecount_do_2(a);
	return result;
}

BDD bdd_makenode(BDDVAR level, BDD low, BDD high) {
	// do not create duplicate nodes
	if (low == high) return low;

	uint64_t node_low = (low & 0x000000ffffffffff) << 24;
	uint64_t node_high = (high & 0x000000ffffffffff) << 24;
	uint64_t node_level = ((uint64_t)(level & 0x003fffff)) << 2;

	bddnode_t node;
	node.high_lvl_used = node_high | node_level;
	node.low_data_comp = node_low;

	// preserve data-locality
	if (high & bdd_local) node.high_lvl_used |= MASK_LOCAL_HIGH;
	if (low & bdd_local) node.low_data_comp |= MASK_LOCAL_LOW;

	int mark;
	if (bdd_hasmark(low)) {
		mark = 1;
		node.low_data_comp |= (bdd_hasmark(high) ? 0 : MASK_COMP);
	} else {
		mark = 0;
		node.low_data_comp |= (bdd_hasmark(high) ? MASK_COMP : 0);
	}

	BDD result = htable_find_or_put(&node) & HTABLE_INDEX;

	return mark ? (result | bdd_complement) : result;
}

BDD bdd_makenode_local(BDDVAR level, BDD low, BDD high) {
	// do not create duplicate nodes
	if (low == high) return low;

	uint64_t node_low = (low & 0x000000ffffffffff) << 24;
	uint64_t node_high = (high & 0x000000ffffffffff) << 24;
	uint64_t node_level = ((uint64_t)(level & 0x003fffff)) << 2;

	bddnode_t node;
	node.high_lvl_used = node_high | node_level;
	node.low_data_comp = node_low;

	// preserve data-locality
	if (high & bdd_local) node.high_lvl_used |= MASK_LOCAL_HIGH;
	if (low & bdd_local) node.low_data_comp |= MASK_LOCAL_LOW;

	int mark;
	if (bdd_hasmark(low)) {
		mark = 1;
		node.low_data_comp |= (bdd_hasmark(high) ? 0 : MASK_COMP);
	} else {
		mark = 0;
		node.low_data_comp |= (bdd_hasmark(high) ? MASK_COMP : 0);
	}

	BDD result = localstore_find_or_put(&node) | bdd_local;

	return mark ? (result | bdd_complement) : result;
}

static inline double bdd_set_count(varchain_t *current) {
	double result = 0;

	while (current != NULL) {
		current = current->next;
		result++;
	}

	return result;
}

void bdd_satcount(BDD bdd, uint64_t vars, BDDVAR prev_lvl, uint64_t *result) {
	// update task counter (for statistical purposes)
	if (BDD_WORKCOUNT) {
		increase_work_counter();
	}

	/* Trivial cases */

	if (BDD_ISFALSE(bdd)) {
		*result = 0;
		return;
	}

	varchain_t *variables = NULL;

	if (vars < states->varcount) {
		variables = states->vararray[vars];
	}

	if (BDD_ISTRUE(bdd)) {
		*result = pow(2, bdd_set_count(variables));
		return;
	}

	/* Count variables */

	double skipped = 0;
	BDDVAR var = bdd_var(bdd);

	while (var != variables->level) {
		skipped++;
		variables = variables->next;
		assert(variables != NULL);
		vars++;
	}

	/* Consult the cache */

	int cachenow = granularity < 2 || prev_lvl == 0 ? 1 : prev_lvl / granularity != var / granularity;

	if (cachenow) {
		uint64_t res;
		if (cache_get(bdd_setdata(bdd, CACHE_SATCOUNT), vars, bdd_false, &res)) {
			*result = res * pow(2, skipped);
			return;
		}
	}

	/* Recursive computation */

	uint64_t high, low, res;

	SATCOUNT_INPUT(in1, bdd_high(bdd), vars + 1, var);
	SATCOUNT_INPUT(in2, bdd_low(bdd), vars + 1, var);

	ws_spawn(&in1);
	ws_call(&in2, &low);
	ws_sync(&high);

	res = low + high;

	/* Cache and return result */

	if (cachenow) {
		cache_put(bdd_setdata(bdd, CACHE_SATCOUNT), vars, bdd_false, res);
	}

	*result = res * pow(2, skipped);
}

void bdd_and(BDD a, BDD b, BDDVAR prev_lvl, BDD *result) {
	// update task counter (for statistical purposes)
	if (BDD_WORKCOUNT) {
		increase_work_counter();
	}

	/* Terminal cases */

	// AND(T, B) = B
	if (BDD_ISTRUE(a)) {
		*result = b;
		return;
	}

	// AND(A, T) = A
	if (BDD_ISTRUE(b)) {
		*result = a;
		return;
	}

	// AND(F, _) = F
	if (BDD_ISFALSE(a)) {
		*result = bdd_false;
		return;
	}

	// AND(_, F) = F
	if (BDD_ISFALSE(b)) {
		*result = bdd_false;
		return;
	}

	/* Improve for caching */

	if (bdd_stripmark(a) > bdd_stripmark(b)) {
		BDD t = b;
		b = a;
		a = t;
	}
	
	/* Retrieve nodes */

	upc_handle_t ha = NULL, hb = NULL;
	bddnode_t na, nb;

	if (a & bdd_local) localstore_retrieve(bdd_strip_marklocal(a), &na);
	else if (nodecache_get(bdd_strip_marklocal(a), &na));
	else ha = htable_get_data_async(bdd_strip_marklocal(a), &na);

	if (b & bdd_local) localstore_retrieve(bdd_strip_marklocal(b), &nb);
	else if (nodecache_get(bdd_strip_marklocal(b), &nb));
	else hb = htable_get_data_async(bdd_strip_marklocal(b), &nb);

	if (ha != NULL) {
		while (!upc_sync_attempt(ha)) { ws_progress(); }
		nodecache_put(bdd_strip_marklocal(a), &na);
	}

	if (hb != NULL) {
		while (!upc_sync_attempt(hb)) { ws_progress(); }
		nodecache_put(bdd_strip_marklocal(b), &nb);
	}

	// find lowest level
	BDDVAR va = NODE_LEVEL(&na);
	BDDVAR vb = NODE_LEVEL(&nb);
	BDDVAR level = va < vb ? va : vb;

	/* Consult the cache */

	int cachenow = granularity < 2 || prev_lvl == 0 ? 1 : prev_lvl / granularity != level / granularity;
	
	if (cachenow) {
		if (cache_get(bdd_setdata(a, CACHE_AND), b, bdd_false, result)) {
			return;
		}
	}

	/* Get cofactors */

	BDD aLow = a, aHigh = a;
	BDD bLow = b, bHigh = b;

	if (level == va) {
		aLow = node_low(a, &na);
		aHigh = node_high(a, &na);
	}

	if (level == vb) {
		bLow = node_low(b, &nb);
		bHigh = node_high(b, &nb);
	}

	/* Recursive computation */

	BDD low = bdd_invalid, high = bdd_invalid;
	int n = 0;

	if (BDD_ISTRUE(aHigh)) {
		high = bHigh;
	} else if (BDD_ISFALSE(aHigh) || BDD_ISFALSE(bHigh)) {
		high = bdd_false;
	} else if (BDD_ISTRUE(bHigh)) {
		high = aHigh;
	} else {
		AND_INPUT(in, aHigh, bHigh, level);
		ws_spawn(&in);
		n = 1;
	}

	if (BDD_ISTRUE(aLow)) {
		low = bLow;
	} else if (BDD_ISFALSE(aLow) || BDD_ISFALSE(bLow)) {
		low = bdd_false;
	} else if (BDD_ISTRUE(bLow)) {
		low = aLow;
	} else {
		AND_INPUT(in, aLow, bLow, level);
		ws_call(&in, &low);
	}

	if (n) ws_sync(&high);

	/* Store result */

	*result = bdd_makenode(level, low, high);

	if (cachenow) {
		cache_put(bdd_setdata(a, CACHE_AND), b, bdd_false, *result);
	}
}

void bdd_xor(BDD a, BDD b, BDDVAR prev_lvl, BDD *result) {
	// update task counter (for statistical purposes)
	if (BDD_WORKCOUNT) {
		increase_work_counter();
	}

	/* Terminal cases */

	// XOR(F, B) = B
	if (BDD_ISFALSE(a)) {
		*result = b;
		return;
	}

	// XOR(A, F) = A
	if (BDD_ISFALSE(b)) {
		*result = a;
		return;
	}

	// XOR(T, B) = ~B
	if (BDD_ISTRUE(a)) {
		*result = bdd_not(b);
		return;
	}

	// XOR(A, T) = ~A
	if (BDD_ISTRUE(b)) {
		*result = bdd_not(a);
		return;
	}

	// XOR(A, A) = F
	if (a == b) {
		*result = bdd_false;
		return;
	}

	// XOR(A, ~A) = T
	if (a == bdd_not(b)) {
		*result = bdd_true;
		return;
	}

	/* Improve for caching */

	if (bdd_stripmark(a) > bdd_stripmark(b)) {
		BDD t = b;
		b = a;
		a = t;
	}

	// XOR(~A, B) = XOR(A, ~B)
	if (bdd_hasmark(a)) {
		a = bdd_stripmark(a);
		b = bdd_not(b);
	}

	/* Retrieve nodes */

	upc_handle_t ha = NULL, hb = NULL;
	bddnode_t na, nb;

	if (a & bdd_local) localstore_retrieve(bdd_strip_marklocal(a), &na);
	else if (nodecache_get(bdd_strip_marklocal(a), &na));
	else ha = htable_get_data_async(bdd_strip_marklocal(a), &na);

	if (b & bdd_local) localstore_retrieve(bdd_strip_marklocal(b), &nb);
	else if (nodecache_get(bdd_strip_marklocal(b), &nb));
	else hb = htable_get_data_async(bdd_strip_marklocal(b), &nb);

	if (ha != NULL) {
		while (!upc_sync_attempt(ha)) { ws_progress(); }
		nodecache_put(bdd_strip_marklocal(a), &na);
	}

	if (hb != NULL) {
		while (!upc_sync_attempt(hb)) { ws_progress(); }
		nodecache_put(bdd_strip_marklocal(b), &nb);
	}

	// find lowest level
	BDDVAR va = NODE_LEVEL(&na);
	BDDVAR vb = NODE_LEVEL(&nb);
	BDDVAR level = va < vb ? va : vb;

	/* Consult the cache */

	int cachenow = granularity < 2 || prev_lvl == 0 ? 1 : prev_lvl / granularity != level / granularity;
	
	if (cachenow) {
		if (cache_get(bdd_setdata(a, CACHE_XOR), b, bdd_false, result)) {
			return;
		}
	}

	/* Get cofactors */

	BDD aLow = a, aHigh = a;
	BDD bLow = b, bHigh = b;

	if (level == va) {
		aLow = node_low(a, &na);
		aHigh = node_high(a, &na);
	}

	if (level == vb) {
		bLow = node_low(b, &nb);
		bHigh = node_high(b, &nb);
	}

	/* Recursive computation */

	XOR_INPUT(in1, aHigh, bHigh, level);
	XOR_INPUT(in2, aLow, bLow, level);
	BDD low, high;

	ws_spawn(&in1);
	ws_call(&in2, &low);
	ws_sync(&high);

	/* Store result */

	*result = bdd_makenode(level, low, high);

	if (cachenow) {
		cache_put(bdd_setdata(a, CACHE_XOR), b, bdd_false, *result);
	}
}

void bdd_ite(BDD a, BDD b, BDD c, BDDVAR prev_lvl, BDD *result) {
	// update task counter (for statistical purposes)
	if (BDD_WORKCOUNT) {
		increase_work_counter();
	}

	/* Terminal cases */

	// ITE(T, B, _) = B
	if (BDD_ISTRUE(a)) {
		*result = b;
		return;
	}

	// ITE(F, _, C) = C
	if (BDD_ISFALSE(a)) {
		*result = c;
		return;
	}

	// ITE(A, A, C) = ITE(A, T, C)
	if (a == b) b = bdd_true;

	// ITE(A, ~A, C) = ITE(A, F, C)
	if (a == bdd_not(b)) b = bdd_false;

	// ITE(A, B, A) = ITE(A, B, F)
	if (a == c) c = bdd_false;

	// ITE(A, B, ~A) = ITE(A, B, T)
	if (a == bdd_not(c)) c = bdd_true;

	// ITE(A, B, B) = B
	if (b == c) {
		*result = b;
		return;
	}

	// ITE(A, T, F) = A
	if (BDD_ISTRUE(b) && BDD_ISFALSE(c)) {
		*result = a;
		return;
	}

	// ITE(A, F, T) = ~A
	if (BDD_ISFALSE(b) && BDD_ISTRUE(c)) {
		*result = bdd_not(a);
		return;
	}

	/* Cases that reduce to AND and XOR */

	// ITE(A, B, F) = AND(A, B)
	if (BDD_ISFALSE(c)) {
		AND_INPUT(in, a, b, prev_lvl);
		ws_call(&in, result);
		return;
	}

	// ITE(A, T, C) = ~AND(~A, ~C)
	if (BDD_ISTRUE(b)) {
		AND_INPUT(in, bdd_not(a), bdd_not(c), prev_lvl);
		BDD out;
		ws_call(&in, &out);
		*result = bdd_not(out);
		return;
	}

	// ITE(A, F, C) = AND(~A, C)
	if (BDD_ISFALSE(b)) {
		AND_INPUT(in, bdd_not(a), c, prev_lvl);
		ws_call(&in, result);
		return;
	}

	// ITE(A, B, T) = ~AND(A, ~B)
	if (BDD_ISTRUE(c)) {
		AND_INPUT(in, a, bdd_not(b), prev_lvl);
		BDD out;
		ws_call(&in, &out);
		*result = bdd_not(out);
		return;
	}

	// ITE(A, B, ~B) = XOR(A, ~B)
	if (b == bdd_not(c)) {
		XOR_INPUT(in, a, c, 0);
		ws_call(&in, result);
		return;
	}

	/* At this point, there are no more terminals */
	/* Canonical for optimal cache use */

	// ITE(~A, B, C) = ITE(A, C, B)
	if (bdd_hasmark(a)) {
		a = bdd_stripmark(a);
		BDD t = c;
		c = b;
		b = t;
	}

	// ITE(A, B, C) = ~ITE(A, ~B, ~C)
	int mark = 0;
	if (bdd_hasmark(b)) {
		b = bdd_not(b);
		c = bdd_not(c);
		mark = 1;
	}

	/* Retrieve nodes */

	upc_handle_t ha = NULL, hb = NULL, hc = NULL;
	bddnode_t na, nb, nc;

	if (a & bdd_local) localstore_retrieve(bdd_strip_marklocal(a), &na);
	else if (nodecache_get(bdd_strip_marklocal(a), &na));
	else ha = htable_get_data_async(bdd_strip_marklocal(a), &na);

	if (b & bdd_local) localstore_retrieve(bdd_strip_marklocal(b), &nb);
	else if (nodecache_get(bdd_strip_marklocal(b), &nb));
	else hb = htable_get_data_async(bdd_strip_marklocal(b), &nb);

	if (c & bdd_local) localstore_retrieve(bdd_strip_marklocal(c), &nc);
	else if (nodecache_get(bdd_strip_marklocal(c), &nc));
	else hc = htable_get_data_async(bdd_strip_marklocal(c), &nc);

	if (ha != NULL) {
		while (!upc_sync_attempt(ha)) { ws_progress(); }
		nodecache_put(bdd_strip_marklocal(a), &na);
	}

	if (hb != NULL) {
		while (!upc_sync_attempt(hb)) { ws_progress(); }
		nodecache_put(bdd_strip_marklocal(b), &nb);
	}

	if (hc != NULL) { 
		while (!upc_sync_attempt(hc)) { ws_progress(); }
		nodecache_put(bdd_strip_marklocal(c), &nc);
	}

	// find lowest level
	BDDVAR va = NODE_LEVEL(&na);
	BDDVAR vb = NODE_LEVEL(&nb);
	BDDVAR vc = NODE_LEVEL(&nc);
	BDDVAR level = vb < vc ? vb : vc;

	// fast case
	if (va < level && BDD_ISFALSE(node_low(a, &na)) && BDD_ISTRUE(node_high(a, &na))) {
		BDD res = bdd_makenode(va, c, b);
		*result = mark ? bdd_not(res) : res;
		return;
	}

	if (va < level) level = va;

	/* Consult the cache */

	int cachenow = granularity < 2 || prev_lvl == 0 ? 1 : prev_lvl / granularity != level / granularity;
	
	if (cachenow) {
		BDD res;
		if (cache_get(bdd_setdata(a, CACHE_ITE), b, c, &res)) {
			*result = mark ? bdd_not(res) : res;
			return;
		}
	}

	/* Get cofactors */

	BDD aLow = a, aHigh = a;
	BDD bLow = b, bHigh = b;
	BDD cLow = b, cHigh = b;

	if (level == va) {
		aLow = node_low(a, &na);
		aHigh = node_high(a, &na);
	}

	if (level == vb) {
		bLow = node_low(b, &nb);
		bHigh = node_high(b, &nb);
	}

	if (level == vc) {
		cLow = node_low(c, &nc);
		cHigh = node_high(c, &nc);
	}

	/* Recursive computation */

	BDD low = bdd_invalid, high = bdd_invalid;
	int n = 0;

	if (BDD_ISTRUE(aHigh)) {
		high = bHigh;
	} else if (BDD_ISFALSE(aHigh)) {
		high = cHigh;
	} else {
		ITE_INPUT(in, aHigh, bHigh, cHigh, level);
		ws_spawn(&in);
		n = 1;
	}

	if (BDD_ISTRUE(aLow)) {
		low = bLow;
	} else if (BDD_ISFALSE(aLow)) {
		low = cLow;
	} else {
		ITE_INPUT(in, aLow, bLow, cLow, level);
		ws_call(&in, &low);
	}

	if (n) ws_sync(&high);

	/* Construct result */

	BDD res = bdd_makenode(level, low, high);

	if (cachenow) {
		cache_put(bdd_setdata(a, CACHE_ITE), b, c, res);
	}

	*result = mark ? bdd_not(res) : res;
}

void bdd_relnext(BDD a, BDD b, BDDVAR prev_lvl, uint64_t vars, BDD *result) {
	// update task counter (for statistical purposes)
	if (BDD_WORKCOUNT) {
		increase_work_counter();
	}

	/* Terminal cases */

	if (BDD_ISTRUE(a) && BDD_ISTRUE(b)) {
		*result = bdd_true;
		return;
	}

	if (BDD_ISFALSE(a) || BDD_ISFALSE(b)) {
		*result = bdd_false;
		return;
	}

	uint64_t from = vars & 0x00000000ffffffff;
	uint64_t node_i = vars >> 32;

	varchain_t *varchain;

	if (from < next_count) {
		rel_t rel = next[from];
		varchain = rel->vararray[node_i];
	}

	if (varchain == NULL) {
		*result = a;
		return;
	}

	/* Retrieve nodes */

	bddnode_t na, nb;
	bool ba = true, bb = true;
	upc_handle_t ha = NULL, hb = NULL;

	if (bdd_isconst(a)) ba = false; 
	else if (a & bdd_local) localstore_retrieve(bdd_strip_marklocal(a), &na);
	else if (nodecache_get(bdd_strip_marklocal(a), &na));
	else ha = htable_get_data_async(bdd_strip_marklocal(a), &na);
	
	if (bdd_isconst(b)) bb = false; 
	else if (b & bdd_local) localstore_retrieve(bdd_strip_marklocal(b), &nb);
	else if (nodecache_get(bdd_strip_marklocal(b), &nb));
	else hb = htable_get_data_async(bdd_strip_marklocal(b), &nb);

	// synchronize on requests
	if (ha != NULL) {
		while (!upc_sync_attempt(ha)) { ws_progress(); }
		nodecache_put(bdd_strip_marklocal(a), &na);
	}

	if (hb != NULL) {
		while (!upc_sync_attempt(hb)) { ws_progress(); }
		nodecache_put(bdd_strip_marklocal(b), &nb);
	}

	// determine top level
	BDDVAR va = ba ? NODE_LEVEL(&na) : 0xffffffff;
	BDDVAR vb = bb ? NODE_LEVEL(&nb) : 0xffffffff;
	BDDVAR level = va < vb ? va : vb;

	/* Skip variables */

	for (;;) {
		// check if level is s/t 
		if (level == varchain->level || (level^1) == varchain->level) break;
		// check if level < s/t 
		if (level < varchain->level) break;

		varchain = varchain->next;
		node_i++;

		if (varchain == NULL) {
			*result = a;
			return;
		}
	}

	/* Consult the cache */

	int cachenow = granularity < 2 || prev_lvl == 0 ? 1 : prev_lvl / granularity != level / granularity;
	uint64_t cache_var_index = from | (node_i << 32);

	if (cachenow) {
		BDD res;
		if (cache_get(bdd_setdata(a, CACHE_RELNEXT), b, cache_var_index, &res)) {
			*result = res;
			return;
		}
	}

	/* Recursive computation */

	if (level == varchain->level || (level^1) == varchain->level) {

		// Get s and t
		BDDVAR s = level & (~1);
		BDDVAR t = s + 1;
		BDD a0, a1, b0, b1;

		if (ba) {
			if (NODE_LEVEL(&na) == s) {
				a0 = node_low(a, &na);
				a1 = node_high(a, &na);
			} else {
				a0 = a1 = a;
			}
		} else {
			a0 = a1 = a;
		}

		if (bb) {
			if (NODE_LEVEL(&nb) == s) {
				b0 = node_low(b, &nb);
				b1 = node_high(b, &nb);
			} else {
				b0 = b1 = b;
			}
		} else {
			b0 = b1 = b;
		}

		/* Retrieve nodes */

		BDD b00, b01, b10, b11;
		bddnode_t nb0, nb1;
		ha = NULL; hb = NULL;

		if (!bdd_isconst(b0)) {
			if (b0 & bdd_local) localstore_retrieve(bdd_strip_marklocal(b0), &nb0);
			else if (nodecache_get(bdd_strip_marklocal(b0), &nb0));
			else ha = htable_get_data_async(bdd_strip_marklocal(b0), &nb0);
		}

		if (!bdd_isconst(b1)) {
			if (b1 & bdd_local) localstore_retrieve(bdd_strip_marklocal(b1), &nb1);
			else if (nodecache_get(bdd_strip_marklocal(b1), &nb1));
			else hb = htable_get_data_async(bdd_strip_marklocal(b1), &nb1);
		}

		if (!bdd_isconst(b0)) {
			if (ha != NULL) {
				while (!upc_sync_attempt(ha)) { ws_progress(); }
				nodecache_put(bdd_strip_marklocal(b0), &nb0);
			}

			if (NODE_LEVEL(&nb0) == t) {
				b00 = node_low(b0, &nb0);
				b01 = node_high(b0, &nb0);
			} else {
				b00 = b01 = b0;
			}
		} else {
			b00 = b01 = b0;
		}

		if (!bdd_isconst(b1)) {
			if (hb != NULL) {
				while (!upc_sync_attempt(hb)) { ws_progress(); }
				nodecache_put(bdd_strip_marklocal(b1), &nb1);
			}

			if (NODE_LEVEL(&nb1) == t) {
				b10 = node_low(b1, &nb1);
				b11 = node_high(b1, &nb1);
			} else {
				b10 = b11 = b1;
			}
		} else {
			b10 = b11 = b1;
		}

		/* Recursive computation */

		uint64_t _vars = from | ((node_i + 1) << 32);

		BDD f, e, d, c;

		RELPROD_INPUT(in1, a0, b00, _vars, level);
		RELPROD_INPUT(in2, a1, b10, _vars, level);
		RELPROD_INPUT(in3, a0, b01, _vars, level);
		RELPROD_INPUT(in4, a1, b11, _vars, level);

		ws_spawn(&in1);
		ws_spawn(&in2);
		ws_spawn(&in3);
		ws_call(&in4, &f);
		ws_sync(&e);
		ws_sync(&d);
		ws_sync(&c);

		ITE_INPUT(in5, c, bdd_true, d, 0);
		ITE_INPUT(in6, e, bdd_true, f, 0);

		ws_spawn(&in5);
		ws_call(&in6, &d);
		ws_sync(&c);

		*result = bdd_makenode(s, c, d);
	}
	else {
		BDD a0, a1, b0, b1;

		if (ba) {
			if (NODE_LEVEL(&na) == level) {
				a0 = node_low(a, &na);
				a1 = node_high(a, &na);
			} else {
				a0 = a1 = a;
			}
		} else {
			a0 = a1 = a;
		}

		if (bb) {
			if (NODE_LEVEL(&nb) == level) {
				b0 = node_low(b, &nb);
				b1 = node_high(b, &nb);
			} else {
				b0 = b1 = b;
			}
		} else {
			b0 = b1 = b;
		}

		/* Recursive computation */

		uint64_t _vars = from | (node_i << 32);

		if (b0 != b1) {
			if (a0 == a1) {
				RELPROD_INPUT(in1, a0, b0, _vars, level);
				RELPROD_INPUT(in2, a1, b1, _vars, level);
				BDD r0, r1;

				ws_spawn(&in1);
				ws_call(&in2, &r1);
				ws_sync(&r0);

				*result = bdd_or(r0, r1);
			} else {
				BDD r00, r01, r10, r11;

				RELPROD_INPUT(in1, a0, b0, _vars, level);
				RELPROD_INPUT(in2, a1, b1, _vars, level);
				RELPROD_INPUT(in3, a0, b0, _vars, level);
				RELPROD_INPUT(in4, a1, b1, _vars, level);

				ws_spawn(&in1);
				ws_spawn(&in2);
				ws_spawn(&in3);
				ws_call(&in4, &r11);
				ws_sync(&r10);
				ws_sync(&r01);
				ws_sync(&r00);

				BDD r0, r1;

				ITE_INPUT(in5, r00, bdd_true, r01, 0);
				ITE_INPUT(in6, r10, bdd_true, r11, 0);

				ws_spawn(&in5);
				ws_call(&in6, &r1);
				ws_sync(&r0);

				*result = bdd_makenode(level, r0, r1);
			}
		} else {
			RELPROD_INPUT(in1, a0, b0, _vars, level);
			RELPROD_INPUT(in2, a1, b1, _vars, level);
			BDD r0, r1;

			ws_spawn(&in1);
			ws_call(&in2, &r1);
			ws_sync(&r0);

			*result = bdd_makenode(level, r0, r1);
		}
	}

	if (cachenow) {
		cache_put(bdd_setdata(a, CACHE_RELNEXT), b, cache_var_index, *result);
	}
}

void bdd_go_par(BDD cur, BDD visited, size_t from, size_t len, BDD *result) {
	if (BDD_WORKCOUNT) {
		increase_work_counter();
	}

	if (len == 1) {
		RELPROD_INPUT(in1, cur, next[from]->bdd, from, 0);
		BDD succ;
		ws_call(&in1, &succ);

		*result = bdd_diff(succ, visited);
	}
	else {
		BDD left, right;

		GO_PAR_INPUT(in1, cur, visited, from, (len+1)/2);
		GO_PAR_INPUT(in2, cur, visited, from+(len+1)/2, len/2);

		ws_spawn(&in1);
		ws_call(&in2, &right);
		ws_sync(&left);

		*result = bdd_or(left, right);
	}
}

void bdd_par(BDD bdd, uint64_t *result) {
	if (BDD_WORKCOUNT) {
		increase_work_counter();
	}

	BDD visited = bdd;
	BDD new = visited;

	size_t counter = 1;

	do {
		printf("Level %zu... ", counter++);

		GO_PAR_INPUT(in1, new, visited, 0, next_count);
		ws_call(&in1, &new);

		visited = bdd_or(visited, new);

		if (MYTHREAD == 0) {
			printf("done. ");

			if (BDD_INTERMEDIATE_COUNT) {
				printf("%lu BDD nodes", bdd_nodecount(visited));
			}
			
			printf("\n");
		}
	} 
	while (bdd_striplocal(new) != bdd_false);

	*result = visited;
}

void driver(bdd_input_t *in, BDD *out) {
	// find metadata of input
	uint64_t metadata = in->metadata;
	uint64_t val = metadata & BDD_INPUT_LEVEL;

	// search and invoke the intended task
	if (metadata & BDD_INPUT_TYPE_ITE)
		bdd_ite(in->a, in->b, in->c, in->lvl, out);
	else if (metadata & BDD_INPUT_TYPE_AND)
		bdd_and(in->a, in->b, in->lvl, out);
	else if (metadata & BDD_INPUT_TYPE_XOR)
		bdd_xor(in->a, in->b, in->lvl, out);
	else if (metadata & BDD_INPUT_TYPE_RELPROD)
		bdd_relnext(in->a, in->b, in->lvl, val, out);
	else if (metadata & BDD_INPUT_TYPE_SATCOUNT)
		bdd_satcount(in->a, val, in->lvl, out);
	else if (metadata & BDD_INPUT_TYPE_GOPAR)
		bdd_go_par(in->a, in->b, in->c, val, out);
	else if (metadata & BDD_INPUT_TYPE_PAR)
		bdd_par(in->a, out);
}

void bdd_printinfo() {
	if (BDD_WORKCOUNT) {
		printf("------------------------------------------------------\n");
		printf("Number of tasks performed:\n");

		int i; for (i = 0; i < THREADS; i++) {
			//printf("Process %i performed %lu tasks\n", i, work_count[i]);
		}
	}
}

void bdd_init() {
	// initialize hash table and wstealer
	ws_init(&driver, sizeof(bdd_input_t), sizeof(BDD));
	htable_init(&ws_progress);
	cache_init(&ws_progress);
	nodecache_init();

	// reset work counters
	if (BDD_WORKCOUNT) {
		int i; for (i = 0; i < THREADS; i++) {
			//work_count[i] = 0;
		}
	}
}
