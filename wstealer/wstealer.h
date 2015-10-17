#ifndef WSTEALER_H
#define WSTEALER_H

#include <upc_relaxed.h>
#include <upc_castable.h>
#include <upc_nb.h>
#include <sys/time.h>
#include <stdbool.h>
#include "atomic.h"

// number of entries per thread (by default 2^21)
#define WS_DEQUE_SIZE 2097152
#define WS_DEQUE_OFFSET (MYTHREAD * WS_DEQUE_SIZE)
#define WS_TRANS_THRESHOLD 2

// maximum size of a steal batch
#define WS_MAX_STEALS 48

// maximum length of hinted steal trials
#define WS_MAX_HINTED 1

// bitmaps for task metadata (for flags, identifier, and index)
#define WS_TASK_STOLEN ((uint64_t)0x8000000000000000) // 1 bit
#define WS_TASK_DONE ((uint64_t)0x4000000000000000) // 1 bit
#define WS_TASK_EMPTY ((uint64_t)0x2000000000000000) // 1 bit
#define WS_TASK_OCC ((uint64_t)0x1000000000000000) // 1 bit
#define WS_TASK_ID ((uint64_t)0x0FFFF00000000000) // 16 bits
#define WS_TASK_AMOUNT ((uint64_t)0x00000FC000000000) // 6 bits
#define WS_TASK_INDEX ((uint64_t)0x0000003FFFFFFFFF) // 38 bits

// bitmaps for the request cell
#define WS_REQ_BLOCK ((uint32_t)0x80000000) // 1 bit
#define WS_REQ_OCC ((uint32_t)0x40000000) // 1 bit
#define WS_REQ_HINT ((uint32_t)0x20000000) // 1 bit
#define WS_REQ_AMOUNT ((uint32_t)0x1F800000) // 6 bit
#define WS_REQ_THIEF ((uint32_t)0x007FFFFF) // 23 bits

// bitmaps for steals
#define WS_STEAL_SUCC ((uint32_t)0x80000000) // 1 bit
#define WS_STEAL_FAIL ((uint32_t)0x40000000) // 1 bit
#define WS_STEAL_HINT ((uint32_t)0x20000000) // 1 bit
#define WS_STEAL_ID ((uint32_t)0x1FFFFFFF) // 23 bits

// allow statistical data to be gathered
// #define WS_USE_STATS 1

#ifdef WS_USE_STATS
static uint64_t _steals = 0;
static uint64_t _steal_attempts = 0;
static uint64_t _failed_steals = 0;
static uint64_t _empty_steals = 0;
static uint64_t _blocked_steals = 0;
static uint64_t _executed = 0;

#define ADD_TO_STEALS(n) { _steals += n; }
#define ADD_TO_STEAL_ATTEMPTS(n) { _steal_attempts += n; }
#define ADD_TO_FAILED_STEALS(n) { _failed_steals += n; }
#define ADD_TO_EMPTY_STEALS(n) { _empty_steals += n; }
#define ADD_TO_BLOCKED_STEALS(n) { _blocked_steals += n; }
#define ADD_EXECUTED_TASKS(n) { _executed += n; }
#else
#define ADD_TO_STEALS(n)
#define ADD_TO_STEAL_ATTEMPTS(n)
#define ADD_TO_FAILED_STEALS(n)
#define ADD_TO_EMPTY_STEALS(n)
#define ADD_TO_BLOCKED_STEALS(n)
#define ADD_EXECUTED_TASKS(n)
#endif

void ws_init(void *func, size_t input_s, size_t output_s);
void ws_spawn(void *input);
void ws_call(void *input, void *output);
void ws_sync(void *output);
double ws_compute(void *input, void *output);
double ws_initiate(void *input, void *output);
void ws_progress();
void ws_participate();
void ws_free();
void ws_statistics();

#endif
