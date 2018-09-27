#include <obs.h>
#include <obs-module.h>

#include "caffeine.h"

#define do_log(level, format, ...) \
	blog(level, "[caffeine output: '%s'] " format, \
			obs_output_get_name(stream->output), ##__VA_ARGS__)

#define warn(format, ...)  do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...)  do_log(LOG_INFO, format, ##__VA_ARGS__)

struct caffeine_output
{
	obs_output_t *output;
};

static const char *caffeine_get_name(void *data)
{
	UNUSED_PARAMETER(data);

	blog(LOG_INFO, "caffeine_get_name");

	return obs_module_text("CaffeineOutput"); // TODO localize
}

/* Converts caffeine-rtc (webrtc) log levels to OBS levels. NONE or unrecognized
 * values return 0 to indicate the message shouldn't be logged
 *
 * Note: webrtc uses INFO for debugging messages, not meant to be user-facing,
 * so this will never return LOG_INFO
 */
static int caffeine_to_obs_log_level(enum caff_log_severity severity)
{
	switch (severity)
	{
	case CAFF_LOG_SENSITIVE:
	case CAFF_LOG_VERBOSE:
	case CAFF_LOG_INFO:
		return LOG_DEBUG;
	case CAFF_LOG_WARNING:
		return LOG_WARNING;
	case CAFF_LOG_ERROR:
		return LOG_ERROR;
	case CAFF_LOG_NONE:
	default:
		return 0;
	}
}

/* Log sink for caffeine-rtc */
static void caffeine_log(enum caff_log_severity severity, char const * message)
{
	int log_level = caffeine_to_obs_log_level(severity);
	if (log_level)
		blog(log_level, "[caffeine-rtc] %s", message);
}

static void *caffeine_create(obs_data_t *settings, obs_output_t *output)
{
	UNUSED_PARAMETER(settings);

	struct caffeine_output *stream = bzalloc(sizeof(struct caffeine_output));
	stream->output = output;

	info("caffeine_create");

	caff_initialize(caffeine_log, CAFF_LOG_INFO);

	return stream;
}

static void caffeine_destroy(void *data)
{
	struct caffeine_output *stream = data;

	info("caffeine_destroy");
	/* TODO */

	bfree(data);
}

static bool caffeine_start(void *data)
{
	struct caffeine_output *stream = data;

	info("caffeine_start");

	if (!obs_output_can_begin_data_capture(stream->output, 0))
		return false;

	/* TODO: do get the service, set up stream with broadcast name etc.
	 * Most of this work should be on separate thread
	 */

	obs_output_begin_data_capture(stream->output, 0);

	return true;
}

static void caffeine_stop(void *data, uint64_t ts)
{
	UNUSED_PARAMETER(ts);

	struct caffeine_output *stream = data;

	info("caffeine_stop");

	/* TODO: teardown with service; do something with ts? */

	obs_output_end_data_capture(stream->output);
}

static void caffeine_raw_video(void *data, struct video_data *frame)
{
	UNUSED_PARAMETER(frame);

	struct caffeine_output *stream = data;
	/* TODO */
}

static void caffeine_raw_audio(void *data, struct audio_data *frames)
{
	UNUSED_PARAMETER(frames);

	struct caffeine_output *stream = data;
	/* TODO */
}

struct obs_output_info caffeine_output_info = {
	.id        = "caffeine_output",
	.flags     = OBS_OUTPUT_AV,  /* TODO: OBS_OUTPUT_SERVICE for login info, etc*/
	.get_name  = caffeine_get_name,
	.create    = caffeine_create,
	.destroy   = caffeine_destroy,
	.start     = caffeine_start,
	.stop      = caffeine_stop,
	.raw_video = caffeine_raw_video,
	.raw_audio = caffeine_raw_audio,
};
