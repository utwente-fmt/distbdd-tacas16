#include "wstealer.h"

#ifndef WS_DEQUE_ELEM
#define WS_DEQUE_ELEM(n) bupc_ptradd(deque, WS_DEQUE_SIZE, task_size, n)
#endif

#ifndef WS_TRANS_ELEM
#define WS_TRANS_ELEM(n) bupc_ptradd(transfer, 1, task_size, n)
#endif

#ifndef ITERATE_ALL_VICTIMS
#define ITERATE_ALL_VICTIMS(_v, _x) \
	int __i; for (__i = 0; __i < 4; __i++) { \
	int __j; for (__j = 0; __j < victims_n[__i]; __j++) { \
	size_t _v = victims[__i][rand() % victims_n[__i]]; _x }}
#endif

// shared deque and data transfer cells
static shared void *deque;
static shared uint64_t request[THREADS];
static shared void *transfer;
static shared uint64_t term[THREADS];

// transfer completion semaphores
static bupc_sem_t * shared comp[THREADS];
static bupc_sem_t **completion;

// pointers to local pointers of shared regions
static void *deque_p = NULL;
static void *deque_head_p = NULL;
static uint64_t *request_p = NULL;
static void *transfer_p = NULL;
static uint64_t *term_p = NULL;

// pointer to an empty task (used for refusals)
static void *empty_task;
static size_t last_hint;
static bool has_last_hint;

// local deque counters
static uint64_t head, tail;

// information regarding task input and output
static void (*handler)(void*, void*);
static size_t input_size, output_size;
static size_t task_size, block_size;

// pointers to victim arrays and corresponding counters
static size_t *victims[4];
static size_t victims_n[4];
static uint32_t victims_steal[4];

static inline double wctime() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

static inline void init_victim_array() {
	// allocate memory for victim arrays
	int i; for (i = 0; i < 4; i++) {
		victims[i] = (size_t*)malloc(sizeof(size_t) * THREADS);
		victims_n[i] = 0;
	}

	// initialize victim arrays
	for (i = 0; i < THREADS; i++) {
		// find distance between myself and thread 'i'
		uint32_t distance = bupc_thread_distance(MYTHREAD, i);

		// fill victim arrays
		if (distance == BUPC_THREADS_VERYNEAR) victims[0][victims_n[0]++] = i;
		if (distance == BUPC_THREADS_NEAR) victims[1][victims_n[1]++] = i;
		if (distance == BUPC_THREADS_FAR) victims[2][victims_n[2]++] = i;
		if (distance == BUPC_THREADS_VERYFAR) victims[3][victims_n[3]++] = i;
	}

	// set amount of steals for hierarchical stealing
	victims_steal[0] = 1;
	victims_steal[1] = 1;
	victims_steal[2] = 1;
	victims_steal[3] = 1;
}

static inline void init_comp_semaphores() {
	// allocate completion semaphore
	comp[MYTHREAD] = bupc_sem_alloc(0);
	completion = malloc(THREADS * sizeof(bupc_sem_t*));

	// wait for all threads to have allocated their semaphore
	upc_barrier;

	// copy all semaphore pointers to local memory
	int i; for (i = 0; i < THREADS; i++) {
		completion[i] = comp[i];
	}
}

void ws_init(void *func, size_t input_s, size_t output_s) {
	// store input parameters
	input_size = input_s;
	output_size = output_s;
	handler = func;

	// allocate the shared private deque collectively
	task_size = sizeof(uint64_t) + input_s + output_s;
	block_size = WS_DEQUE_SIZE * task_size;
	deque = upc_all_alloc(THREADS, block_size);

	// allocate and initialize the 'empty task'
	empty_task = malloc(task_size);
	*(uint64_t*)empty_task = WS_TASK_EMPTY;
	has_last_hint = false;

	// allocate shared transfer cells
	transfer = upc_all_alloc(THREADS, task_size);

	// get local pointers to shared data owned by this thread
	deque_p = upc_cast(WS_DEQUE_ELEM(WS_DEQUE_OFFSET));
	deque_head_p = upc_cast(WS_DEQUE_ELEM(WS_DEQUE_OFFSET));
	transfer_p = upc_cast(WS_TRANS_ELEM(MYTHREAD));
	request_p = upc_cast(&request[MYTHREAD]);
	term_p = upc_cast(&term[MYTHREAD]);

	// assign initial values
	*request_p = 0;
	head = 0;
	tail = 0;

	// initialize victim arrays and semaphores
	init_victim_array();
	init_comp_semaphores();
}

void ws_free() {
	// free shared arrays
	upc_all_free(deque);
	upc_all_free(transfer);

	// free completion semaphore
	bupc_sem_free(comp[MYTHREAD]);
	free(completion);
	free(empty_task);

	// free victim arrays
	int i; for (i = 0; i < 4; i++) {
		free(victims[i]);
	}
}

static inline void ws_refuse(uint32_t slot) {
	// extract thief identifier from slot
	size_t thief = slot & WS_REQ_THIEF;

	// set 'empty' flag on first task
	bupc_memput_signal_async(WS_TRANS_ELEM(thief), empty_task, sizeof(uint64_t), completion[thief], 1);
}

static inline uint32_t hinted_block() {
	uint32_t res = WS_REQ_BLOCK;

	// apply hint if possible..
	if (has_last_hint) {
		res |= (last_hint & WS_REQ_THIEF) | WS_REQ_HINT;
	}

	return res;
}

static inline void ws_communicate() {
	// make progress on pending network operations
	bupc_poll();

	// find slot pointer
	uint32_t *slots = (uint32_t*)request_p;

	// iterate over every slot
	int i; for (i = 0; i < 2; i++) {

		// check if request cell must block further requests
		if (tail - head < WS_TRANS_THRESHOLD) {
			if ((slots[i] & WS_REQ_BLOCK) == 0) {

				// apply hint to 'blocking' message
				uint32_t block = hinted_block();

				// check if the slot is occupied
				if (slots[i] & WS_REQ_OCC) {
					ws_refuse(slots[i]);
					slots[i] = block;
				}

				// attempt to block further requests via a local CAS
				else if (!LOCAL_CAS(&slots[i], 0, block)) {
					ws_refuse(slots[i]);
					slots[i] = block;
				}
			}
		}

		// check if new incoming requests must again be allowed
		else if (slots[i] & WS_REQ_BLOCK) {
			slots[i] = 0;
		}

		// check for incoming requests
		else if (slots[i] & WS_REQ_OCC) {

			// extract thief and amount from slot
			size_t thief = slots[i] & WS_REQ_THIEF;
			uint32_t amount = (slots[i] & WS_REQ_AMOUNT) >> 23;

			// empty request cell
			slots[i] = 0;

			// determine actual amount of tasks to send
			if (amount == 0) amount = (tail - head) / 2;
			amount = MIN(amount, tail - head);
			amount = MIN(amount, WS_MAX_STEALS);
			amount = MIN(amount, 63);

			uint64_t *head_p = (uint64_t*)deque_head_p;

			// fill in administrative details
			int i; for (i = 0; i < amount; i++) {
				*head_p |= WS_TASK_STOLEN;
				*head_p |= ((uint64_t)thief << 44) & WS_TASK_ID;
				*head_p |= ((uint64_t)amount << 38) & WS_TASK_AMOUNT;
				*head_p |= (WS_DEQUE_OFFSET + head) & WS_TASK_INDEX;

				// update head pointer
				(char*)head_p += task_size; head++;
			}

			// transfer task to transfer cell of thief
			bupc_memput_signal_async(WS_TRANS_ELEM(thief), deque_head_p, amount * task_size, completion[thief], 1);
			has_last_hint = true;
			last_hint = thief;

			// update head pointer
			(char*)deque_head_p = head_p;
		}
	}
}

static inline uint32_t ws_steal(size_t victim, uint32_t amount) {
	// first handle incoming requests
	ws_communicate();

	// build steal request message
	uint32_t req = (MYTHREAD & WS_REQ_THIEF) | WS_REQ_OCC;
	req |= (amount << 23) & WS_REQ_AMOUNT;

	// write steal request with cas
	shared uint32_t *slot_addr = bupc_ptradd(&request[victim], 2, sizeof(uint32_t), rand() & 0x00000001);
	uint32_t result = CAS32(slot_addr, 0, req);
	ADD_TO_STEAL_ATTEMPTS(1);

	if (result == 0) {
		// wait for the task to arrive...
		bupc_sem_wait(completion[MYTHREAD]);

		// find metadata pointer of first task
		uint64_t *metadata = (uint64_t*)transfer_p;

		// if no tasks received, return negatively..
		if (*metadata & WS_TASK_EMPTY) {
			ADD_TO_EMPTY_STEALS(1);
			return WS_STEAL_FAIL;
		}

		// read the amount of tasks stolen
		uint32_t amount = (*metadata & WS_TASK_AMOUNT) >> 38;
		ADD_TO_STEALS(1);

		// copy received task to stack
		void *tasks = alloca(amount * task_size);
		memcpy(tasks, transfer_p, amount * task_size);
		char *current = (char*)tasks + (amount - 1) * task_size;

		// iterate over stolen tasks
		int i; for (i = 0; i < amount; i++) {

			// find pointers to input and output parameters
			uint64_t *meta = (uint64_t*)current;
			char *input = (char*)meta + sizeof(uint64_t);
			char *output = input + input_size;

			// execute received task
			handler(input, output);
			ADD_EXECUTED_TASKS(1);

			// write back the task result
			shared void *addr = WS_DEQUE_ELEM(*meta & WS_TASK_INDEX);
			upc_handle_t h = upc_memput_nb(addr, current, task_size);

			// wait for the result to be written back
			while (!upc_sync_attempt(h)) { ws_communicate(); }

			// mark task as completed (set 'done' flag)
			upc_memset_nbi(addr, 0xFF, sizeof(uint64_t));

			// decrease task pointer
			current -= task_size;
		}

		// return a positive result
		return WS_STEAL_SUCC;
	}
	else if (result & WS_REQ_BLOCK) {
		// record 'blocked' steal attempt
		ADD_TO_BLOCKED_STEALS(1);

		// return hint if received
		if (result & WS_REQ_HINT) {
			return (result & WS_REQ_THIEF) | WS_STEAL_FAIL | WS_STEAL_HINT;
		}
	}
	else if (result & WS_REQ_HINT) {
		// record a failed steal attempt
		ADD_TO_FAILED_STEALS(1);

		// return the received hint
		return (result & WS_REQ_THIEF) | WS_STEAL_FAIL | WS_STEAL_HINT;
	}

	// record a failed steal attempt
	ADD_TO_FAILED_STEALS(1);

	// steal attempt failed...
	return WS_STEAL_FAIL;
}

static inline bool ws_steal_hinted(size_t victim, uint32_t amount) {
	// allocate a history for steal attempts
	uint32_t attempts[WS_MAX_HINTED];

	int i; for (i = 0; i < WS_MAX_HINTED; i++) {
		// attempt to steal from 'victim'
		uint32_t result = ws_steal(victim, amount);

		// return positively on successful steal
		if (result & WS_STEAL_SUCC) return true;

		// check if we received a hint from the victim
		if (result & WS_STEAL_HINT) {

			// store the hint
			attempts[i] = result & WS_STEAL_ID;

			// do not steal from ourselves
			if (attempts[i] == MYTHREAD) return false;

			// make sure we do not enter a loop..
			int j; for (j = 0; j < i; j++) {
				if (attempts[j] == attempts[i]) return false;
			}

			// attempt to steal from the hinted victim
			victim = attempts[i];
		}
		else break;
	}

	return false;
}

void ws_spawn(void *input) {
	// reset task metadata
	*(uint64_t*)deque_p = 0;

	// copy input parameters to new deque entry
	memcpy((char*)deque_p + sizeof(uint64_t), input, input_size);

	// move stack pointer to next slot
	(char*)deque_p += task_size;
	tail++;
}

void ws_call(void *input, void *output) {
	// first handle incoming requests
	ws_communicate();

	// then execute the task
	handler(input, output);

	// record task execution
	ADD_EXECUTED_TASKS(1);
}

void ws_sync(void *output) {
	// first handle incoming requests
	ws_communicate();

	// get pointer to previous task
	char *prev_p = (char*)deque_p - task_size;

	// get pointer to metadata section of previous task
	uint64_t *metadata = (uint64_t*)prev_p;

	// test if the task has been stolen
	if (*metadata & WS_TASK_STOLEN) {
		// to peform leapfrogging, first find the thiefs ID
		size_t thief = (*metadata & WS_TASK_ID) >> 44;

		// wait for stolen task to complete...
		while ((*metadata & WS_TASK_DONE) == 0) {

			// perform hinted leapfrogging
			if (ws_steal_hinted(thief, 1) & WS_STEAL_SUCC) continue;
			if (*metadata & WS_TASK_DONE) break;

			// try to steal remotely
			int i; for (i = 0; i < 4; i++) {
				int j; for (j = 0; j < victims_n[i]; j++) {
					size_t victim = victims[i][rand() % victims_n[i]];

					if (ws_steal(victim, victims_steal[i]) & WS_STEAL_SUCC) goto sync_reset;
					if (*metadata & WS_TASK_DONE) goto sync_reset;
				}
			}
		sync_reset:
		}

		// copy the return value of the task to the 'output' pointer
		memcpy(output, prev_p + sizeof(uint64_t) + input_size, output_size);

		// the task has been completed, update both head and tail pointers..
		(char*)deque_head_p -= task_size;
		deque_p = prev_p;
		head--;
		tail--;
	}
	else {
		// the task has not been stolen, decrease the deque pointer
		deque_p = prev_p;
		tail--;

		// copy input parameters to stack
		void *input = alloca(input_size);
		memcpy(input, prev_p + sizeof(uint64_t), input_size);

		// execute task normally
		handler(input, output);

		// record task execution
		ADD_EXECUTED_TASKS(1);
	}
}

double ws_initiate(void *input, void *output) {
	// reset all termination flags
	*term_p = 0;
	upc_barrier;

	// execute the given task and measure its execution time
	double start = wctime();
	ws_call(input, output);
	double stop = wctime();

	// record task execution
	ADD_EXECUTED_TASKS(1);

	// flip all termination flags..
	int i; for (i = 0; i < THREADS; i++) {
		upc_memset_nbi(&term[i], 0xFF, sizeof(uint64_t));
	}

	// wait for all workers to terminate
	upc_barrier;

	// return execution time
	return stop - start;
}

void ws_participate() {
	// reset all termination flags
	*term_p = 0;
	upc_barrier;

	do {
		int i; for (i = 0; i < 4; i++) {
			int j; for (j = 0; j < victims_n[i]; j++) {
				size_t victim = victims[i][rand() % victims_n[i]];

				if (ws_steal(victim, victims_steal[i]) & WS_STEAL_SUCC) goto test_term;
				if (*term_p != 0) goto test_term;
			}
		}
	test_term:
	}
	while (*term_p == 0);

	// wait for all workers to terminate
	upc_barrier;
}

double ws_compute(void *input, void *output) {
	if (MYTHREAD == 0)
		return ws_initiate(input, output);
	else
		ws_participate();

	return 0;
}

void ws_progress() {
	// simply check for incoming requests
	ws_communicate();
}

void ws_print_deque_status() {
	printf("thread=%u, head=%lu, tail=%lu\n", MYTHREAD, head, tail);
}

void ws_statistics() {
	#ifdef WS_USE_STATS
	printf("%i/%i - victim array sizes: (%lu, %lu, %lu, %lu)\n", MYTHREAD, THREADS, victims_n[0], victims_n[1], victims_n[2], victims_n[3]);
	printf("%i/%i - nr. of steal attempts = %lu\n", MYTHREAD, THREADS, _steal_attempts);
	printf("%i/%i - nr. of successful steals = %lu\n", MYTHREAD, THREADS, _steals);
	printf("%i/%i - nr. of failed steal attempts (by occupation) = %lu\n", MYTHREAD, THREADS, _failed_steals);
	printf("%i/%i - nr. of empty steal attempts = %lu\n", MYTHREAD, THREADS, _empty_steals);
	printf("%i/%i - nr. of blocked steal attempts = %lu\n", MYTHREAD, THREADS, _blocked_steals);
	printf("%i/%i - tasks executed: %lu\n", MYTHREAD, THREADS, _executed);
	#endif
}
