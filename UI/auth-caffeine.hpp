#pragma once

#include "auth-oauth.hpp"
#include "caffeine.h"
#include "window-dock.hpp"

class CaffeineChat;
class QLineEdit;
class QWidget;
class QString;
class QDialog;

class CaffeineAuth : public OAuthStreamKey {
	Q_OBJECT
	caff_InstanceHandle instance;

	bool uiLoaded = false;
	QSharedPointer<OBSDock> panelDock;
	QSharedPointer<CaffeineChat> chat;
	QSharedPointer<QAction> chatMenu;

	std::string username;

	void TryAuth(
		QLineEdit * u,
		QLineEdit * p,
		QWidget * parent,
		QString const & caffeineStyle,
		QDialog * prompt);
	virtual bool RetryLogin() override;

	virtual void SaveInternal() override;
	virtual bool LoadInternal() override;

	bool GetChannelInfo();

	virtual void LoadUI() override;

	virtual void OnStreamConfig() override;

public:
	CaffeineAuth(const Def &d);
	virtual ~CaffeineAuth();

	static std::shared_ptr<Auth> Login(QWidget *parent);

	std::string GetUsername();
};
