#pragma once

#include <util/platform.h>
#include <obs-data.h>
#include <stdio.h>

typedef struct caffeine_sample_logger {
	char file_name[1000];
	char line_buff[1000];
	bool is_ok;
	int sample_cnt;
	double prev_wall_time_ms;
	double prev_sample_obs_timestamp_ms;
	double prev_caffeine_func_time_ms;
	double prev_obs_app_time_ms;
} caffeine_sample_logger_t;

bool caffeine_sample_logger_init(caffeine_sample_logger_t *lpsl,
				 const char *file_name);

void caffeine_sample_logger_log_sample(
	caffeine_sample_logger_t *lpsl, bool sample_sent_to_libcaffeine,
	const char *reason_not_sent_to_libcaffeine, uint64_t wall_time_ns,
	uint64_t sample_obs_timestamp_ns, uint64_t pair_obs_last_timestamp_ns,
	uint64_t caffeine_func_time_ns, uint64_t obs_app_time_ns);
