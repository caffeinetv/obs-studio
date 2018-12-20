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

struct caffeine_broadcast_info {
	char * stream_url;
	char * feed_id;
	struct caffeine_stage_request * next_request;
	bool is_mutating_feed;
};

static void caffeine_free_broadcast_info(struct caffeine_broadcast_info ** info)
{
	if (!info || !*info) {
		return;
	}

	bfree((*info)->stream_url);
	bfree((*info)->feed_id);
	caffeine_free_stage_request(&(*info)->next_request);
	bfree(*info);

	*info = NULL;
}

struct caffeine_output
{
	obs_output_t * output;
	caff_interface_handle interface;
	caff_stream_handle stream;
	struct caffeine_broadcast_info * broadcast_info;
	pthread_mutex_t stream_mutex;
	pthread_t broadcast_thread;
	pthread_t longpoll_thread;
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

static void transfer_stage_data(
	struct caffeine_stage_response ** from_response,
	struct caffeine_stage_request * to_request)
{
	bfree(to_request->cursor);
	caffeine_free_stage(&to_request->stage);
	to_request->cursor = (*from_response)->cursor;
	to_request->stage = (*from_response)->stage;
	(*from_response)->cursor = NULL;
	(*from_response)->stage = NULL;
	caffeine_free_stage_response(from_response);
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
		set_error(context->output, "%s",
			obs_module_text("ErrorAspectRatio"));
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
		set_error(context->output, "%s",
			obs_module_text("ErrorStartStream"));
		return false;
	}

	pthread_mutex_lock(&context->stream_mutex);
	context->stream = stream;
	pthread_mutex_unlock(&context->stream_mutex);

	return true;
}

static char * caffeine_generate_unique_id()
{
	const int id_length = 12;
	const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";

	struct dstr id;
	dstr_init(&id);
	dstr_reserve(&id, id_length + 1);

	for (int i = 0; i < id_length; ++i) {
		int random_index = rand() % (sizeof(charset) - 1);
		char character = charset[random_index];
		dstr_cat_ch(&id, character);
	}

	return id.array;
}

static void caffeine_set_string(char ** source, char const * new_value) {
	bfree(*source);
	*source = bstrdup(new_value);
}

// If successful, updates request stage with the response values
static bool caffeine_request_stage_update(
	struct caffeine_stage_request * request,
	struct caffeine_credentials * creds,
	double * retry_in)
{
	struct caffeine_stage_response_result * result =
		caffeine_stage_update(*request, creds);

	bool success = result && result->response;
	if (success) {
		if (retry_in) {
			*retry_in = result->response->retry_in;
		}
		transfer_stage_data(&result->response, request);
	}

	caffeine_free_stage_response_result(&result);

	return success;
}

/* Called from another thread, blocking OK */
static char const * caffeine_offer_generated(
	void * data,
	char const * sdp_offer)
{
	trace();
	struct caffeine_output * context = data;

	char * title = NULL;
	char * sdp_answer = NULL;
	char * feed_id = caffeine_generate_unique_id();

	if (!require_state(context, STARTING))
		goto setup_error;

	obs_service_t * service = obs_output_get_service(context->output);
	struct caffeine_credentials * creds =
		obs_service_query(service, CAFFEINE_QUERY_CREDENTIALS);
	char * username = bstrdup(
		obs_service_query(service, CAFFEINE_QUERY_USERNAME));
	char * raw_title = obs_service_query(service, CAFFEINE_QUERY_BROADCAST_TITLE);
	enum caffeine_rating rating = (enum caffeine_rating)
		obs_service_query(service, CAFFEINE_QUERY_BROADCAST_RATING);

	title = caffeine_annotate_title(raw_title, rating);
	char * client_id = caffeine_generate_unique_id();

	// Make initial stage request to get a cursor

	struct caffeine_stage_request * request =
		bzalloc(sizeof(struct caffeine_stage_request));
	request->username = username;
	request->client_id = client_id;

	if (!caffeine_request_stage_update(request, creds, NULL)) {
		goto setup_error;
	}

	// Make request to add our feed and get a new broadcast id

	caffeine_set_string(&request->stage->title, title);
	request->stage->upsert_broadcast = true;
	caffeine_set_string(&request->stage->broadcast_id, NULL);
	request->stage->live = false;

	struct caffeine_feed feed = {
		.id = feed_id,
		.client_id = client_id,
		.role = "primary",
		.volume = 1.0,
		.capabilities = {
			.video = true,
			.audio = true
		},
		.stream = {
			.sdp_offer = (char *)sdp_offer
		}
	};
	caffeine_set_stage_feed(request->stage, &feed);

	if (!caffeine_request_stage_update(request, creds, NULL)) {
		goto setup_error;
	}

	// Get stream details

	struct caffeine_feed * response_feed =
		caffeine_get_stage_feed(request->stage, feed_id);
	if (!response_feed
	    || !response_feed->stream.sdp_answer
	    || !response_feed->stream.url) {
		goto setup_error;
	}

	sdp_answer = response_feed->stream.sdp_answer;

	struct caffeine_broadcast_info * broadcast_info =
		bzalloc(sizeof(struct caffeine_broadcast_info));

	broadcast_info->stream_url = bstrdup(response_feed->stream.url);
	broadcast_info->feed_id = feed_id;
	broadcast_info->next_request = request;

	pthread_mutex_lock(&context->stream_mutex);
	context->broadcast_info = broadcast_info;
	pthread_mutex_unlock(&context->stream_mutex);

setup_error:
	if (!sdp_answer) {
		caffeine_free_stage_request(&request);
		bfree(feed_id);
	}

	bfree(title);

	return sdp_answer;
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
	char * stream_url = bstrdup(context->broadcast_info->stream_url);
	pthread_mutex_unlock(&context->stream_mutex);

	bool result = caffeine_trickle_candidates(
		candidates, num_candidates, stream_url, creds);

	bfree(stream_url);

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

static char const * get_game_id(struct caffeine_games * games, char * const process_name)
{
	if (games && process_name) {
		for (size_t game_index = 0; game_index < games->num_games; ++game_index) {
			struct caffeine_game_info * info =
				games->game_infos[game_index];
			if (!info)
				continue;
			for (size_t pname_index = 0; pname_index < info->num_process_names; ++pname_index) {
				char const * pname = info->process_names[pname_index];
				if (!pname)
			    		continue;
				if (strcmp(process_name, pname) == 0) {
			    		return info->id;
				}
			}
		}
	}

	return NULL;
}

// Falls back to obs_id if no foreground game detected
static char const * get_running_game_id(
    struct caffeine_games * games, const char * obs_id)
{
	char * foreground_process = get_foreground_process_name();
	char const * id = get_game_id(games, foreground_process);
	bfree(foreground_process);
	return id ? id : obs_id;
}

// Returns `true` if the feed's game id changed
static bool caffeine_update_game_id(char const * game_id, struct caffeine_feed * feed)
{
	if (!feed) {
		return false;
	}

	bool did_change = false;

	if (game_id) {
		if (!feed->content.id || strcmp(feed->content.id, game_id) != 0) {
			caffeine_set_string(&feed->content.id, game_id);
			did_change = true;
		}

		if (!feed->content.type) {
			caffeine_set_string(&feed->content.type, "game");
			did_change = true;
		}
	} else if (feed->content.id || feed->content.type) {
		caffeine_set_string(&feed->content.id, NULL);
		caffeine_set_string(&feed->content.type, NULL);
		did_change = true;
    	}

	return did_change;
}

// Returns `true` if the feed's connection quality changed
static bool caffeine_update_connection_quality(
	char const * quality, struct caffeine_feed * feed)
{
	if (!quality) {
		return false;
	}

	if (!feed->source_connection_quality
	    || strcmp(quality, feed->source_connection_quality) != 0)
	{
		caffeine_set_string(&feed->source_connection_quality, quality);
		return true;
	}

	return false;
}

static void * longpoll_thread(void * data);

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

	char * feed_id = bstrdup(context->broadcast_info->feed_id);
	char * stream_url = bstrdup(context->broadcast_info->stream_url);
	struct caffeine_stage_request * request =
		context->broadcast_info->next_request;
	context->broadcast_info->next_request = NULL;
	pthread_mutex_unlock(&context->stream_mutex);

	obs_service_t * service = obs_output_get_service(context->output);
	struct caffeine_credentials * creds =
		obs_service_query(service, CAFFEINE_QUERY_CREDENTIALS);

	struct caffeine_games * games = caffeine_get_supported_games();
	char const * obs_game_id = get_game_id(games, "obs");

	// Obtain broadcast id

	char * broadcast_id = request->stage->broadcast_id;

	for (int broadcast_id_retry_count = 0; !broadcast_id && broadcast_id_retry_count < 3; ++broadcast_id_retry_count) {
		request->stage->upsert_broadcast = true;
		if (!caffeine_request_stage_update(request, creds, NULL)
		    || !caffeine_get_stage_feed(request->stage, feed_id))
		{
			caffeine_stream_failed(data, CAFF_ERROR_UNKNOWN);
			goto broadcast_error;
		}

		broadcast_id = request->stage->broadcast_id;
	}

	pthread_mutex_lock(&context->screenshot_mutex);
	while (context->screenshot_needed)
		pthread_cond_wait(&context->screenshot_cond,
				&context->screenshot_mutex);
	pthread_mutex_unlock(&context->screenshot_mutex);

	bool screenshot_success = caffeine_update_broadcast_screenshot(
		broadcast_id,
		context->screenshot.data,
		context->screenshot.size,
		creds);

	if (!screenshot_success) {
		caffeine_stream_failed(data, CAFF_ERROR_BROADCAST_FAILED);
		goto broadcast_error;
	}

	// Set stage live with current game content

	caffeine_update_game_id(
		get_running_game_id(games, obs_game_id),
		caffeine_get_stage_feed(request->stage, feed_id));
	request->stage->live = true;

	if (!caffeine_request_stage_update(request, creds, NULL)
	    || !request->stage->live
	    || !caffeine_get_stage_feed(request->stage, feed_id))
	{
		caffeine_stream_failed(data, CAFF_ERROR_BROADCAST_FAILED);
		goto broadcast_error;
	}

	pthread_mutex_lock(&context->stream_mutex);
	context->broadcast_info->next_request = request;
	request = NULL;
	pthread_mutex_unlock(&context->stream_mutex);

	pthread_create(
		&context->longpoll_thread, NULL, longpoll_thread, context);

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

		caffeine_free_stage_request(&request);

		pthread_mutex_lock(&context->stream_mutex);
		request = caffeine_copy_stage_request(context->broadcast_info->next_request);
		pthread_mutex_unlock(&context->stream_mutex);

		if (!request) {
			caffeine_stream_failed(data, CAFF_ERROR_UNKNOWN);
			goto broadcast_error;
		}

		struct caffeine_feed * feed = caffeine_get_stage_feed(request->stage, feed_id);
		if (!feed || !request->stage->live) {
			caffeine_stream_failed(data, CAFF_ERROR_TAKEOVER);
			goto broadcast_error;
		}

		bool should_mutate_feed = false;

		// Heartbeat stream

		struct caffeine_heartbeat_response * heartbeat_response =
			caffeine_heartbeat_stream(stream_url, creds);

		if (heartbeat_response) {
			should_mutate_feed =
				caffeine_update_connection_quality(
					heartbeat_response->connection_quality,
					feed);
			failures = 0;
			caffeine_free_heartbeat_response(&heartbeat_response);
		} else {
			log_debug("Heartbeat failed");
			++failures;
			if (failures > max_failures) {
				log_error("Heartbeat failed %d times; ending stream.",
						  failures);
				caffeine_stream_failed(data, CAFF_ERROR_UNKNOWN);
				break;
			}
		}

		should_mutate_feed =
			caffeine_update_game_id(
				get_running_game_id(games, obs_game_id), feed)
			|| should_mutate_feed;

		if (!should_mutate_feed) {
			continue;
		}

		// Mutate the feed

		pthread_mutex_lock(&context->stream_mutex);
		context->broadcast_info->is_mutating_feed = true;
		pthread_mutex_unlock(&context->stream_mutex);

		if (!caffeine_request_stage_update(request, creds, NULL)) {
			caffeine_stream_failed(data, CAFF_ERROR_BROADCAST_FAILED);
			goto broadcast_error;
		}

		if (!request->stage->live
		    || !caffeine_get_stage_feed(request->stage, feed_id))
		{
			caffeine_stream_failed(data, CAFF_ERROR_TAKEOVER);
			goto broadcast_error;
		}

		pthread_mutex_lock(&context->stream_mutex);
		context->broadcast_info->is_mutating_feed = false;
		caffeine_free_stage_request(&context->broadcast_info->next_request);
		context->broadcast_info->next_request = request;
		request = NULL;
		pthread_mutex_unlock(&context->stream_mutex);
	}

	pthread_mutex_lock(&context->stream_mutex);
	if (context->broadcast_info) {
		context->broadcast_info->is_mutating_feed = true;
		caffeine_free_stage_request(&request);
		request = context->broadcast_info->next_request;
		context->broadcast_info->next_request = NULL;
	}
	pthread_mutex_unlock(&context->stream_mutex);

	// Only set the stage offline if it contains our feed
	if (request && caffeine_get_stage_feed(request->stage, feed_id)) {
		request->stage->live = false;
		caffeine_clear_stage_feeds(request->stage);

		if (!caffeine_request_stage_update(request, creds, NULL)) {
			caffeine_stream_failed(data, CAFF_ERROR_UNKNOWN);
		}
	}

broadcast_error:
	bfree(stream_url);
	bfree(feed_id);
	caffeine_free_stage_request(&request);
	caffeine_free_game_list(&games);
	return NULL;
}

static void * longpoll_thread(void * data)
{
	// This thread is purely for hearbeating our feed.
	// If the broadcast thread is making a mutating, this thread holds off.

	trace();
	os_set_thread_name("Caffeine broadcast longpoll");

	struct caffeine_output * context = data;

	obs_service_t * service = obs_output_get_service(context->output);
	struct caffeine_credentials * creds =
		obs_service_query(service, CAFFEINE_QUERY_CREDENTIALS);

	long retry_interval = 0; /* ms */
	long const check_interval = 100;

	long interval = retry_interval;

	char * feed_id = NULL;

	pthread_mutex_lock(&context->stream_mutex);
	if (context->broadcast_info) {
		feed_id = bstrdup(context->broadcast_info->feed_id);
	}
	pthread_mutex_unlock(&context->stream_mutex);

	if (!feed_id) {
		return NULL;
	}

	for (enum state state = get_state(context);
	     state == ONLINE;
	     os_sleep_ms(check_interval), state = get_state(context))
	{
		interval += check_interval;
		if (interval < retry_interval)
			continue;

		struct caffeine_stage_request * request = NULL;

		pthread_mutex_lock(&context->stream_mutex);
		if (context->broadcast_info) {
			if (context->broadcast_info->is_mutating_feed) {
				pthread_mutex_unlock(&context->stream_mutex);
				continue;
			}
			request = caffeine_copy_stage_request(
				context->broadcast_info->next_request);
		}
		pthread_mutex_unlock(&context->stream_mutex);

		if (!request) {
			break;
		}

		double retry_in = 0;

		bool did_update = caffeine_request_stage_update(
			request, creds, &retry_in);

		bool live_feed_is_still_present =
			request->stage->live
			&& caffeine_get_stage_feed(request->stage, feed_id);

		pthread_mutex_lock(&context->stream_mutex);
		if (context->broadcast_info) {
			caffeine_free_stage_request(
				&context->broadcast_info->next_request);
			if (did_update) {
				context->broadcast_info->next_request = request;
				request = NULL;
			}
		}
		pthread_mutex_unlock(&context->stream_mutex);

		caffeine_free_stage_request(&request);

		if (!did_update || !live_feed_is_still_present) {
			break;
		}

		interval = 0;
		retry_interval = (long)(retry_in * 1000);
	}

	bfree(feed_id);

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

	caffeine_free_broadcast_info(&context->broadcast_info);

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
	pthread_join(context->longpoll_thread, NULL);
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
	obs_service_t *service = obs_output_get_service(context->output);
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
