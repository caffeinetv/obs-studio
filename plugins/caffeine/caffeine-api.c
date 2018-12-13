#include "caffeine-api.h"

#include <obs.h>

#include <curl/curl.h>
#include <jansson.h>
#include <util/dstr.h>
#include <util/threading.h>
#include <util/platform.h>

#define CAFFEINE_LOG_TITLE "caffeine api"
#include "caffeine-log.h"

/* TODO: load these from config? */
#define API_ENDPOINT       "https://api.caffeine.tv/"
#define REALTIME_ENDPOINT  "https://realtime.caffeine.tv/"

/* TODO: some of these are deprecated */
#define SIGNIN_URL         API_ENDPOINT  "v1/account/signin"
#define REFRESH_TOKEN_URL  API_ENDPOINT  "v1/account/token"
#define GETGAMES_URL       API_ENDPOINT  "v1/games"
#define GETUSER_URL_F      API_ENDPOINT  "v1/users/%s"
#define STREAM_URL         API_ENDPOINT  "v2/broadcasts/streams"
#define TRICKLE_URL_F      API_ENDPOINT  "v2/broadcasts/streams/%s"
#define BROADCAST_URL      API_ENDPOINT  "v1/broadcasts"
#define BROADCAST_URL_F    BROADCAST_URL "/%s"
#define HEARTBEAT_URL_F    API_ENDPOINT  "v2/broadcasts/streams/%s/heartbeat"

#define STAGE_STATE_URL_F  REALTIME_ENDPOINT "v2/stages/%s/details"
#define STAGE_UPDATE_URL_F REALTIME_ENDPOINT "v4/stage/%s"

#define CONTENT_TYPE_JSON  "Content-Type: application/json"
#define CONTENT_TYPE_FORM  "Content-Type: multipart/form-data"

/*
 * Request type: GET, PUT, POST, PATCH
 *
 * Request body format: Form data, JSON
 *
 * Request data itself
 *
 * URL, URL_F
 *
 * Basic, Authenticated
 *
 * Result type: new object/NULL, success/failure
 *
 * Error type: (various json formats, occasionally HTML e.g. for 502)
 */

struct caffeine_credentials {
	char * access_token;
	char * refresh_token;
	char * caid;
	char * credential;

	pthread_mutex_t mutex;
};

static size_t caffeine_curl_write_callback(char * ptr, size_t size,
	size_t nmemb, void * user_data)
{
	UNUSED_PARAMETER(size);
	if (nmemb == 0)
		return 0;

	struct dstr * result_str = user_data;
	dstr_ncat(result_str, ptr, nmemb);
	return nmemb;
}

static struct curl_slist * caffeine_basic_headers(char const * content_type) {
	struct curl_slist * headers = NULL;
	headers = curl_slist_append(headers, content_type);
	headers = curl_slist_append(headers, "X-Client-Type: obs");
	headers = curl_slist_append(headers, "X-Client-Version: " OBS_VERSION);
	return headers;
}

static struct curl_slist * caffeine_authenticated_headers(
	char const * content_type,
	struct caffeine_credentials * creds)
{
	pthread_mutex_lock(&creds->mutex);
	struct dstr header;
	dstr_init(&header);
	struct curl_slist * headers = caffeine_basic_headers(content_type);

	dstr_printf(&header, "Authorization: Bearer %s", creds->access_token);
	headers = curl_slist_append(headers, header.array);

	dstr_printf(&header, "X-Credential: %s", creds->credential);
	headers = curl_slist_append(headers, header.array);

	dstr_free(&header);
	pthread_mutex_unlock(&creds->mutex);
	return headers;
}

static struct caffeine_credentials * make_credentials(
	char const * access_token,
	char const * caid,
	char const * refresh_token,
	char const * credential)
{
	struct caffeine_credentials * creds =
		bzalloc(sizeof(struct caffeine_credentials));

	creds->access_token = bstrdup(access_token);
	creds->caid = bstrdup(caid);
	creds->refresh_token = bstrdup(refresh_token);
	creds->credential = bstrdup(credential);
	pthread_mutex_init(&creds->mutex, NULL);

	return creds;
}

char const * caffeine_refresh_token(struct caffeine_credentials * creds)
{
	return creds->refresh_token;
}

#define RETRY_MAX 3

/* TODO: this is not very robust or intelligent. Some kinds of failure will
 * never be recoverable and should not be retried
 */
#define retry_request(result_t, request) \
	for (int try_num = 0; try_num < RETRY_MAX; ++try_num) { \
		result_t result = request; \
		if (result) \
			return result; \
		os_sleep_ms(1000 + 1000 * try_num); \
	} \
	return (result_t)0

/* TODO: refactor this - lots of dupe code between request types
 * TODO: reuse curl handle across requests
 */
static struct caffeine_auth_response * do_caffeine_signin(
	char const * username,
	char const * password,
	char const * otp)
{
	trace();
	struct caffeine_auth_response * response = NULL;

	json_t * request_json = NULL;
	if (otp)
		request_json = json_pack("{s:{s:s,s:s},s:{s:s}}",
			"account", "username", username, "password", password,
			"mfa", "otp", otp);
	else
		request_json = json_pack("{s:{s:s,s:s}}",
			"account", "username", username, "password", password);

	if (!request_json)
	{
		log_error("Failed to create request JSON");
		goto request_json_error;
	}

	char * request_body = json_dumps(request_json, 0);
	if (!request_body)
	{
		log_error("Failed to serialize request JSON");
		goto request_serialize_error;
	}

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		goto curl_init_error;
	}

	curl_easy_setopt(curl, CURLOPT_URL, SIGNIN_URL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);

	struct curl_slist *headers = caffeine_basic_headers(CONTENT_TYPE_JSON);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure signing in: [%d] %s", res, curl_error);
		goto request_error;
	}

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse signin response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:{s:[s!]}}",
		"errors", "_error", &error_text);
	if (error_result == 0) {
		log_error("Error logging in: %s", error_text);
		goto json_parsed_error;
	}

	char const *access_token = NULL;
	char const *refresh_token = NULL;
	char const *caid = NULL;
	char const *credential = NULL;
	char const *next = NULL;
	char const *mfa_otp_method = NULL;
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s?:{s:s,s:s,s:s,s:s}, s?:s, s?:s}",
		"credentials",
		"access_token", &access_token,
		"refresh_token", &refresh_token,
		"caid", &caid,
		"credential", &credential,

		"next", &next,

		"mfa_otp_method", &mfa_otp_method);

	if (success_result != 0) {
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	error_result = json_unpack(response_json, "{s:{s:[s!]}}",
		"errors", "otp", &error_text);
	if (error_result == 0) {
		log_error("One time password error: %s", error_text);
		next = "mfa_otp_required";
	}

	struct caffeine_credentials * creds = NULL;

	if (access_token) {
		log_debug("Sign-in complete");
		creds = make_credentials(access_token, caid, refresh_token,
					credential);
	}
	else if (mfa_otp_method) {
		log_debug("MFA required");
	}
	else
		log_error("Sign-in response missing");

	response = bzalloc(sizeof(struct caffeine_auth_response));

	response->credentials = creds;
	response->next = bstrdup(next);
	response->mfa_otp_method = bstrdup(mfa_otp_method);

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
curl_init_error:
	free(request_body);
request_serialize_error:
	json_decref(request_json);
request_json_error:

	return response;
}

struct caffeine_auth_response * caffeine_signin(
	char const * username,
	char const * password,
	char const * otp)
{
	retry_request(void*, do_caffeine_signin(username, password, otp));
}

static struct caffeine_credentials * do_caffeine_refresh_auth(
	char const * refresh_token)
{
	trace();

	json_t * request_json = json_pack("{s:s}",
		"refresh_token", refresh_token);

	struct caffeine_credentials * result = NULL;

	if (!request_json)
	{
		log_error("Failed to create request JSON");
		goto request_json_error;
	}

	char * request_body = json_dumps(request_json, 0);
	if (!request_body)
	{
		log_error("Failed to serialize request JSON");
		goto request_serialize_error;
	}

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		goto curl_init_error;
	}

	curl_easy_setopt(curl, CURLOPT_URL, REFRESH_TOKEN_URL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);

	struct curl_slist *headers = caffeine_basic_headers(CONTENT_TYPE_JSON);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure refreshing tokens: [%d] %s", res,
			curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	log_debug("Http response [%ld]", response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse refresh response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:{s:[s!]}}",
		"errors", "_error", &error_text);
	if (error_result == 0) {
		log_error("Error refreshing tokens: %s", error_text);
		goto json_parsed_error;
	}

	char const *access_token = NULL;
	char const *new_refresh_token = NULL;
	char const *caid = NULL;
	char const *credential = NULL;
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s:{s:s,s:s,s:s,s:s}}",
		"credentials",
		"access_token", &access_token,
		"refresh_token", &new_refresh_token,
		"caid", &caid,
		"credential", &credential);

	if (success_result != 0) {
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	result = make_credentials(access_token, caid, new_refresh_token,
		credential);

	log_debug("Auth tokens refreshed");

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
curl_init_error:
	free(request_body);
request_serialize_error:
	json_decref(request_json);
request_json_error:

	return result;
}

struct caffeine_credentials * caffeine_refresh_auth(
	char const * refresh_token)
{
	retry_request(void*, do_caffeine_refresh_auth(refresh_token));
}

void caffeine_free_credentials(struct caffeine_credentials ** credentials)
{
	trace();
	if (!credentials || !*credentials)
		return;

	pthread_mutex_lock(&(*credentials)->mutex);
	bfree((*credentials)->access_token);
	bfree((*credentials)->caid);
	bfree((*credentials)->refresh_token);
	bfree((*credentials)->credential);

	pthread_mutex_unlock(&(*credentials)->mutex);
	pthread_mutex_destroy(&(*credentials)->mutex);
	bfree(*credentials);
	*credentials = NULL;
}

void caffeine_free_auth_response(struct caffeine_auth_response ** auth_response)
{
	trace();
	if (!auth_response || !*auth_response)
		return;

	caffeine_free_credentials(&(*auth_response)->credentials);

	bfree((*auth_response)->next);
	bfree((*auth_response)->mfa_otp_method);

	bfree(*auth_response);
	*auth_response = NULL;
}

static struct caffeine_user_info * do_caffeine_getuser(
	struct caffeine_credentials * creds)
{
	trace();
	if (creds == NULL) {
		log_error("Missing credentials");
		return NULL;
	}

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		return NULL;
	}

	struct caffeine_user_info * user_info = NULL;

	struct dstr url_str;
	dstr_init(&url_str);
	dstr_printf(&url_str, GETUSER_URL_F, creds->caid);

	curl_easy_setopt(curl, CURLOPT_URL, url_str.array);

	struct curl_slist *headers =
		caffeine_authenticated_headers(CONTENT_TYPE_JSON, creds);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure fetching user: [%d] %s", res, curl_error);
		goto request_error;
	}

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse user response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:{s:[s!]}}",
		"errors", "_error", &error_text);
	if (error_result == 0) {
		log_error("Error fetching user: %s", error_text);
		goto json_parsed_error;
	}

	char const *fetched_caid = NULL;
	char const *username = NULL;
	char const *stage_id = NULL;
	int can_broadcast = false; /* Jansson uses int for bool */
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s:{s:s,s:s,s:s,s:b}}",
		"user",
		"caid", &fetched_caid,
		"username", &username,
		"stage_id", &stage_id,
		"can_broadcast", &can_broadcast);

	if (success_result != 0) {
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	if (strcmp(fetched_caid, creds->caid) != 0) {
		log_warn("Somehow got a different user. Original caid: %s - "
			 "Fetched caid: %s",
			 creds->caid, fetched_caid);
	}

	user_info = bzalloc(sizeof(struct caffeine_user_info));
	user_info->caid = bstrdup(fetched_caid);
	user_info->username = bstrdup(username);
	user_info->stage_id = bstrdup(stage_id);
	user_info->can_broadcast = can_broadcast;

	log_debug("Got user details");

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&url_str);
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);

	return user_info;
}

struct caffeine_user_info * caffeine_getuser(
	struct caffeine_credentials * creds)
{
	retry_request(void*, do_caffeine_getuser(creds));
}

void caffeine_free_user_info(struct caffeine_user_info ** user_info)
{
	trace();
	if (!user_info || !*user_info)
		return;

	bfree((*user_info)->caid);
	bfree((*user_info)->username);
	bfree((*user_info)->stage_id);
	*user_info = NULL;
}

static struct caffeine_stream_info * do_caffeine_start_stream(
	char const * stage_id,
	char const * sdp_offer,
	struct caffeine_credentials * creds)
{
	trace();
	struct caffeine_stream_info * response = NULL;

	json_t * request_json = NULL;
	request_json = json_pack("{s:s,s:s,s:s}",
		"stage_id", stage_id,
		"offer", sdp_offer,
		"video_codec", "h264");

	if (!request_json)
	{
		log_error("Failed to create request JSON");
		goto request_json_error;
	}

	char * request_body = json_dumps(request_json, 0);
	if (!request_body)
	{
		log_error("Failed to serialize request JSON");
		goto request_serialize_error;
	}

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		goto curl_init_error;
	}

	curl_easy_setopt(curl, CURLOPT_URL, STREAM_URL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);

	struct curl_slist *headers =
		caffeine_authenticated_headers(CONTENT_TYPE_JSON, creds);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure starting stream: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse start stream response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error starting stream: %s", error_text);
		goto json_parsed_error;
	}

	char const *stream_id = NULL;
	char const *sdp_answer = NULL;
	char const *signed_payload = NULL;
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s:s,s:s,s:s}",
		"id", &stream_id,
		"answer", &sdp_answer,
		"signed_payload", &signed_payload);

	if (success_result != 0) {
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	response = bzalloc(sizeof(struct caffeine_stream_info));
	response->stream_id = bstrdup(stream_id);
	response->sdp_answer = bstrdup(sdp_answer);
	response->signed_payload = bstrdup(signed_payload);

	log_debug("Stream started");

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
curl_init_error:
	free(request_body);
request_serialize_error:
	json_decref(request_json);
request_json_error:

	return response;
}

struct caffeine_stream_info * caffeine_start_stream(
	char const * stage_id,
	char const * sdp_offer,
	struct caffeine_credentials * creds)
{
	retry_request(void*,
		do_caffeine_start_stream(stage_id, sdp_offer, creds));
}

void caffeine_free_stream_info(struct caffeine_stream_info ** stream_info)
{
	trace();
	if (!stream_info || !*stream_info)
		return;

	bfree((*stream_info)->stream_id);
	bfree((*stream_info)->sdp_answer);
	bfree((*stream_info)->signed_payload);
	*stream_info = NULL;
}

static struct caffeine_games * do_caffeine_get_supported_games()
{
	struct caffeine_games * response = NULL;

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		goto curl_init_error;
	}

	curl_easy_setopt(curl, CURLOPT_URL, GETGAMES_URL);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure fetching supported games: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse game list response: %s",
			json_error.text);
		goto json_failed_error;
	}
	size_t num_games = json_array_size(response_json); /* returns 0 if not array*/
	if (num_games == 0)
	{
		log_error("Unable to retrieve games list");
		goto json_parsed_error;
	}

	response = bzalloc(sizeof(struct caffeine_games));
	response->num_games = num_games;
	response->game_infos =
		bzalloc(num_games * sizeof(struct caffeine_game_info *));

	size_t index;
	json_t * value;
	json_array_foreach(response_json, index, value) {
		int id = 0;
		char const * name = NULL;
		json_t * process_names = NULL;
		int success_result = json_unpack_ex(value, &json_error, 0,
			"{s:i,s:s,s:o}",
			"id", &id,
			"name", &name,
			"process_names", &process_names);
		if (success_result != 0) {
			log_warn("Unable to parse game list entry; ignoring");
			continue;
		}

		size_t num_processes = json_array_size(process_names);
		if (num_processes == 0) {
			log_warn("No process names found for %s; ignoring",
				name);
			continue;
		}

		struct caffeine_game_info * info =
			bzalloc(sizeof(struct caffeine_game_info));
		info->id = id;
		info->name = bstrdup(name);
		info->num_process_names = num_processes;
		info->process_names = bzalloc(num_processes * sizeof(char *));

		size_t process_index;
		json_t * process_value;
		json_array_foreach(process_names, process_index, process_value) {
			char const * process_name = NULL;
			success_result = json_unpack(process_value, "s",
				&process_name);
			if (success_result != 0) {
				log_warn("Unable to read process name; ignoring");
				continue;
			}
			info->process_names[process_index] =
				bstrdup(process_name);
		}

		response->game_infos[index] = info;
	}

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_easy_cleanup(curl);
curl_init_error:

	return response;
}

struct caffeine_games * caffeine_get_supported_games()
{
	retry_request(struct caffeine_games *,
		do_caffeine_get_supported_games());
}

void caffeine_free_game_info(struct caffeine_game_info ** info)
{
	if (!info || !*info)
		return;

	for (size_t i = 0; i < (*info)->num_process_names; ++i) {
		bfree((*info)->process_names[i]);
		(*info)->process_names[i] = NULL;
	}
	bfree((*info)->name);
	bfree(*info);
	*info = NULL;
}

void caffeine_free_game_list(struct caffeine_games ** games)
{
	trace();
	if (!games || !*games)
		return;

	for (size_t i = 0; i < (*games)->num_games; ++i) {
		caffeine_free_game_info(&(*games)->game_infos[i]);
	}

	bfree((*games)->game_infos);
	bfree(*games);
	*games = NULL;
}

static bool do_caffeine_trickle_candidates(
	caff_ice_candidates candidates,
	size_t num_candidates,
	struct caffeine_credentials * creds,
	struct caffeine_stream_info const * stream_info)
{
	trace();
	json_t * ice_candidates = json_array();
	for (size_t i = 0; i < num_candidates; ++i)
	{
		json_array_append_new(ice_candidates, json_pack(
			"{s:s,s:s,s:i}",
			"candidate", candidates[i].sdp,
			"sdpMid", candidates[i].sdp_mid,
			"sdpMLineIndex", candidates[i].sdp_mline_index));
	}

	json_t * request_json = json_pack(
		"{s:o,s:s}",
		"ice_candidates", ice_candidates,
		"signed_payload", stream_info->signed_payload);

	bool response = false;

	if (!request_json)
	{
		log_error("Failed to create request JSON");
		goto request_json_error;
	}

	char * request_body = json_dumps(request_json, 0);
	if (!request_body)
	{
		log_error("Failed to serialize request JSON");
		goto request_serialize_error;
	}

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		goto curl_init_error;
	}

	struct dstr url_str;
	dstr_init(&url_str);
	dstr_printf(&url_str, TRICKLE_URL_F, stream_info->stream_id);

	curl_easy_setopt(curl, CURLOPT_URL, url_str.array);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

	struct curl_slist *headers =
		caffeine_authenticated_headers(CONTENT_TYPE_JSON, creds);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure negotiating ICE: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse ICE response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error negotiating ICE: %s", error_text);
		goto json_parsed_error;
	}

	char const *stream_id = NULL;
	char const *stage_id = NULL;
	char const *new_signed_payload = NULL;
	int success_result = json_unpack_ex(response_json, &json_error, 0,
		"{s:s,s:s,s:s}",
		"id", &stream_id,
		"stage_id", &stage_id,
		"signed_payload", &new_signed_payload);

	if (success_result != 0) {
		log_error("Failed to extract response info: [%s]",
			json_error.text);
		goto json_parsed_error;
	}

	response = true;
	log_debug("ICE candidates trickled");

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	dstr_free(&url_str);
curl_init_error:
	free(request_body);
request_serialize_error:
	json_decref(request_json);
request_json_error:

	return response;
}

bool caffeine_trickle_candidates(
	caff_ice_candidates candidates,
	size_t num_candidates,
	struct caffeine_credentials * creds,
	struct caffeine_stream_info const * stream_info)
{
	retry_request(bool,
		do_caffeine_trickle_candidates(candidates, num_candidates,
						creds, stream_info));
}

static bool do_refresh_credentials(struct caffeine_credentials * creds)
{
	trace();
	char * refresh_token = NULL;
	pthread_mutex_lock(&creds->mutex);
	refresh_token = bstrdup(creds->refresh_token);
	pthread_mutex_unlock(&creds->mutex);

	struct caffeine_credentials * new_creds =
		caffeine_refresh_auth(refresh_token);

	if (!new_creds)
		return false;

#define swap(field) do { \
			void * temp = creds->field; \
			creds->field = new_creds->field; \
			new_creds->field = temp; } while (false)

	pthread_mutex_lock(&creds->mutex);
	swap(access_token);
	swap(caid);
	swap(refresh_token);
	swap(credential);
	pthread_mutex_unlock(&creds->mutex);

#undef swap

	caffeine_free_credentials(&new_creds);
	return true;
}

bool refresh_credentials(struct caffeine_credentials * creds)
{
	retry_request(bool, do_refresh_credentials(creds));
}

bool send_heartbeat(
	char const * stage_id,
	char const * signed_payload,
	struct caffeine_credentials * creds)
{
	trace();
	bool result = false;

	json_t * request_json =
		json_pack("{s:s}", "signed_payload", signed_payload);

	if (!request_json)
	{
		log_error("Failed to create request JSON");
		goto request_json_error;
	}

	char * request_body = json_dumps(request_json, 0);
	if (!request_body)
	{
		log_error("Failed to serialize request JSON");
		goto request_serialize_error;
	}

	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		goto curl_init_error;
	}

	struct dstr url_str;
	dstr_init(&url_str);
	dstr_printf(&url_str, HEARTBEAT_URL_F, stage_id);

	curl_easy_setopt(curl, CURLOPT_URL, url_str.array);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);

	struct curl_slist *headers =
		caffeine_authenticated_headers(CONTENT_TYPE_JSON, creds);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure going live: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	/* TODO: implement this more generally (along with retries) */
	if (response_code == 401) {
		log_info("Unauthorized - refreshing credentials");
		refresh_credentials(creds);
		result = send_heartbeat(stage_id, signed_payload, creds);
		goto auth_refresh;
	}

	log_debug("Http response code [%ld]", response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to parse heartbeat response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error sending heartbeat: %s", error_text);
		goto json_parsed_error;
	}

	if (response_code / 100 == 2) {
		log_debug("Heartbeat sent");
		result = true;
	}

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
auth_refresh:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	dstr_free(&url_str);
curl_init_error:
	free(request_body);
request_serialize_error:
	json_decref(request_json);
request_json_error:

	return result;
}

static struct dstr make_title(char const * title, enum caffeine_rating rating)
{
	static const size_t MAX_TITLE_LENGTH = 60;
	static char const * rating_strings[] = { "", "[17+] " };

	if (rating < CAFF_RATING_NONE || rating >= CAFF_RATING_MAX)
		rating = CAFF_RATING_NONE;

	struct dstr final_title;
	dstr_init(&final_title);
	dstr_printf(&final_title, "%s%s", rating_strings[rating], title);
	dstr_resize(&final_title, MAX_TITLE_LENGTH);

	return final_title;
}

static char * do_set_stage_live(
	bool is_live,
	char const * session_id,
	char const * stage_id,
	char const * stream_id,
	char const * title,
	enum caffeine_rating rating,
	int game_id,
	struct caffeine_credentials * creds)
{
	trace();
	json_t * stream_json = json_pack("{s:s,s:s,s:s,s:{s:b,s:b}}",
		"id", stream_id,
		"label", "game",
		"type", "primary",
		"capabilities",
		"audio", 1,
		"video", 1);

	if (!stream_json)
		return NULL;

	struct dstr final_title = make_title(title, rating);
	json_t * payload_json = json_pack("{s:s,s:s,s:i,s:[o],s:s}",
		"state", is_live ? "ONLINE" : "OFFLINE",
		"title", final_title.array,
		"game_id", game_id,
		"streams", stream_json,
		"host_connection_quality", "GOOD");
	dstr_free(&final_title);

	if (!payload_json)
	{
		log_error("Failed to create request JSON");
		json_decref(stream_json);
		return NULL;
	}

	char const * request_type = "POST";
	if (session_id) {
		json_object_set_new(payload_json,
			"session_id", json_string(session_id));
		request_type = "PUT";
	}

	json_t * request_json = json_pack("{s:o}", "v2", payload_json);
	if (!request_json)
	{
		log_error("Failed to create request JSON");
		json_decref(payload_json);
		return NULL;
	}

	char * request_body = json_dumps(request_json, 0);
	json_decref(request_json);
	if (!request_body)
	{
		log_error("Failed to serialize request JSON");
		return NULL;
	}

	char * result = NULL;

	CURL * curl = curl_easy_init();
	if (!curl)
	{
		log_error("Failed to initialize cURL");
		goto curl_init_error;
	}

	struct dstr url_str;
	dstr_init(&url_str);
	dstr_printf(&url_str, STAGE_STATE_URL_F, stage_id);

	curl_easy_setopt(curl, CURLOPT_URL, url_str.array);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request_type);

	struct curl_slist *headers =
		caffeine_authenticated_headers(CONTENT_TYPE_JSON, creds);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure going live: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
	log_debug("Http response [%ld]", response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to stage state response: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error updating stage state: %s", error_text);
		goto json_parsed_error;
	}

	if (session_id) {
		result = session_id;
	}
	else {
		char const * new_session_id;
		if (json_unpack(response_json, "{s:s}",
			"session_id", &new_session_id) != 0) {
			goto request_error;
		}
		log_debug("got session id %s", new_session_id);

		result = bstrdup(new_session_id);
	}

	if (response_code / 100 == 2)
		log_debug("Stage set %s", (is_live ? "live" : "offline"));
	else
		log_warn("Failed to set stage state");

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	dstr_free(&url_str);
curl_init_error:
	free(request_body);

	return result;
}

char * set_stage_live(
	bool is_live,
	char const * session_id,
	char const * stage_id,
	char const * stream_id,
	char const * title,
	enum caffeine_rating rating,
	int game_id,
	struct caffeine_credentials * creds)
{
	retry_request(char*,
		do_set_stage_live(is_live, session_id, stage_id, stream_id,
				title, rating, game_id, creds));
}

void add_text_part(
	struct curl_httppost ** post,
	struct curl_httppost ** last,
	char const * name,
	char const * text)
{
	curl_formadd(post, last,
		CURLFORM_COPYNAME, name,
		CURLFORM_COPYCONTENTS, text,
		CURLFORM_END);
}

static char * do_create_broadcast(
	char const * title,
	enum caffeine_rating rating,
	uint8_t const * screenshot_data,
	size_t screenshot_size,
	struct caffeine_credentials * creds)
{
	trace();
	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		return false;
	}

	struct curl_httppost * post = NULL;
	struct curl_httppost * last = NULL;

	struct dstr final_title = make_title(title, rating);
	add_text_part(&post, &last, "broadcast[name]", final_title.array);
	dstr_free(&final_title);

	add_text_part(&post, &last, "broadcast[state]", "ONLINE");
	add_text_part(&post, &last, "broadcast[game_id]", "79");
	if (screenshot_data) {
		curl_formadd(&post, &last,
			CURLFORM_COPYNAME, "broadcast[game_image]",
			CURLFORM_BUFFER, "game_image.jpg",
			CURLFORM_BUFFERPTR, screenshot_data,
			CURLFORM_BUFFERLENGTH, screenshot_size,
			CURLFORM_CONTENTTYPE, "image/jpeg",
			CURLFORM_END);
	}

	curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);

	curl_easy_setopt(curl, CURLOPT_URL, BROADCAST_URL);

	struct curl_slist *headers =
		caffeine_authenticated_headers(CONTENT_TYPE_FORM, creds);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	char * result = NULL;

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure creating broadcast: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	log_debug("Http response code [%ld]", response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to create broadcast: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error creating broadcast: %s", error_text);
		goto json_parsed_error;
	}

	if (response_code / 100 != 2) {
		log_error("Failed to create broadcast");
		goto http_status_error;
	}

	char const * broadcast_id;
	int success_result = json_unpack(response_json, "{s:{s:s}}",
		"broadcast", "id", &broadcast_id);

	if (success_result == 0) {
		result = bstrdup(broadcast_id);
	}
	else {
		log_error("Failed to extract broadcast id");
	}

http_status_error:
json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	curl_formfree(post);
	curl_easy_cleanup(curl);

	return result;
}

char * create_broadcast(
	char const * title,
	enum caffeine_rating rating,
	uint8_t const * screenshot_data,
	size_t screenshot_size,
	struct caffeine_credentials * creds)
{
	retry_request(char*,
		do_create_broadcast(title, rating, screenshot_data,
				screenshot_size, creds));
}

static bool do_end_broadcast(
	char const * broadcast_id,
	char const * title,
	enum caffeine_rating rating,
	struct caffeine_credentials * creds)
{
	trace();
	CURL * curl = curl_easy_init();

	if (!curl)
	{
		log_error("Failed to initialize cURL");
		return false;
	}

	struct curl_httppost * post = NULL;
	struct curl_httppost * last = NULL;

	struct dstr final_title = make_title(title, rating);
	add_text_part(&post, &last, "broadcast[name]", final_title.array);
	dstr_free(&final_title);

	add_text_part(&post, &last, "broadcast[state]", "OFFLINE");
	add_text_part(&post, &last, "broadcast[game_id]", "79");

	curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");

	struct dstr url;
	dstr_init(&url);
	dstr_printf(&url, BROADCAST_URL_F, broadcast_id);
	curl_easy_setopt(curl, CURLOPT_URL, url.array);

	struct curl_slist *headers =
		caffeine_authenticated_headers(CONTENT_TYPE_FORM, creds);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	struct dstr response_str;
	dstr_init(&response_str);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
		caffeine_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

	char curl_error[CURL_ERROR_SIZE];
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

	bool result = false;

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		log_error("HTTP failure ending broadcast: [%d] %s",
			res, curl_error);
		goto request_error;
	}

	long response_code;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

	log_debug("Http response code [%ld]", response_code);

	json_error_t json_error;
	json_t * response_json = json_loads(response_str.array, 0, &json_error);
	if (!response_json) {
		log_error("Failed to end broadcast: %s",
			json_error.text);
		goto json_failed_error;
	}
	char const *error_text = NULL;
	int error_result = json_unpack(response_json, "{s:[s!]}",
		"_errors", &error_text);
	if (error_result == 0) {
		log_error("Error ending broadcast: %s", error_text);
		goto json_parsed_error;
	}

	if (response_code / 100 == 2) {
		result = true;
	}
	else {
		log_error("Failed to end broadcast");
	}

json_parsed_error:
	json_decref(response_json);
json_failed_error:
request_error:
	dstr_free(&response_str);
	curl_slist_free_all(headers);
	dstr_free(&url);
	curl_formfree(post);
	curl_easy_cleanup(curl);

	return result;
}

bool end_broadcast(
	char const * broadcast_id,
	char const * title,
	enum caffeine_rating rating,
	struct caffeine_credentials * creds)
{
	retry_request(bool, do_end_broadcast(broadcast_id, title, rating, creds));
}

void caffeine_free_stage_response(struct caffeine_stage_response ** response) {
	if (!response || !*response) {
		return;
	}

	bfree((*response)->cursor);
	json_decref((*response)->payload);

	*response = NULL;
}

void caffeine_free_failure(struct caffeine_failure_response ** failure) {
	if (!failure || !*failure) {
		return;
	}

	bfree((*failure)->uuid);
	bfree((*failure)->type);
	bfree((*failure)->reason);
	bfree((*failure)->display_message.title);
	bfree((*failure)->display_message.body);
}

void caffeine_free_stage_response_result(struct caffeine_stage_response_result ** result) {
	if (!result || !*result) {
		return;
	}

	caffeine_free_stage_response(&(*result)->response);
	caffeine_free_failure(&(*result)->failure);

	*result = NULL;
}

static struct caffeine_stage_response_result * do_caffeine_stage_update(
	char const * username,
	struct caffeine_stage_request const * request,
	struct caffeine_credentials * creds)
{
    trace();

	json_t * request_json = json_object();
	json_object_set_new(request_json,
						"client",
						json_pack("{s:s,s:b}",
						"client_id", request->client_id,
						"headless", 1));

	if (request->cursor) {
		json_object_set_new(request_json, "cursor", json_string(request->cursor));
	}

	if (request->payload) {
		json_object_set(request_json, "payload", request->payload);
	}

    char * request_body = json_dumps(request_json, 0);
    json_decref(request_json);
    if (!request_body)
    {
        log_error("Failed to serialize request JSON");
        return NULL;
    }

	struct caffeine_stage_response_result * result = NULL;
	struct caffeine_stage_response * stage_response = NULL;
	struct caffeine_failure_response * failure_response = NULL;

    CURL * curl = curl_easy_init();
    if (!curl)
    {
        log_error("Failed to initialize cURL");
        goto curl_init_error;
    }

    struct dstr url_str;
    dstr_init(&url_str);
    dstr_printf(&url_str, STAGE_UPDATE_URL_F, username);

    curl_easy_setopt(curl, CURLOPT_URL, url_str.array);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

    struct curl_slist *headers =
    caffeine_authenticated_headers(CONTENT_TYPE_JSON, creds);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    struct dstr response_str;
    dstr_init(&response_str);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     caffeine_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&response_str);

    char curl_error[CURL_ERROR_SIZE];
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        log_error("HTTP failure performing stage update: [%d] %s",
                  res, curl_error);
        goto request_error;
    }

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    log_debug("Http response [%ld]", response_code);

    json_error_t json_error;
    json_t * response_json = json_loads(response_str.array, 0, &json_error);
    if (!response_json) {
        log_error("Failed to deserialized stage update response to JSON: %s",
                  json_error.text);
        goto json_failed_error;
    }

	if (response_code == 200) {
		char * cursor = NULL;
		double retry_in = 0.0;
		if (json_unpack(
				response_json,
				"{s:s, s:f}",
				&cursor,
				&retry_in) != 0) {
			log_error("Failed to unpack stage response cursor and retry in");
			goto json_parsed_error;
		}

		json_t * payload = json_object_get(response_json, "payload");
		if (!payload || payload->type != JSON_OBJECT) {
			log_error("Response did not contain a payload");
			goto json_parsed_error;
		}

		struct caffeine_stage_response stage = {
			.cursor = bstrdup(cursor),
			.retry_in = retry_in,
			.payload = json_deep_copy(payload)
		};

		stage_response = bzalloc(sizeof(struct caffeine_stage_response));
		*stage_response = stage;
	} else if (response_code == 401) {
		log_info("Unauthorized - refreshing credentials");
		if (refresh_credentials(creds)) {
			result = do_caffeine_stage_update(username, request, creds);
		}
		goto auth_refresh;
	} else {
		char * uuid = NULL;
		char * type = NULL;
		char * reason = NULL;
		char * display_message_title = NULL;
		char * display_message_body = NULL;

		if (json_unpack(response_json, "{s?s,s?s,s?s,s?:{s?:s,s:s}", "uuid", &uuid, "type", &type, "reason", &reason, "display_message", "title", &display_message_title, "body", &display_message_body) != 0) {
			log_error("Failed to unpack failure response");
			goto json_parsed_error;
		}

		struct caffeine_failure_response failure = {
			.uuid = bstrdup(uuid),
			.type = bstrdup(type),
			.reason = bstrdup(reason),
			.display_message = {
				.title = bstrdup(display_message_body),
				.body = bstrdup(display_message_body)
			}
		};

		failure_response = bzalloc(sizeof(struct caffeine_failure_response));
		*failure_response = failure;
	}

json_parsed_error:
    json_decref(response_json);
json_failed_error:
request_error:
auth_refresh:
    dstr_free(&response_str);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    dstr_free(&url_str);
curl_init_error:
    free(request_body);

	if (stage_response || failure_response) {
		result = bzalloc(sizeof(struct caffeine_stage_response_result));

		if (stage_response) {
			result->response = stage_response;
		} else {
			result->failure = failure_response;
		}

		return result;
	}

    return NULL;
}

struct caffeine_stage_response_result * caffeine_stage_update(
	 char const * username,
	 struct caffeine_stage_request const * request,
	 struct caffeine_credentials * creds)
{
	retry_request(struct caffeine_stage_response_result *, do_caffeine_stage_update(username, request, creds));
}
