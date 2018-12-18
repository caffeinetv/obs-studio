#include <obs-module.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include "obs-ffmpeg-formats.h"

#include <util/base.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>

#include <caffeine.h>

#include "caffeine-api.h"
#include "caffeine-foreground-process.h"
#include "caffeine-service.h"

#define CAFFEINE_LOG_TITLE "caffeine output"
#include "caffeine-log.h"

/* Uncomment this to log each call to raw_audio/video
#define TRACE_FRAMES
/**/

enum state {
	OFFLINE = 0,
	STARTING,
	ONLINE,
	STOPPING,
};

struct caffeine_output
{
	obs_output_t * output;
	caff_interface_handle interface;
	caff_stream_handle stream;
	struct caffeine_stream_info * stream_info;
	pthread_mutex_t stream_mutex;
	pthread_t broadcast_thread;
	struct obs_video_info video_info;

	pthread_cond_t screenshot_cond;
	pthread_mutex_t screenshot_mutex;
	bool screenshot_needed;
	AVPacket screenshot;

	volatile long state;
};

static const char *caffeine_get_name(void *data)
{
	UNUSED_PARAMETER(data);

	return obs_module_text("CaffeineOutput");
}

/* Converts caffeine-rtc (webrtc) log levels to OBS levels. NONE or unrecognized
 * values return 0 to indicate the message shouldn't be logged
 *
 * Note: webrtc uses INFO for debugging messages, not meant to be user-facing,
 * so this will never return LOG_INFO
 */
static int caffeine_to_obs_log_level(caff_log_severity severity)
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

static int caffeine_to_obs_error(caff_error error)
{
	switch (error)
	{
	case CAFF_ERROR_SDP_OFFER:
	case CAFF_ERROR_SDP_ANSWER:
	case CAFF_ERROR_ICE_TRICKLE:
	case CAFF_ERROR_BROADCAST_FAILED:
		return OBS_OUTPUT_CONNECT_FAILED;
	case CAFF_ERROR_DISCONNECTED:
		return OBS_OUTPUT_DISCONNECTED;
	case CAFF_ERROR_TAKEOVER:
	default:
		return OBS_OUTPUT_ERROR;
	}
}

caff_format obs_to_caffeine_format(enum video_format format)
{
	switch (format)
	{
	case VIDEO_FORMAT_I420:
		return CAFF_FORMAT_I420;
	case VIDEO_FORMAT_NV12:
		return CAFF_FORMAT_NV12;
	case VIDEO_FORMAT_YUY2:
		return CAFF_FORMAT_YUY2;
	case VIDEO_FORMAT_UYVY:
		return CAFF_FORMAT_UYVY;
	case VIDEO_FORMAT_BGRA:
		return CAFF_FORMAT_BGRA;

	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_I444:
	case VIDEO_FORMAT_Y800:
	case VIDEO_FORMAT_BGRX:
	case VIDEO_FORMAT_YVYU:
	default:
		return CAFF_FORMAT_UNKNOWN;
	}
}

/* TODO: figure out why caffeine-rtc isn't calling this */
static void caffeine_log(caff_log_severity severity, char const * message)
{
	int log_level = caffeine_to_obs_log_level(severity);
	if (log_level)
		blog(log_level, "[caffeine-rtc] %s", message);
}

static void *caffeine_create(obs_data_t *settings, obs_output_t *output)
{
	trace();
	UNUSED_PARAMETER(settings);

	struct caffeine_output *context = bzalloc(sizeof(struct caffeine_output));
	context->output = output;

	pthread_mutex_init(&context->stream_mutex, NULL);
	pthread_mutex_init(&context->screenshot_mutex, NULL);
	pthread_cond_init(&context->screenshot_cond, NULL);

	context->interface = caff_initialize(caffeine_log, CAFF_LOG_INFO);
	if (!context->interface) {
		log_error("Unable to initialize Caffeine interface");
		bfree(context);
		return NULL;
	}

	return context;
}

static inline enum state get_state(struct caffeine_output * context)
{
	return (enum state)os_atomic_load_long(&context->state);
}

static inline bool require_state(
	struct caffeine_output * context,
	enum state expected_state)
{
	enum state state = get_state(context);
	if (state != expected_state) {
		log_error("In state %d when expecting %d",
			state, expected_state);
		return false;
	}
	return true;
}

static inline void set_state(struct caffeine_output * context, enum state state)
{
	os_atomic_set_long(&context->state, (long)state);
}

static inline bool transition_state(
	struct caffeine_output * context,
	enum state old_state,
	enum state new_state)
{
	bool result = os_atomic_compare_swap_long(&context->state, (long)old_state,
					(long)new_state);
	if (!result)
		log_error("Transitioning to state %d expects state %d",
			new_state, old_state);
	return result;
}

static char const * caffeine_offer_generated(void * data, char const * sdp_offer);
static bool caffeine_ice_gathered(void *data, caff_ice_candidates candidates, size_t num_candidates);
static void caffeine_stream_started(void *data);
static void caffeine_stream_failed(void *data, caff_error error);

static int const enforced_height = 720;
static double const max_ratio = 3.0;
static double const min_ratio = 1.0/3.0;

static bool caffeine_start(void *data)
{
	trace();
	struct caffeine_output *context = data;

	if (!obs_get_video_info(&context->video_info)) {
		set_error(context->output, "Failed to get video info");
		return false;
	}

	if (context->video_info.output_height != enforced_height) {
		log_warn("For best video quality and reduced CPU usage, set "
			"output resolution to 720p");
	}

	double ratio = (double)context->video_info.output_width /
		context->video_info.output_height;
	if (ratio < min_ratio || ratio > max_ratio) {
		set_error(context->output, obs_module_text("ErrorAspectRatio"));
		return false;
	}

	caff_format format =
		obs_to_caffeine_format(context->video_info.output_format);

	if (format == CAFF_FORMAT_UNKNOWN) {
		set_error(context->output, "%s %s",
			obs_module_text("ErrorVideoFormat"),
			get_video_format_name(context->video_info.output_format));
		return false;
	}

	struct audio_convert_info conversion = {
		.format = AUDIO_FORMAT_16BIT,
		.speakers = SPEAKERS_STEREO,
		.samples_per_sec = 48000
	};
	obs_output_set_audio_conversion(context->output, &conversion);

	if (!obs_output_can_begin_data_capture(context->output, 0))
		return false;

	if (!transition_state(context, OFFLINE, STARTING))
		return false;

	pthread_mutex_lock(&context->screenshot_mutex);
	context->screenshot_needed = true;
	av_init_packet(&context->screenshot);
	context->screenshot.data = NULL;
	context->screenshot.size = 0;
	pthread_mutex_unlock(&context->screenshot_mutex);

	caff_stream_handle stream =
		caff_start_stream(context->interface, context,
			caffeine_offer_generated, caffeine_ice_gathered,
			caffeine_stream_started, caffeine_stream_failed);
	if (!stream) {
		set_state(context, OFFLINE);
		set_error(context->output, obs_module_text("ErrorStartStream"));
		return false;
	}

	pthread_mutex_lock(&context->stream_mutex);
	context->stream = stream;
	pthread_mutex_unlock(&context->stream_mutex);

	return true;
}

/* Called from another thread, blocking OK */
static char const * caffeine_offer_generated(
	void * data,
	char const * sdp_offer)
{
	trace();
	struct caffeine_output * context = data;

	if (!require_state(context, STARTING))
		return NULL;

	obs_service_t * service = obs_output_get_service(context->output);
	char const * stage_id =
		obs_service_query(service, CAFFEINE_QUERY_STAGE_ID);
	struct caffeine_credentials * creds =
		obs_service_query(service, CAFFEINE_QUERY_CREDENTIALS);

	struct caffeine_stream_info * stream_info =
		caffeine_start_stream(stage_id, sdp_offer, creds);

	pthread_mutex_lock(&context->stream_mutex);
	context->stream_info = stream_info;
	pthread_mutex_unlock(&context->stream_mutex);

	return stream_info ? stream_info->sdp_answer : NULL;
}

/* Called from another thread, blocking OK */
static bool caffeine_ice_gathered(
	void *data,
	caff_ice_candidates candidates,
	size_t num_candidates)
{
	trace();
	struct caffeine_output * context = data;

	obs_service_t * service = obs_output_get_service(context->output);
	struct caffeine_credentials * creds =
		obs_service_query(service, CAFFEINE_QUERY_CREDENTIALS);

	pthread_mutex_lock(&context->stream_mutex);
	struct caffeine_stream_info info = {
		.stream_id = bstrdup(context->stream_info->stream_id),
		.sdp_answer = bstrdup(context->stream_info->sdp_answer),
		.signed_payload = bstrdup(context->stream_info->signed_payload),
	};
	pthread_mutex_unlock(&context->stream_mutex);

	bool result = caffeine_trickle_candidates(
		candidates, num_candidates, creds, context->stream_info);

	bfree(info.stream_id);
	bfree(info.sdp_answer);
	bfree(info.signed_payload);

	return result;
}

static void * broadcast_thread(void * data);

static void caffeine_stream_started(void *data)
{
	trace();
	struct caffeine_output *context = data;

	if (!transition_state(context, STARTING, ONLINE)) {
		return;
	}

	obs_output_begin_data_capture(context->output, 0);

	pthread_create(&context->broadcast_thread, NULL, broadcast_thread, context);
}

static void caffeine_stop_stream(struct caffeine_output * context);

static void caffeine_stream_failed(void *data, caff_error error)
{
	struct caffeine_output *context = data;

	set_error(context->output, "%s: [%d] %s",
        obs_module_text("ErrorStartStream"),
        error,
        caff_error_string(error));

	set_state(context, STOPPING);
	caffeine_stop_stream(context);

	obs_output_signal_stop(context->output, caffeine_to_obs_error(error));
}

#define OBS_GAME_ID "79"

static char const * get_game_id(struct caffeine_games * games)
{
	char const * id = OBS_GAME_ID;
	char * foreground_process = get_foreground_process_name();
	if (games && foreground_process) {
		/* TODO: this is inside out; should have process name at toplevel */
		for (int game_index = 0; game_index < games->num_games; ++game_index) {
			struct caffeine_game_info * info =
				games->game_infos[game_index];
			if (!info)
				continue;
			for (int pname_index = 0; pname_index < info->num_process_names; ++pname_index) {
				char const * pname = info->process_names[pname_index];
				if (!pname)
					continue;
				if (strcmp(foreground_process, pname) == 0) {
					id = info->id;
					goto found;
				}
			}
		}
	}
found:
	bfree(foreground_process);
	return id;
}

static void * broadcast_thread(void * data)
{
	trace();
	os_set_thread_name("Caffeine broadcast");

	struct caffeine_output * context = data;

	pthread_mutex_lock(&context->stream_mutex);
	if (!require_state(context, ONLINE)) {
		pthread_mutex_unlock(&context->stream_mutex);
		return NULL;
	}
	char * stream_id = bstrdup(context->stream_info->stream_id);
	char * signed_payload = bstrdup(context->stream_info->signed_payload);
	pthread_mutex_unlock(&context->stream_mutex);

	obs_service_t * service = obs_output_get_service(context->output);
	char * stage_id = bstrdup(
		obs_service_query(service, CAFFEINE_QUERY_STAGE_ID));
	char * title = bstrdup(
		obs_service_query(service, CAFFEINE_QUERY_BROADCAST_TITLE));
	struct caffeine_credentials * creds =
		obs_service_query(service, CAFFEINE_QUERY_CREDENTIALS);
	enum caffeine_rating rating = (enum caffeine_rating)
		obs_service_query(service, CAFFEINE_QUERY_BROADCAST_RATING);

	struct caffeine_games * games = caffeine_get_supported_games();
	char const * game_id = get_game_id(games);

	struct caffeine_stage_response stage_response =
		set_stage_live(false, NULL, stage_id,
			stream_id, title, rating, game_id, creds);
	char * session_id = stage_response.session_id;
	if (!session_id) {
		caffeine_stream_failed(data, CAFF_ERROR_UNKNOWN);
		goto get_session_error;
	}

	pthread_mutex_lock(&context->screenshot_mutex);
	while (context->screenshot_needed)
		pthread_cond_wait(&context->screenshot_cond,
				&context->screenshot_mutex);
	pthread_mutex_unlock(&context->screenshot_mutex);

	char * broadcast_id =
		create_broadcast(title, rating, game_id,
			context->screenshot.data, context->screenshot.size,
			creds);
	if (broadcast_id == NULL) {
		caffeine_stream_failed(data, CAFF_ERROR_BROADCAST_FAILED);
		goto create_broadcast_error;
	}

	/* TODO: use wall time instead of accumulation of sleep time */
	long const heartbeat_interval = 5000; /* ms */
	long const check_interval = 100;

	long interval = heartbeat_interval;

	static int const max_failures = 5;
	int failures = 0;

	for (enum state state = get_state(context);
		state == ONLINE;
		os_sleep_ms(check_interval), state = get_state(context))
	{
		interval += check_interval;
		if (interval < heartbeat_interval)
			continue;

		interval = 0;

		game_id = get_game_id(games);
		stage_response = set_stage_live(true, session_id, stage_id,
				stream_id, title, rating, game_id, creds);
		if (stage_response.status_code == 403) {
			log_warn("%s", obs_module_text("StreamTakeover"));
			caffeine_stream_failed(data, CAFF_ERROR_TAKEOVER);
			goto taken_over;
		}

		update_broadcast(broadcast_id, true, title, rating, game_id,
				creds);

		if (send_heartbeat(stream_id, signed_payload, creds)) {
			failures = 0;
			continue;
		}
		log_debug("Heartbeat failed");

		++failures;
		if (failures > max_failures) {
			log_error("Heartbeat failed %d times; ending stream.",
				failures);
			caffeine_stream_failed(data, CAFF_ERROR_DISCONNECTED);
			break;
		}
	}

	set_stage_live(false, session_id, stage_id, stream_id, title, rating,
			game_id, creds);
	update_broadcast(broadcast_id, false, title, rating, game_id, creds);

taken_over:
	bfree(broadcast_id);
create_broadcast_error:
	bfree(session_id);
get_session_error:
	caffeine_free_game_list(&games);
	bfree(title);
	bfree(stage_id);
	bfree(signed_payload);
	bfree(stream_id);
	return NULL;
}

static void create_screenshot(
	struct caffeine_output * context,
	uint32_t width,
	uint32_t height,
	uint8_t *image_data[MAX_AV_PLANES],
	uint32_t image_data_linesize[MAX_AV_PLANES],
	enum video_format format);

static void caffeine_raw_video(void *data, struct video_data *frame)
{
#ifdef TRACE_FRAMES
	trace();
#endif
	struct caffeine_output *context = data;

	uint32_t width = context->video_info.output_width;
	uint32_t height = context->video_info.output_height;
	size_t total_bytes = frame->linesize[0] * height;
	caff_format caff_format =
		obs_to_caffeine_format(context->video_info.output_format);

	pthread_mutex_lock(&context->screenshot_mutex);
	if (context->screenshot_needed)
		create_screenshot(context, width, height, frame->data,
			frame->linesize, context->video_info.output_format);
	pthread_mutex_unlock(&context->screenshot_mutex);

	pthread_mutex_lock(&context->stream_mutex);
	if (context->stream)
		caff_send_video(context->stream, frame->data[0], total_bytes,
			width, height, caff_format);
	pthread_mutex_unlock(&context->stream_mutex);
}

/* Called while screenshot_mutex is locked */
/* Adapted from https://github.com/obsproject/obs-studio/blob/3ddca5863c4d1917ad8443a9ad288f41accf9e39/UI/window-basic-main.cpp#L1741 */
static void create_screenshot(
	struct caffeine_output * context,
	uint32_t width,
	uint32_t height,
	uint8_t *image_data[MAX_AV_PLANES],
	uint32_t image_data_linesize[MAX_AV_PLANES],
	enum video_format format)
{
	trace();

	AVCodec           *codec         = NULL;
	AVCodecContext    *codec_context = NULL;
	AVFrame           *frame         = NULL;
	struct SwsContext *sws_context   = NULL;
	int               got_output     = 0;
	int               ret            = 0;

	if (image_data == NULL) {
		log_warn("No image data for screenshot");
		goto err_no_image_data;
	}

	// Write JPEG output using libavcodec
	codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);

	if (codec == NULL) {
		log_warn("Unable to load screenshot encoder");
		goto err_jpeg_codec_not_found;
	}

	codec_context = avcodec_alloc_context3(codec);

	if (codec_context == NULL) {
		log_warn("Couldn't allocate codec context");
		goto err_jpeg_encoder_context_alloc;
	}

	codec_context->width = width;
	codec_context->height = height;
	codec_context->pix_fmt = AV_PIX_FMT_YUVJ422P;
	codec_context->time_base.num = 1;
	codec_context->time_base.den = 30;
	codec_context->bit_rate = 10000000;
	codec_context->codec_id = codec->id;
	codec_context->codec_type = AVMEDIA_TYPE_VIDEO;

	if (avcodec_open2(codec_context, codec, NULL) != 0) {
		log_warn("Couldn't open codec");
		goto err_jpeg_encoder_open;
	}

	frame = av_frame_alloc();

	if (frame == NULL) {
		log_warn("Couldn't allocate frame");
		goto err_av_frame_alloc;
	}

	frame->pts = 1;
	frame->format = AV_PIX_FMT_YUVJ422P;
	frame->width = width;
	frame->height = height;

	ret = av_image_alloc(
		frame->data,
		frame->linesize,
		codec_context->width,
		codec_context->height,
		codec_context->pix_fmt,
		32);

	if (ret < 0) {
		log_warn("Couldn't allocate image");
		goto err_av_image_alloc;
	}

	enum AVPixelFormat src_format = obs_to_ffmpeg_video_format(format);

	// Copy image data, converting RGBA to
	// image format expected by JPEG encoder
	sws_context = sws_getContext(
			frame->width,
			frame->height,
			src_format,
			frame->width,
			frame->height,
			codec_context->pix_fmt,
			0,
			NULL,
			NULL,
			NULL);

	if (sws_context == NULL) {
		log_warn("Couldn't get scaling context");
		goto err_sws_getContext;
	}

	// Transform RGBA to RGB24
	ret = sws_scale(
			sws_context,
			image_data,
			image_data_linesize,
			0,
			frame->height,
			frame->data,
			frame->linesize);

	if (ret < 0) {
		log_warn("Couldn't translate image format");
		goto err_sws_scale;
	}

	av_init_packet(&context->screenshot);
	context->screenshot.data = NULL;
	context->screenshot.size = 0;

	ret = avcodec_encode_video2(codec_context, &context->screenshot,
		frame, &got_output);

	if (ret != 0 || !got_output) {
		log_warn("Failed to generate screenshot. avcodec_encode_video2"
			  " returned %d", ret);
		goto err_encode;
	}

err_encode:
err_sws_scale:
	sws_freeContext(sws_context);
	sws_context = NULL;
err_sws_getContext:
	av_freep(frame->data);
	frame->data[0] = NULL;
err_av_image_alloc:
	av_frame_free(&frame);
	frame = NULL;
err_av_frame_alloc:
	avcodec_close(codec_context);
err_jpeg_encoder_open:
	avcodec_free_context(&codec_context);
	codec_context = NULL;
err_jpeg_encoder_context_alloc:
err_jpeg_codec_not_found:
err_no_image_data:

	context->screenshot_needed = false;
	pthread_cond_signal(&context->screenshot_cond);
}

static void caffeine_raw_audio(void *data, struct audio_data *frames)
{
#ifdef TRACE_FRAMES
	trace();
#endif
	struct caffeine_output *context = data;

	pthread_mutex_lock(&context->stream_mutex);
	if (context->stream)
		caff_send_audio(context->stream, frames->data[0],
			frames->frames);
	pthread_mutex_unlock(&context->stream_mutex);
}

static void caffeine_stop_stream(struct caffeine_output * context)
{
	trace();
	pthread_mutex_lock(&context->stream_mutex);
	pthread_mutex_lock(&context->screenshot_mutex);

	if (context->stream)
		caff_end_stream(&context->stream);

	caffeine_free_stream_info(&context->stream_info);

	if (context->screenshot.data != NULL) {
		av_free_packet(&context->screenshot);
	}

	context->screenshot_needed = false;

	pthread_mutex_unlock(&context->screenshot_mutex);
	pthread_mutex_unlock(&context->stream_mutex);

	set_state(context, OFFLINE);
}

static void caffeine_stop(void *data, uint64_t ts)
{
	trace();
	/* TODO: do something with this? */
	UNUSED_PARAMETER(ts);

	struct caffeine_output *context = data;
	obs_output_t *output = context->output;

	set_state(context, STOPPING);
	pthread_join(context->broadcast_thread, NULL);

	caffeine_stop_stream(context);

	obs_output_end_data_capture(output);
}

static void caffeine_destroy(void *data)
{
	trace();
	struct caffeine_output *context = data;
	caff_deinitialize(&context->interface);
	pthread_mutex_destroy(&context->stream_mutex);
	pthread_mutex_destroy(&context->screenshot_mutex);
	pthread_cond_destroy(&context->screenshot_cond);

	bfree(data);
}

char const * caffeine_get_username(void * data)
{
	trace();
	struct caffeine_output *context = data;
	struct obs_service_t *service = obs_output_get_service(context->output);
	return obs_service_query(service, CAFFEINE_QUERY_USERNAME);
}

struct obs_output_info caffeine_output_info = {
	.id        = "caffeine_output",
	.flags     = OBS_OUTPUT_AV | OBS_OUTPUT_SERVICE,
	.get_name  = caffeine_get_name,
	.create    = caffeine_create,
	.start     = caffeine_start,
	.raw_video = caffeine_raw_video,
	.raw_audio = caffeine_raw_audio,
	.stop      = caffeine_stop,
	.destroy   = caffeine_destroy,
	.get_username = caffeine_get_username,
};
