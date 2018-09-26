#pragma once

#ifndef CAFFEINE_LOG_TITLE
#error "Define CAFFEINE_LOG_TITLE before including caffeine-log.h"
#endif

#define do_log(level, format, ...) \
	blog(level, "[" CAFFEINE_LOG_TITLE "] " format, ##__VA_ARGS__)

#define log_error(format, ...)  do_log(LOG_ERROR, format, ##__VA_ARGS__)
#define log_warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define log_info(format, ...)  do_log(LOG_INFO, format, ##__VA_ARGS__)
#define log_debug(format, ...)  do_log(LOG_DEBUG, format, ##__VA_ARGS__)

#define trace() log_debug("%s", __func__)
