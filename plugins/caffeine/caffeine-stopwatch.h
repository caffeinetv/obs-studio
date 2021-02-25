#pragma once

#include <util/platform.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct caffeine_stopwatch {
	bool running;
	uint64_t accumulator;
	uint64_t start_timestamp;
	uint64_t value_timestamp;
} caffeine_stopwatch_t;

void caffeine_stopwatch_init(caffeine_stopwatch_t *lpsw);
void caffeine_stopwatch_start(caffeine_stopwatch_t *lpsw);
void caffeine_stopwatch_stop(caffeine_stopwatch_t *lpsw);
void caffeine_stopwatch_reset(caffeine_stopwatch_t *lpsw);
uint64_t caffeine_stopwatch_get_elapsed_ns(caffeine_stopwatch_t *lpsw);
uint64_t caffeine_stopwatch_get_elapsed_ms(caffeine_stopwatch_t *lpsw);
void caffeine_stopwatch_copy_state(caffeine_stopwatch_t *lpsw_dest,
				   caffeine_stopwatch_t *lpsw_src);

#ifdef __cplusplus
}
#endif
