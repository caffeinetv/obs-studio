#include "caffeine-stopwatch.h"
#include <memory.h>
#include <obs.h>
#include <Windows.h>

void caffeine_stopwatch_init(caffeine_stopwatch_t* lpsw)
{
	memset(lpsw, 0, sizeof(caffeine_stopwatch_t));
}

void caffeine_stopwatch_start(caffeine_stopwatch_t* lpsw)
{
	uint64_t time_ns = 0UL;
	if (!lpsw->running) {
		time_ns = os_gettime_ns();
		lpsw->start_timestamp = time_ns;
		lpsw->value_timestamp = time_ns;
		lpsw->running = true;
	}
}

void caffeine_stopwatch_stop(caffeine_stopwatch_t* lpsw)
{
	uint64_t time_ns = 0UL;
	if (!lpsw->running) {
		time_ns = os_gettime_ns();
		lpsw->accumulator +=
			(lpsw->value_timestamp - lpsw->start_timestamp);
		lpsw->start_timestamp = 0L;
		lpsw->value_timestamp = 0L;
		lpsw->running = false;
	}
}

void caffeine_stopwatch_reset(caffeine_stopwatch_t *lpsw)
{
	bool is_running = lpsw->running;
	caffeine_stopwatch_init(lpsw);
	if (is_running) {
		caffeine_stopwatch_start(lpsw);
	}
}

uint64_t caffeine_stopwatch_get_elapsed_ns(caffeine_stopwatch_t *lpsw) {
	uint64_t time_ns = 0UL;
	uint64_t accumulation_total = lpsw->accumulator;
	if (lpsw->running) {
		time_ns = os_gettime_ns();
		lpsw->value_timestamp = time_ns;
		accumulation_total +=
			lpsw->value_timestamp - lpsw->start_timestamp;
	}
	return accumulation_total;
}

uint64_t caffeine_stopwatch_get_elapsed_ms(caffeine_stopwatch_t *lpsw)
{
	uint64_t elapsed_ns = caffeine_stopwatch_get_elapsed_ns(lpsw);
	return elapsed_ns / 1000000UL;
}

void caffeine_stopwatch_copy_state(caffeine_stopwatch_t *lpsw_dest,
				   caffeine_stopwatch_t *lpsw_src)
{
	memcpy(lpsw_dest, lpsw_src, sizeof(caffeine_stopwatch_t));
}
