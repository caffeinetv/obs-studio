#include "caffeine-sample-logger.h"
#include <string.h>
#include <stdarg.h>

void caffeine_sample_logger_log(caffeine_sample_logger_t *lpsl,
				const char *format, ...)
{
	if (false == lpsl->is_ok) {
		return;
	}
	va_list args;
	memset(lpsl->line_buff, 0, sizeof(lpsl->line_buff));
	va_start(args, format);
	int num_chars = vsprintf_s(lpsl->line_buff, sizeof(lpsl->line_buff),
				   format, args);
	va_end(args);
	FILE *file_handle = fopen(lpsl->file_name, "a+b");
	if (file_handle) {
		fwrite(lpsl->line_buff, 1, num_chars, file_handle);
		fclose(file_handle);
	}
}

bool caffeine_sample_logger_init(caffeine_sample_logger_t *lpsl,
				 const char *file_name)
{
	memset(lpsl, 0, sizeof(caffeine_sample_logger_t));
	strcpy_s(lpsl->file_name, sizeof(lpsl->file_name), file_name);
	FILE *file_handle = fopen(lpsl->file_name, "w+b");
	if (file_handle) {
		fclose(file_handle);
		lpsl->is_ok = true;
	}
	caffeine_sample_logger_log(lpsl,
				   "sample"
				   ",sent to libcaffeine"
				   ",reason"
				   ",wall time (ms)"
				   ",wall time delta (ms)"
				   ",sample obs timestamp (ms)"
				   ",sample obs timestamp delta (ms)"
				   ",sample obs timestamp delta from pair (ms)"
				   ",caffeine func time (ms)"
				   ",caffeine func time delta (ms)"
				   ",obs app time (ms)"
				   ",obs app time delta (ms)"
				   "\r\n");
	return lpsl->is_ok;
}

void caffeine_sample_logger_log_sample(
	caffeine_sample_logger_t *lpsl, bool sample_sent_to_libcaffeine,
	const char *reason_not_sent_to_libcaffeine, uint64_t wall_time_ns,
	uint64_t sample_obs_timestamp_ns, uint64_t pair_obs_last_timestamp_ns,
	uint64_t caffeine_func_time_ns, uint64_t obs_app_time_ns)
{
	double wall_time_ms = (double)wall_time_ns / 1000000.0;
	double sample_obs_timestamp_ms =
		(double)sample_obs_timestamp_ns / 1000000.0;
	double caffeine_func_time_ms =
		(double)caffeine_func_time_ns / 1000000.0;
	double obs_app_time_ms = (double)obs_app_time_ns / 1000000.0;
	const char *sample_sent_yn = (true == sample_sent_to_libcaffeine) ? "Y"
									  : "N";
	double pair_obs_last_timestamp_ms =
		(double)pair_obs_last_timestamp_ns / 1000000.0;
	double pair_obs_timestamp_delta_from_pair_ms =
		sample_obs_timestamp_ms - pair_obs_last_timestamp_ms;

	if (lpsl->sample_cnt == 0) {
		caffeine_sample_logger_log(
			lpsl, "%d,%s,%s,%0.3f,0,%0.3f,0,0,%0.3f,0,%0.3f,0\r\n",
			lpsl->sample_cnt, sample_sent_yn,
			reason_not_sent_to_libcaffeine, wall_time_ms,
			sample_obs_timestamp_ms, caffeine_func_time_ms,
			obs_app_time_ms);
	} else {
		double wall_time_ms_delta =
			wall_time_ms - lpsl->prev_wall_time_ms;
		double sample_obs_timestamp_ms_delta =
			sample_obs_timestamp_ms -
			lpsl->prev_sample_obs_timestamp_ms;
		double caffeine_func_time_ms_delta =
			caffeine_func_time_ms -
			lpsl->prev_caffeine_func_time_ms;
		double obs_app_time_ms_delta =
			obs_app_time_ms - lpsl->prev_obs_app_time_ms;
		caffeine_sample_logger_log(
			lpsl,
			"%d,%s,%s,%0.3f,%0.3f,%0.3f,%0.3f,%0.3f,%0.3f,%0.3f,%0.3f,%0.3f\r\n",
			lpsl->sample_cnt, sample_sent_yn,
			reason_not_sent_to_libcaffeine, wall_time_ms,
			wall_time_ms_delta, sample_obs_timestamp_ms,
			sample_obs_timestamp_ms_delta,
			pair_obs_timestamp_delta_from_pair_ms,
			caffeine_func_time_ms, caffeine_func_time_ms_delta,
			obs_app_time_ms, obs_app_time_ms_delta);
	}
	lpsl->prev_wall_time_ms = wall_time_ms;
	lpsl->prev_sample_obs_timestamp_ms = sample_obs_timestamp_ms;
	lpsl->prev_caffeine_func_time_ms = caffeine_func_time_ms;
	lpsl->prev_obs_app_time_ms = obs_app_time_ms;
	lpsl->sample_cnt++;
}
