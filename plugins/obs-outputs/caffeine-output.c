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

static void *caffeine_create(obs_data_t *settings, obs_output_t *output)
{
	UNUSED_PARAMETER(settings);

	struct caffeine_output *stream = bzalloc(sizeof(struct caffeine_output));
	stream->output = output;

	info("caffeine_create");

	info("TestStub: %d", (int)TestStub()); /* temp - make sure we can call into caffeine-rtc */
	/* TODO */

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

	info("caffeine_raw_video");
	/* TODO */
}

static void caffeine_raw_audio(void *data, struct audio_data *frames)
{
	UNUSED_PARAMETER(frames);

	struct caffeine_output *stream = data;

	info("caffeine_raw_audio");
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

	/* These are supposed to be optional for non-encoded streams but stream
	   fails to start if they're omitted */
	.encoded_video_codecs = "h264",
	.encoded_audio_codecs = "opus",
};
