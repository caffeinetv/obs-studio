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
	QFontDatabase::addApplicationFont(":/res/fonts/Poppins-Regular.ttf");
	QFontDatabase::addApplicationFont(":/res/fonts/Poppins-Bold.ttf");
	QFontDatabase::addApplicationFont(":/res/fonts/Poppins-Light.ttf");
	QFontDatabase::addApplicationFont(":/res/fonts/Poppins-ExtraLight.ttf");
	return 0;
}

CaffeineAuth::CaffeineAuth(const Def &d)
	: OAuthStreamKey(d)
{
	UNUSED_PARAMETER(d);
	static int once = addFonts();
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
		return true;
	} else {
		switch (caff_refreshAuth(instance, refresh_token.c_str())) {
		case caff_ResultSuccess:
			username = caff_getUsername(instance);
			return true;
		case caff_ResultRefreshTokenRequired:
		case caff_ResultInfoIncorrect:
			throw ErrorInfo(Str("CaffeineAuth.Unauthorized"),
				Str("CaffeineAuth.IncorrectRefresh"));
		default:
			throw ErrorInfo(Str("CaffeineAuth.Failed"),
				Str("CaffeineAuth.SigninFailed"));
		}
	}
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

void CaffeineAuth::TryAuth(
	QLineEdit * u,
	QLineEdit * p,
	QWidget * parent,
	QString const & caffeineStyle,
	QDialog * prompt)
{
	std::string username = u->text().toStdString();
	std::string password = p->text().toStdString();

	QDialog otpdialog(parent);
	QString style = otpdialog.styleSheet();
	style += caffeineStyle;
	QFormLayout otpform(&otpdialog);
	otpdialog.setWindowTitle("Caffeine Login (One Time Password)");
	//otpform.addRow(new QLabel("Caffeine One Time Password"));

	QLineEdit *onetimepassword = new QLineEdit(&otpdialog);
	onetimepassword->setEchoMode(QLineEdit::Password);
	onetimepassword->setPlaceholderText(QTStr("Password"));
	//otpform.addRow(new QLabel(QTStr("Password")), onetimepassword);
	otpform.addWidget(onetimepassword);

	QPushButton *login = new QPushButton(QTStr("Login"));
	QPushButton *cancel = new QPushButton(QTStr("Cancel"));

	QDialogButtonBox otpButtonBox(Qt::Horizontal, &otpdialog);

	otpButtonBox.addButton(login, QDialogButtonBox::ButtonRole::AcceptRole);
	otpButtonBox.addButton(cancel, QDialogButtonBox::ButtonRole::RejectRole);

	QObject::connect(&otpButtonBox, SIGNAL(accepted()),
			&otpdialog, SLOT(accept()));
	QObject::connect(&otpButtonBox, SIGNAL(rejected()),
			&otpdialog, SLOT(reject()));
	otpform.addRow(&otpButtonBox);

	std::string message = "";
	std::string error = "";

	caff_Result response = caff_signIn(
		instance, username.c_str(), password.c_str(), nullptr);
	do {
		switch (response) {
		case caff_ResultSuccess:
			refresh_token = caff_getRefreshToken(instance);
			prompt->accept();
			return;
		case caff_ResultUsernameRequired:
			message = Str("CaffeineAuth.Failed");
			error = Str("CaffeineAuth.UsernameRequired");
			break;
		case caff_ResultPasswordRequired:
			message = Str("CaffeineAuth.Failed");
			error = Str("CaffeineAuth.PasswordRequired");
			break;
		case caff_ResultInfoIncorrect:
			message = Str("CaffeineAuth.Unauthorized");
			error = Str("CaffeineAuth.IncorrectInfo");
			break;
		case caff_ResultMfaOtpRequired:
		case caff_ResultMfaOtpIncorrect: /* TODO make this different */
			if (otpdialog.exec() == QDialog::Rejected)
				return;
			response = caff_signIn(
				instance, username.c_str(), password.c_str(),
				onetimepassword->text().toStdString().c_str());
			continue;
		case caff_ResultLegalAcceptanceRequired:
			message = Str("CaffeineAuth.Unauthorized");
			error = Str("CaffeineAuth.TosAcceptanceRequired");
			break;
		case caff_ResultEmailVerificationRequired:
			message = Str("CaffeineAuth.Unauthorized");
			error = Str("CaffeineAuth.EmailVerificationRequired");
			break;
		case caff_ResultFailure:
		default:
			message = Str("CaffeineAuth.Failed");
			error = Str("CaffeineAuth.SigninFailed");
			break;
		}
	} while (true);

	QString title = QTStr("Auth.ChannelFailure.Title");
	QString text = QTStr("Auth.ChannelFailure.Text")
		.arg("Caffeine", message.c_str(), error.c_str());

	QMessageBox::warning(OBSBasic::Get(), title, text);

	blog(LOG_WARNING, "%s: %s: %s",
		__FUNCTION__,
		message.c_str(),
		error.c_str());
	return;
}

std::shared_ptr<Auth> CaffeineAuth::Login(QWidget *parent)
{
	QDialog dialog(parent);
	QDialog *prompt = &dialog;
	QFormLayout form(&dialog);
	dialog.setObjectName("caffeinelogin");
	dialog.setProperty("themeID", "caffeineLogin");
	form.setContentsMargins(0, 0, 0, 0);
	form.setSpacing(0);
	QString caffeineStyle = R"(
		* { background-color: white; color: black; margin:0; padding:0; }
		* [themeID="caffeineLogo"] {margin: 31px 0 23px 0;}
		* [themeID="caffeineWelcome"] {line-height: 48px; margin-bottom: 64px; font-weight: normal; font-family: "Poppins ExtraLight", SegoeUI, sans-serif; font-size: 32px;}
		QLineEdit {width: 280px; margin: 0px 195px 0px 195px; padding: 10px 20px 10px 20px; font-weight: normal; font-family: "Poppins Light", SegoeUI, sans-serif; border-radius: 0px; border: 1px solid #f2f2f2; border-top: 4px solid #f2f2f2;}
		QPushButton {width: 280px; height:80px; font-family: Poppins, SegoeUI, sans-serif; font-size: 24px; background-color: #009fe0; color:white; border-radius: 40px; border: 0px solid #009fe0}
		QPushButton::hover {background-color:#007cad;}
		* [themeID="caffeineLogin"] {font-weight: normal; font-family: Poppins, SegoeUI, sans-serif; font-size: 14px;}
		* [themeID="caffeineTrouble"] {padding-left: 29px; padding-right: 29px; font-weight: normal; font-family: Poppins, SegoeUI, sans-serif; font-size: 18px;}
		* [themeID="caffeineFooter"] { font-family: Poppins, SegoeUI, sans-serif; background-color: #009fe0; color: white; width: 100%; padding: 18 0 16 0; margin: 10 0 0 0;}
	)";

	QString style = dialog.styleSheet();
	style += caffeineStyle;

	dialog.setStyleSheet(style);
	dialog.setWindowTitle("Sign In");
	
	QIcon icon(":/res/images/CaffeineLogo.svg");
	QLabel *logo = new QLabel();
	logo->setPixmap(icon.pixmap(76, 66));
	logo->setAlignment(Qt::AlignHCenter);
	logo->setProperty("themeID", "caffeineLogo");
	form.addRow(logo);

	QLabel *welcome = new QLabel("Sign in to\nCaffeine");
	welcome->setAlignment(Qt::AlignHCenter);
	welcome->setProperty("themeID", "caffeineWelcome");
	welcome->setFixedHeight(160);
	form.addRow(welcome);

	QLineEdit *u = new QLineEdit(&dialog);
	u->setPlaceholderText(QTStr("Username"));
	u->setProperty("themeID", "caffeineLogin");
	u->setAttribute(Qt::WA_MacShowFocusRect, 0);
	form.addRow(u);

	QSpacerItem *spacer = new QSpacerItem(0, 8);
	form.addItem(spacer);

	QLineEdit *p = new QLineEdit(&dialog);
	p->setPlaceholderText(QTStr("Password"));
	p->setEchoMode(QLineEdit::Password);
	p->setProperty("themeID", "caffeineLogin");
	p->setAttribute(Qt::WA_MacShowFocusRect, 0);
	form.addRow(p);

	spacer = new QSpacerItem(0, 64);
	form.addItem(spacer);

	QPushButton *signin  = new QPushButton(QTStr("Sign in"));
	signin->setCursor(Qt::PointingHandCursor);
	QDialogButtonBox buttonBox(Qt::Horizontal, &dialog);
	buttonBox.setCenterButtons(true);
	buttonBox.addButton(signin,  QDialogButtonBox::ButtonRole::ActionRole);
	form.addRow(&buttonBox);
	
	QObject::connect(&buttonBox, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(&buttonBox, SIGNAL(rejected()), &dialog, SLOT(reject()));

	spacer = new QSpacerItem(0, 16);
	form.addItem(spacer);

	QLabel      *trouble = new QLabel(
		R"(Forgot something?<br/><a href="https://www.caffeine.tv/forgot-password" style="color: #009fe0; text-decoration: none">Reset your password</a>)");

	trouble->setFixedHeight(60);
	trouble->setProperty("themeID", "caffeineTrouble");
	trouble->setOpenExternalLinks(true);
	trouble->setAlignment(Qt::AlignHCenter);
	form.addRow(trouble);

	spacer = new QSpacerItem(0, 69);
	form.addItem(spacer);

	QLabel      *signup = new QLabel(
		R"(New to Caffeine? <b><a href="https://www.caffeine.tv/sign-up" style="color: white; text-decoration: none">Sign Up</a></b>)"
	);

	signup->setProperty("themeID", "caffeineFooter");
	signup->setOpenExternalLinks(true);
	signup->setAlignment(Qt::AlignHCenter);
	form.addRow(signup);

	dialog.setFixedSize(dialog.sizeHint());

	std::shared_ptr<CaffeineAuth> auth =
		std::make_shared<CaffeineAuth>(caffeineDef);
	QObject::connect(signin, &QPushButton::clicked, [=](bool checked) {
		auth->TryAuth(u, p, parent, caffeineStyle, prompt);
	});
	
	if (dialog.exec() == QDialog::Rejected)
		return nullptr;

	if (auth) {
		if (auth->GetChannelInfo())
			return auth;
	}
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
