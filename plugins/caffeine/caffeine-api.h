#pragma once

#include <caffeine.h>

struct caffeine_credentials;

struct caffeine_auth_response {
	struct caffeine_credentials * credentials;
	char * next;
	char * mfa_otp_method;
};

struct caffeine_user_info {
	char * caid;
	char * username;
	char * stage_id;
	bool can_broadcast;
};

struct caffeine_stream_info {
	char * stream_id;
	char * sdp_answer;
	char * signed_payload;
};

/* TODO: game detection will be handled behind the scenes */
struct caffeine_game_info {
	char * id;
	char * name;
	char ** process_names;
	size_t num_process_names;
};

struct caffeine_games {
	struct caffeine_game_info ** game_infos;
	size_t num_games;
};

enum caffeine_rating {
	CAFF_RATING_NONE,
	CAFF_RATING_SEVENTEEN_PLUS,
	CAFF_RATING_MAX,
};

struct caffeine_stage_response {
	long status_code;
	char * session_id;
};

struct caffeine_auth_response * caffeine_signin(
	char const * username,
	char const * password,
	char const * otp);

struct caffeine_credentials * caffeine_refresh_auth(
	char const * refresh_token);

void caffeine_free_credentials(struct caffeine_credentials ** creds);

char const * caffeine_refresh_token(struct caffeine_credentials * creds);

void caffeine_free_auth_response(struct caffeine_auth_response ** auth_response);

struct caffeine_user_info * caffeine_getuser(
	struct caffeine_credentials * creds);

void caffeine_free_user_info(struct caffeine_user_info ** user_info);

struct caffeine_games * caffeine_get_supported_games();

void caffeine_free_game_info(struct caffeine_game_info ** info);
void caffeine_free_game_list(struct caffeine_games ** games);

struct caffeine_stream_info * caffeine_start_stream(
	char const * stage_id,
	char const * sdp_offer,
	struct caffeine_credentials * creds);

void caffeine_free_stream_info(struct caffeine_stream_info ** stream_info);

bool caffeine_trickle_candidates(
	caff_ice_candidates candidates,
	size_t num_candidates,
	struct caffeine_credentials * creds,
	struct caffeine_stream_info const * stream_info);

char * create_broadcast(
	char const * title,
	enum caffeine_rating rating,
	char const * game_id,
	uint8_t const * screenshot_data,
	size_t screenshot_size,
	struct caffeine_credentials * creds);

struct caffeine_stage_response set_stage_live(
	bool is_live,
	char const * session_id,
	char const * stage_id,
	char const * stream_id,
	char const * title,
	enum caffeine_rating rating,
	char const * game_id,
	struct caffeine_credentials * creds);

bool send_heartbeat(
	char const * stage_id,
	char const * signed_payload,
	struct caffeine_credentials * creds);

bool update_broadcast(
	char const * broadcast_id,
	bool is_online,
	char const * title,
	enum caffeine_rating rating,
	char const * game_id,
	struct caffeine_credentials * creds);
