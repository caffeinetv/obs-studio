#include <obs-module.h>

#include "caffeine-api.h"
#include "caffeine-service.h"

#define CAFFEINE_LOG_TITLE "caffeine service"
#include "caffeine-log.h"

#define USERNAME_KEY         "username"
#define PASSWORD_KEY         "password"
#define OTP_KEY              "otp"
#define SIGNIN_KEY           "signin"
#define SIGNOUT_KEY          "signout"

#define REFRESH_TOKEN_KEY    "refresh_token"

#define BROADCAST_RATING_KEY "rating"
#define BROADCAST_TITLE_KEY  "broadcast_title"

#define SIGNIN_MESSAGE_KEY   "signin_message"

struct caffeine_service {
	obs_service_t * service;

	/* auth */
	char * refresh_token;
	struct caffeine_credentials * creds;
	struct caffeine_user_info * user_info;

	/* settings */
	char * broadcast_title;
	enum caffeine_rating broadcast_rating;
};

static char const * caffeine_service_name(void * unused)
{
	UNUSED_PARAMETER(unused);

	return obs_module_text("CaffeineService");
}

static void caffeine_service_free_contents(struct caffeine_service * context)
{
	trace();
	if (!context)
		return;

	caffeine_free_credentials(&context->creds);
	caffeine_free_user_info(&context->user_info);

	bfree(context->refresh_token);
	bfree(context->broadcast_title);

	context->refresh_token = NULL;
	context->broadcast_title = NULL;
	context->broadcast_rating = CAFF_RATING_NONE;
}

static void set_requirements(obs_data_t *settings)
{
	obs_data_t *requirements = obs_data_create();
	obs_data_set_bool(requirements, REFRESH_TOKEN_KEY, true);
	obs_data_set_bool(requirements, BROADCAST_TITLE_KEY, true);
//	obs_data_set_bool(requirements, BROADCAST_RATING_KEY, true);
	obs_data_set_obj(settings, "requirements", requirements);
	obs_data_release(requirements);
}

static void caffeine_service_update(void * data, obs_data_t * settings)
{
	trace();
	struct caffeine_service * context = data;
	caffeine_service_free_contents(context);

	context->refresh_token =
		bstrdup(obs_data_get_string(settings, REFRESH_TOKEN_KEY));
	obs_data_set_default_string(settings, BROADCAST_TITLE_KEY,
		obs_module_text("DefaultBroadcastTitle"));
	context->broadcast_title =
		bstrdup(obs_data_get_string(settings, BROADCAST_TITLE_KEY));
	context->broadcast_rating =
		obs_data_get_int(settings, BROADCAST_RATING_KEY);

	set_requirements(settings);
}

static void * caffeine_service_create(
	obs_data_t * settings,
	obs_service_t * service)
{
	trace();

	struct caffeine_service * context =
		bzalloc(sizeof(struct caffeine_service));
	context->service = service;
	caffeine_service_update(context, settings);

	return context;
}

static void caffeine_service_destroy(void * data)
{
	trace();
	struct caffeine_service * context = data;
	caffeine_service_free_contents(context);
	bfree(context);
}

static void set_visible(obs_properties_t * props, char const * key, bool val)
{
	obs_property_t * prop = obs_properties_get(props, key);
	obs_property_set_visible(prop, val);
}

static void set_enabled(obs_properties_t * props, char const * key, bool val)
{
	obs_property_t * prop = obs_properties_get(props, key);
	obs_property_set_enabled(prop, val);
}

static void signed_out_state(obs_properties_t * props)
{
	set_enabled(props, USERNAME_KEY, true);
	set_visible(props, PASSWORD_KEY, true);
	set_visible(props, SIGNIN_KEY, true);

	set_visible(props, SIGNOUT_KEY, false);
	set_visible(props, OTP_KEY, false);
	set_visible(props, BROADCAST_TITLE_KEY, false);
	set_visible(props, BROADCAST_RATING_KEY, false);
}

static void signed_in_state(obs_properties_t * props)
{
	set_enabled(props, USERNAME_KEY, false);
	set_visible(props, PASSWORD_KEY, false);
	set_visible(props, SIGNIN_KEY, false);
	set_visible(props, OTP_KEY, false);

	set_visible(props, SIGNOUT_KEY, true);
	set_visible(props, BROADCAST_RATING_KEY, true);
	set_visible(props, BROADCAST_TITLE_KEY, true);
}

static void show_message(obs_properties_t * props, char const * message)
{
	log_info("Showing [%s] message: %s", SIGNIN_MESSAGE_KEY, message);
	obs_property_t * prop = obs_properties_get(props, SIGNIN_MESSAGE_KEY);
	obs_property_set_description(prop, message);
	obs_property_set_visible(prop, true);
}

static void hide_messages(obs_properties_t * props)
{
	obs_property_t * prop =
		obs_properties_get(props, SIGNIN_MESSAGE_KEY);
	obs_property_set_visible(prop, false);
}

static bool signin_clicked(obs_properties_t * props, obs_property_t * prop,
	obs_data_t * settings, void * data)
{
	trace();
	UNUSED_PARAMETER(prop);
	UNUSED_PARAMETER(data);

	set_requirements(settings);

	char const * username = obs_data_get_string(settings, USERNAME_KEY);
	char const * password = obs_data_get_string(settings, PASSWORD_KEY);

	hide_messages(props);

	if (strcmp(username, "") == 0 || strcmp(password, "") == 0) {
		show_message(props, obs_module_text("SigninInfoMissing"));
		return true;
	}

	char const * otp = obs_data_get_string(settings, OTP_KEY);
	obs_property_t * otp_prop = obs_properties_get(props, OTP_KEY);
	bool otp_visible = obs_property_visible(otp_prop);

	if (otp_visible && strcmp(otp, "") == 0) {
		/* TODO: add error messaging */
		show_message(props, obs_module_text("OtpMissing"));
		return true;
	}

	/* TODO: figure out a way to make this asynchronous */
	struct caffeine_auth_response * response =
		caffeine_signin(username, password, otp);

	if (!response) {
		show_message(props, obs_module_text("SigninFailed"));
	}
	else if (response->next) {
		if (strcmp(response->next, "mfa_otp_required") == 0) {
			if (otp_visible) {
				show_message(props, obs_module_text("OtpIncorrect"));
			}
			else {
				show_message(props, obs_module_text("OtpRequired"));
				obs_property_set_visible(otp_prop, true);
			}
		}
		if (strcmp(response->next, "legal_acceptance_required") == 0) {
			show_message(props, obs_module_text("TosAcceptanceRequired"));
		}
		if (strcmp(response->next, "email_verification") == 0) {
			show_message(props, obs_module_text("EmailVerificationRequired"));
		}
	}
	else if (!response->credentials) {
		show_message(props, obs_module_text("NoAuthResponse"));
	}
	else {
		obs_data_set_string(settings, REFRESH_TOKEN_KEY,
			caffeine_refresh_token(response->credentials));

		obs_data_erase(settings, PASSWORD_KEY);
		obs_data_erase(settings, OTP_KEY);

		signed_in_state(props);

		log_info("Successfully signed in");
	}

	caffeine_free_auth_response(&response);
	return true;
}

static bool signout_clicked(obs_properties_t * props, obs_property_t * prop,
	obs_data_t * settings, void * data)
{
	trace();
	UNUSED_PARAMETER(prop);
	UNUSED_PARAMETER(data);

	set_requirements(settings);

	obs_data_erase(settings, REFRESH_TOKEN_KEY);
	obs_data_erase(settings, USERNAME_KEY);
	obs_data_set_string(settings, BROADCAST_TITLE_KEY,
		obs_module_text("DefaultBroadcastTitle"));
	signed_out_state(props);
	return true;
}

static bool refresh_token_changed(obs_properties_t * props,
	obs_property_t * prop, obs_data_t * settings)
{
	trace();
	UNUSED_PARAMETER(prop);

	char const * val = obs_data_get_string(settings, REFRESH_TOKEN_KEY);
	if (strcmp(val, "") == 0)
		signed_out_state(props);
	else
		signed_in_state(props);

	return true;
}

static obs_properties_t * caffeine_service_properties(void * data)
{
	trace();
	UNUSED_PARAMETER(data);
	obs_properties_t * props = obs_properties_create();
	obs_property_t * prop = NULL;

	/* TODO: show different properties when already logged in */

	obs_properties_add_text(props, USERNAME_KEY,
		obs_module_text("Username"), OBS_TEXT_DEFAULT);

	prop = obs_properties_add_text(props, PASSWORD_KEY,
		obs_module_text("Password"), OBS_TEXT_PASSWORD);
	obs_property_set_transient(prop, true);

	prop = obs_properties_add_text(props, OTP_KEY,
		obs_module_text("OneTimePassword"), OBS_TEXT_PASSWORD);
	obs_property_set_transient(prop, true);

	prop = obs_properties_add_message(props, SIGNIN_MESSAGE_KEY, "");
	obs_property_set_visible(prop, false);

	prop = obs_properties_add_text(props, REFRESH_TOKEN_KEY,
		REFRESH_TOKEN_KEY, OBS_TEXT_DEFAULT);
	obs_property_set_modified_callback(prop, refresh_token_changed);
	obs_property_set_visible(prop, false);

	obs_properties_add_button3(props, SIGNIN_KEY,
		obs_module_text("ButtonSignIn"), signin_clicked, NULL);

	obs_properties_add_button3(props, SIGNOUT_KEY,
		obs_module_text("ButtonSignOut"), signout_clicked, NULL);

	obs_properties_add_text(props, BROADCAST_TITLE_KEY,
		obs_module_text("BroadcastTitle"), OBS_TEXT_DEFAULT);

	prop = obs_properties_add_list(props, BROADCAST_RATING_KEY,
		obs_module_text("Rating"), OBS_COMBO_TYPE_LIST,
		OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(prop, obs_module_text("None"),
		CAFF_RATING_NONE);
	obs_property_list_add_int(prop, obs_module_text("SeventeenPlus"),
		CAFF_RATING_SEVENTEEN_PLUS);

	signed_out_state(props);

	/* TODO: add button to do the signin here */

	return props;
}

static void caffeine_service_defaults(obs_data_t *defaults)
{
	trace();
	obs_data_set_default_string(defaults, BROADCAST_TITLE_KEY,
		obs_module_text("DefaultBroadcastTitle"));
	set_requirements(defaults);
}

static bool caffeine_service_initialize(void * data, obs_output_t * output)
{
	trace();
	UNUSED_PARAMETER(output);
	struct caffeine_service * context = data;

	if (!caffeine_is_supported_version()) {
		set_error(output, "%s", obs_module_text("ErrorOldVersion"));
		return false;
	}

	obs_data_t * settings = obs_service_get_settings(context->service);
	char const * refresh_token =
		obs_data_get_string(settings, REFRESH_TOKEN_KEY);

	if (strcmp(refresh_token, "") == 0) {
		set_error(output, "%s", obs_module_text("ErrorMustSignIn"));
		return false;
	}

	char const * title = obs_data_get_string(settings, BROADCAST_TITLE_KEY);
	if (strcmp(title, "") == 0) {
		title = obs_module_text("DefaultBroadcastTitle");
	}

	if (context->creds)
		return true;

	struct caffeine_credentials * credentials = NULL;
	struct caffeine_user_info * user_info = NULL;

	/* TODO: make this asynchronous? */

	credentials = caffeine_refresh_auth(context->refresh_token);

	if (!credentials) {
		set_error(output, "%s", obs_module_text("ErrorExpiredAuth"));
		/* todo switch to non-logged-in state*/
		return false;
	}

	user_info = caffeine_getuser(credentials);

	if (!user_info) {
		set_error(output, "%s", obs_module_text("ErrorNoUserInfo"));
		goto cleanup_auth;
	}
	if (!user_info->can_broadcast) {
		set_error(output, "%s", obs_module_text("ErrorCantBroadcast"));
		caffeine_free_user_info(&user_info);
		goto cleanup_auth;
	}

	context->creds = credentials;
	context->user_info = user_info;

	log_info("Successfully refreshed auth");

	return true;

cleanup_auth:
	caffeine_free_credentials(&credentials);
	return false;
}

static void * caffeine_service_query(void * data, int query_id, va_list unused)
{
	trace();
	UNUSED_PARAMETER(unused);
	struct caffeine_service * service = data;

	switch (query_id)
	{
	case CAFFEINE_QUERY_CREDENTIALS:
		return service->creds;
	case CAFFEINE_QUERY_STAGE_ID:
		return service->user_info->stage_id;
	case CAFFEINE_QUERY_USERNAME:
		return service->user_info->username;
	case CAFFEINE_QUERY_BROADCAST_TITLE:
		if (service->broadcast_title && *service->broadcast_title)
			return service->broadcast_title;
		else
			return obs_module_text("DefaultBroadcastTitle");
	case CAFFEINE_QUERY_BROADCAST_RATING:
		return (void*)service->broadcast_rating;
	default:
		log_warn("Unrecognized query [%d]", query_id);
		return NULL;
	}
}

static char const * caffeine_service_output_type(void * unused)
{
	UNUSED_PARAMETER(unused);
	return "caffeine_output";
}

struct obs_service_info caffeine_service_info = {
	.id = "caffeine_service",
	.get_name = caffeine_service_name,
	.create = caffeine_service_create,
	.destroy = caffeine_service_destroy,
	.update = caffeine_service_update,
	.get_properties = caffeine_service_properties,
	.get_defaults = caffeine_service_defaults,
	.initialize = caffeine_service_initialize,
	.query = caffeine_service_query,
	.get_output_type = caffeine_service_output_type,
};
