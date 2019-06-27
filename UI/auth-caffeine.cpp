#include "auth-caffeine.hpp"

#include <QFontDatabase>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>

#include <qt-wrappers.hpp>
#include <obs-app.hpp>

#include "window-basic-main.hpp"
#include "remote-text.hpp"
#include "window-dock.hpp"
#include "window-caffeine.hpp"

#include <util/threading.h>
#include <util/platform.h>

#include <json11.hpp>

#include <ctime>

#include "ui-config.h"
#include "obf.h"

#include "ui_CaffeineSignIn.h"

using namespace json11;

/* ------------------------------------------------------------------------- */

#define CAFFEINE_AUTH_URL \
	"https://obsproject.com/app-auth/caffeine?action=redirect"
#define CAFFEINE_TOKEN_URL \
	"https://obsproject.com/app-auth/caffeine-token"

#define CAFFEINE_SCOPE_VERSION 1

static Auth::Def caffeineDef = {
	"Caffeine",
	Auth::Type::Custom,
	true
};

/* ------------------------------------------------------------------------- */

static int addFonts() {
	QFontDatabase::addApplicationFont(":/caffeine/fonts/Poppins-Regular.ttf");
	QFontDatabase::addApplicationFont(":/caffeine/fonts/Poppins-Bold.ttf");
	QFontDatabase::addApplicationFont(":/caffeine/fonts/Poppins-Light.ttf");
	QFontDatabase::addApplicationFont(":/caffeine/fonts/Poppins-ExtraLight.ttf");
	return 0;
}

CaffeineAuth::CaffeineAuth(const Def &d)
	: OAuthStreamKey(d)
{
	UNUSED_PARAMETER(d);
	instance = caff_createInstance();
}

CaffeineAuth::~CaffeineAuth()
{
	caff_freeInstance(&instance);
}

bool CaffeineAuth::GetChannelInfo()
try {
	key_ = refresh_token;

	if (caff_isSignedIn(instance)) {
		username = caff_getUsername(instance);
	} else {
		switch (caff_refreshAuth(instance, refresh_token.c_str())) {
		case caff_ResultSuccess:
			username = caff_getUsername(instance);
			break;
		case caff_ResultRefreshTokenRequired:
		case caff_ResultInfoIncorrect:
			throw ErrorInfo(Str("CaffeineAuth.Unauthorized"),
				Str("CaffeineAuth.IncorrectRefresh"));
		default:
			throw ErrorInfo(Str("CaffeineAuth.Failed"),
				Str("CaffeineAuth.SigninFailed"));
		}
	}

	OBSBasic *main = OBSBasic::Get();
	obs_service_t *service = main->GetService();
	obs_data_t *settings = obs_service_get_settings(service);
	obs_data_set_string(settings, "username", username.c_str());
	obs_data_release(settings);

	return true;
} catch (ErrorInfo info) {
	QString title = QTStr("Auth.ChannelFailure.Title");
	QString text = QTStr("Auth.ChannelFailure.Text")
		.arg(service(), info.message.c_str(), info.error.c_str());

	QMessageBox::warning(OBSBasic::Get(), title, text);

	blog(LOG_WARNING, "%s: %s: %s",
			__FUNCTION__,
			info.message.c_str(),
			info.error.c_str());
	return false;
}

void CaffeineAuth::SaveInternal()
{
	OBSBasic *main = OBSBasic::Get();
	config_set_string(main->Config(), service(), "Username", username.c_str());
	if (uiLoaded) {
		config_set_string(main->Config(), service(), "DockState",
				main->saveState().toBase64().constData());
	}
	OAuthStreamKey::SaveInternal();
}

static inline std::string get_config_str(
		OBSBasic *main,
		const char *section,
		const char *name)
{
	const char *val = config_get_string(main->Config(), section, name);
	return val ? val : "";
}

bool CaffeineAuth::LoadInternal()
{
	OBSBasic *main = OBSBasic::Get();
	username = get_config_str(main, service(), "Username");
	firstLoad = false;
	return OAuthStreamKey::LoadInternal();
}

class CaffeineChat : public OBSDock {
public:
	inline CaffeineChat() : OBSDock() {}
};

void CaffeineAuth::LoadUI()
{
	if (uiLoaded)
		return;
	if (!GetChannelInfo())
		return;
	/* TODO: Chat */

	// Panel
	panelDock = QSharedPointer<CaffeineInfoPanel>(
		new CaffeineInfoPanel(this, instance)).dynamicCast<OBSDock>();

	uiLoaded = true;
	return;
}

void CaffeineAuth::OnStreamConfig()
{
	OBSBasic *main = OBSBasic::Get();
	obs_service_t *service = main->GetService();
	obs_data_t *data = obs_service_get_settings(service);
	QSharedPointer<CaffeineInfoPanel> panel2 =
		panelDock.dynamicCast<CaffeineInfoPanel>();
	obs_data_set_string(data, "broadcast_title", panel2->getTitle().c_str());
	obs_data_set_int(data, "rating", static_cast<int64_t>(panel2->getRating()));
	obs_service_update(service, data);
	obs_data_release(data);
}

bool CaffeineAuth::RetryLogin()
{
	std::shared_ptr<Auth> ptr = Login(OBSBasic::Get());
	return ptr != nullptr;
}

void CaffeineAuth::TryAuth(Ui::CaffeineSignInDialog *ui, QDialog *dialog,
	std::string &passwordForOtp)
{
	std::string username = ui->usernameEdit->text().toStdString();
	std::string otp;
	std::string password;
	if (passwordForOtp.empty()) {
		password = ui->passwordEdit->text().toStdString();
	} else {
		otp = ui->passwordEdit->text().toStdString();
		password = passwordForOtp;
	}

	auto result = caff_signIn(
		instance, username.c_str(), password.c_str(), otp.c_str());
	switch (result) {
	case caff_ResultSuccess:
		refresh_token = caff_getRefreshToken(instance);
		dialog->accept();
		return;
	case caff_ResultMfaOtpRequired:
		ui->messageLabel->setText(R"(Enter the authentication code that was sent to your email. <a href="https://www.caffeine.tv" style="text-decoration:none;color:#009fe0;">Resend email.</a>)");
		ui->usernameEdit->hide();
		passwordForOtp = ui->passwordEdit->text().toStdString();
		ui->passwordEdit->clear();
		ui->passwordEdit->setPlaceholderText("Enter code");
		ui->signInButton->setText("Continue");
		ui->newUserFooter->hide();
		return;
	case caff_ResultMfaOtpIncorrect:
		ui->passwordEdit->clear();
		ui->messageLabel->setText("The verification code was incorrect.");
		return;
	case caff_ResultUsernameRequired:
		ui->messageLabel->setText(Str("CaffeineAuth.UsernameRequired"));
		return;
	case caff_ResultPasswordRequired:
		ui->messageLabel->setText(Str("CaffeineAuth.PasswordRequired"));
		return;
	case caff_ResultInfoIncorrect:
		ui->messageLabel->setText(Str("CaffeineAuth.IncorrectInfo"));
		return;
	case caff_ResultLegalAcceptanceRequired:
		ui->messageLabel->setText(Str("CaffeineAuth.TosAcceptanceRequired"));
		return;
	case caff_ResultEmailVerificationRequired:
		ui->messageLabel->setText(Str("CaffeineAuth.EmailVerificationRequired"));
		return;
	case caff_ResultFailure:
	default:
		ui->messageLabel->setText(Str("CaffeineAuth.SigninFailed"));
		return;
	}
}

std::shared_ptr<Auth> CaffeineAuth::Login(QWidget *parent)
{
	static int once = addFonts();
	UNUSED_PARAMETER(once);
	const auto flags =
		Qt::WindowTitleHint
		| Qt::WindowSystemMenuHint
		| Qt::WindowCloseButtonHint;

	QDialog dialog(parent, flags);
	auto ui = new Ui::CaffeineSignInDialog;
	ui->setupUi(&dialog);
	// For some reason the SVG appears in the designer but not in the dialog
	QIcon icon(":/caffeine/images/CaffeineLogo.svg");
	ui->logo->setPixmap(icon.pixmap(76, 66));

	// Don't highlight text boxes
	for (auto edit : dialog.findChildren<QLineEdit*>()) {
		edit->setAttribute(Qt::WA_MacShowFocusRect, false);
	}

	ui->messageLabel->clear();

	std::shared_ptr<CaffeineAuth> auth =
		std::make_shared<CaffeineAuth>(caffeineDef);

	std::string origPassword;
	connect(ui->signInButton, &QPushButton::clicked,
		[&](bool checked) {
			auth->TryAuth(ui, &dialog, origPassword);
		});

	// Only used for One-time-password "resend email" link. resending the
	// email is just attempting the sign-in without one-time password
	// included
	connect(ui->messageLabel, &QLabel::linkActivated,
		[&](const QString &link) {
			auto username = ui->usernameEdit->text().toStdString();
			caff_signIn(auth->instance, username.c_str(),
				origPassword.c_str(), nullptr);
		});
	
	if (dialog.exec() == QDialog::Rejected)
		return nullptr;

	if (auth->GetChannelInfo())
		return auth;

	return nullptr;
}

std::string CaffeineAuth::GetUsername()
{
	return this->username;
}

static std::shared_ptr<Auth> CreateCaffeineAuth()
{
	return std::make_shared<CaffeineAuth>(caffeineDef);
}

static void DeleteCookies()
{

}

void RegisterCaffeineAuth()
{
	OAuth::RegisterOAuth(
			caffeineDef,
			CreateCaffeineAuth,
			CaffeineAuth::Login,
			DeleteCookies);	
}
