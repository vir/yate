/**
 * updater.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Auto updater logic and downloader for Qt-4 clients.
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "updater.h"

#include <unistd.h>
#include <stdio.h>

#include <QUrl>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#define MIN_SIZE 1024
#define MAX_SIZE (16*1024*1024)

#define TMP_EXT ".tmp"
#ifdef _WINDOWS
#define EXE_EXT ".exe"
#else
#define EXE_EXT ".bin"
#endif

using namespace TelEngine;
namespace { // anonymous

/**
 * UI logic interaction
 */
class UpdateLogic : public ClientLogic
{
public:
    enum Policy {
	Invalid,
	Never,
	Check,
	Download,
	Install,
    };
    inline UpdateLogic(const char* name)
	: ClientLogic(name,100),
	  m_policy(Invalid), m_checking(false),
	  m_checked(false), m_install(false),
	  m_http(0), m_file(0), m_httpSlots(0), m_canUpdate(true)
	{ }
    virtual ~UpdateLogic();
    inline Policy policy() const
	{ return static_cast<Policy>(m_policy); }
    virtual bool initializedClient();
    virtual void exitingClient();
    virtual bool action(Window* wnd, const String& name, NamedList* params);
    virtual bool toggle(Window* wnd, const String& name, bool active);
    void gotPercentage(int percent);
    void endHttp(bool error);
protected:
    void setPolicy(int policy, bool save);
    void startChecking(bool start = true);
    void finishedChecking();
    void startDownloading(bool start = true);
    void finishedDownloading();
    void startInstalling();
private:
    QString filePath(bool temp);
    bool startHttp(const char* url, const QString& saveAs);
    void stopHttp();
    void stopFile();
    int m_policy;
    bool m_checking;
    bool m_checked;
    bool m_install;
    String m_url;
    QHttp* m_http;
    QFile* m_file;
    QtUpdateHttp* m_httpSlots;
    bool m_canUpdate;
};
/**
 * Plugin registration
 */
class Updater : public Plugin
{
public:
    Updater();
    virtual ~Updater();
    virtual void initialize();
private:
    UpdateLogic* m_logic;
};

static Updater s_plugin;

static const TokenDict s_policies[] = {
    { "never",    UpdateLogic::Never    },
    { "check",    UpdateLogic::Check    },
    { "download", UpdateLogic::Download },
    { "install",  UpdateLogic::Install  },
    { 0,          UpdateLogic::Invalid  }
};

UpdateLogic::~UpdateLogic()
{
}

bool UpdateLogic::initializedClient()
{
    // Check if the current user can write to install dir
    // Disable and uncheck all updater UI controls on failure
    Configuration cfg(Engine::configFile("updater"));
    m_canUpdate = !File::exists(cfg) || cfg.save();
    if (!m_canUpdate) {
	Debug(toString(),DebugInfo,"Disabling updates: the current user can't write to '%s'",
	    Engine::configPath().c_str());
	NamedList p("");
	p.addParam("check:upd_automatic","false");
	p.addParam("active:upd_automatic","false");
	p.addParam("active:upd_install","false");
	p.addParam("active:upd_check","false");
	p.addParam("active:upd_download","false");
	for (int i = 0; s_policies[i].token; i++)
	    p.addParam("active:upd_policy_" + String(s_policies[i].token),"false");
	if (Client::self())
	    Client::self()->setParams(&p);
	return false;
    }

    int policy = Engine::config().getIntValue("client",toString(),s_policies,Never);
    policy = Client::s_settings.getIntValue(toString(),"policy",s_policies,policy);
    setPolicy(policy,false);
    if (QFile::exists(filePath(false))) {
	m_install = Client::s_settings.getBoolValue(toString(),"install");
	if ((m_policy >= Install) && !m_install) {
	    Debug(toString(),DebugNote,"Deleting old updater file");
	    QFile::remove(filePath(false));
	}
    }
    Client::self()->setActive("upd_install",m_install);
    if (m_install && (m_policy >= Install))
	startInstalling();
    else if (m_policy >= Check)
	startChecking();
    return false;
}

void UpdateLogic::exitingClient()
{
    startDownloading(false);
    startChecking(false);
    stopHttp();
    delete m_httpSlots;
    m_httpSlots = 0;
}

bool UpdateLogic::action(Window* wnd, const String& name, NamedList* params)
{
    if (!m_canUpdate)
	return false;
    if (name == "upd_install")
	startInstalling();
    else if (name == "upd_check")
	startChecking();
    else if (name == "upd_download")
	startDownloading();
    else
	return false;
    return true;
}

bool UpdateLogic::toggle(Window* wnd, const String& name, bool active)
{
    if (!m_canUpdate)
	return false;
    if (!name.startsWith("upd_"))
	return false;
    if (name == "upd_check")
	startChecking(active);
    else if (name == "upd_download")
	startDownloading(active);
    else if (name == "upd_automatic")
	setPolicy(active ? Install : Never,true);
    else if (active) {
	String tmp = name;
	if (tmp.startSkip("upd_policy_",false))
	    setPolicy(lookup(tmp,s_policies,Invalid),true);
    }
    return true;
}

void UpdateLogic::setPolicy(int policy, bool save)
{
    if ((policy == Invalid) || (policy == m_policy))
	return;
    const char* pol = lookup(policy,s_policies);
    if (!pol)
	return;
    m_policy = policy;
    if (save) {
	Client::s_settings.setValue(toString(),"policy",pol);
	Client::save(Client::s_settings);
    }
    if (!Client::self())
	return;
    for (policy = Never; policy <= Install; policy++) {
	String tmp = "upd_policy_";
	tmp += lookup(policy,s_policies);
	Client::self()->setCheck(tmp,(policy == m_policy));
    }
    Client::self()->setCheck("upd_automatic",(Install == m_policy));
}

void UpdateLogic::startChecking(bool start)
{
    String url = Engine::config().getValue("client","updateurl");
    Engine::runParams().replaceParams(url);
    if (url.trimBlanks().null()) {
	start = false;
	if (Client::self()) {
	    Client::self()->setActive("upd_check",false);
	    Client::self()->setActive("upd_download",false);
	    Client::self()->setActive("upd_install",false);
	}
    }
    if (start) {
	Debug(toString(),DebugNote,"Checking new version: %s",url.c_str());
	m_checked = false;
	m_checking = true;
	start = startHttp(url,"");
	if (Client::self()) {
	    Client::self()->setActive("upd_download",false);
	    Client::self()->setSelect("upd_progress","0");
	    Client::self()->setText("upd_version","");
	}
    }
    else
	stopHttp();
    if (Client::self())
	Client::self()->setCheck("upd_check",start);
}

void UpdateLogic::startDownloading(bool start)
{
    m_checking = false;
    if (start && m_install) {
	m_install = false;
	Client::s_settings.setValue(toString(),"install",String::boolText(false));
	Client::save(Client::s_settings);
    }
    if (start) {
	Debug(toString(),DebugNote,"Downloading from: %s",m_url.c_str());
	start = startHttp(m_url,filePath(true));
    }
    else {
	stopHttp();
	QFile::remove(filePath(true));
    }
    if (Client::self()) {
	Client::self()->setActive("upd_check",!start);
	Client::self()->setActive("upd_install",m_install);
	Client::self()->setCheck("upd_download",start);
	Client::self()->setSelect("upd_progress","0");
    }
}

void UpdateLogic::startInstalling()
{
    if (!QFile::exists(filePath(false)))
	return;
    QString cmd = Engine::config().getValue("client","updatecmd");
    if (!cmd.isEmpty()) {
	String tmp = cmd.toUtf8().constData();
	NamedList params(Engine::runParams());
	params.setParam("filename",filePath(false).toUtf8().constData());
	params.replaceParams(tmp);
	if (tmp.trimBlanks().null())
	    return;
	cmd = QString::fromUtf8(tmp.c_str());
    }
    else
	cmd = filePath(false);
    if (QProcess::startDetached(cmd)) {
	Debug(toString(),DebugNote,"Executing: %s",cmd.toUtf8().constData());
	Client::s_settings.setValue(toString(),"install",String::boolText(false));
	Client::save(Client::s_settings);
	Engine::halt(0);
	return;
    }
    Debug(toString(),DebugWarn,"Failed to execute: %s",cmd.toUtf8().constData());
}

void UpdateLogic::finishedChecking()
{
    if (Client::self()) {
	Client::self()->setCheck("upd_check",false);
	Client::self()->setActive("upd_download",m_checked);
	Client::self()->setSelect("upd_progress","0");
    }
    if (m_checked && (m_policy >= Download))
	startDownloading();
}

void UpdateLogic::finishedDownloading()
{
    if (Client::self()) {
	Client::self()->setCheck("upd_download",false);
	Client::self()->setActive("upd_check",true);
	Client::self()->setActive("upd_install",m_install);
	if (!m_install)
	    Client::self()->setSelect("upd_progress","0");
    }
    Client::s_settings.setValue(toString(),"install",String::boolText(m_install));
    Client::save(Client::s_settings);
}

QString UpdateLogic::filePath(bool temp)
{
    return QString::fromUtf8((Engine::configPath(true) + Engine::pathSeparator() + toString() +
	(temp ? TMP_EXT : EXE_EXT)));
}

bool UpdateLogic::startHttp(const char* url, const QString& saveAs)
{
    stopHttp();
    QUrl qurl(QString::fromUtf8(url));
    if (!qurl.isValid())
	return false;
    QFile* file = 0;
    if (!saveAs.isEmpty()) {
	QFile::remove(saveAs);
	file = new QFile(saveAs);
	if (!(file->open(QIODevice::WriteOnly) &&
	    file->setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner))) {
	    file->remove();
	    delete file;
	    return false;
	}
	m_file = file;
    }
    if (!m_httpSlots)
	m_httpSlots = new QtUpdateHttp(this);
    m_http = m_httpSlots->http();
    const char* proxy = Client::s_settings.getValue(toString(),"proxy_host");
    if (proxy)
	m_http->setProxy(proxy,
	    Client::s_settings.getIntValue(toString(),"proxy_port",8080),
	    Client::s_settings.getValue(toString(),"proxy_user"),
	    Client::s_settings.getValue(toString(),"proxy_pass"));
    m_http->setHost(qurl.host(),qurl.port(80));
    m_http->get(qurl.path(),file);
    return true;
}

void UpdateLogic::stopHttp()
{
    QHttp* http = m_http;
    m_http = 0;
    if (http) {
	http->abort();
	delete http;
    }
    stopFile();
}

void UpdateLogic::stopFile()
{
    QFile* file = m_file;
    m_file = 0;
    delete file;
}

void UpdateLogic::gotPercentage(int percent)
{
    if (!Client::self())
	return;
    Client::self()->setSelect("upd_progress",String(percent));
}

void UpdateLogic::endHttp(bool error)
{
    stopFile();
    if (!m_http)
	return;
    if (m_checking) {
	if (!error) {
	    QByteArray data = m_http->readAll();
	    if (data.size() <= 1024) {
		String str(data.constData());
		// 1st row is the URL, everything else description
		int nl = str.find('\n');
		if (nl > 0) {
		    int len = (str.at(nl - 1) == '\r') ? (nl - 1) : nl;
		    URI url(str.substr(0,len));
		    url.trimBlanks();
		    if (url.getProtocol() == "http") {
			m_checked = true;
			m_url = url;
			if (Client::self())
			    Client::self()->setText("upd_version",str.substr(nl+1));
		    }
		}
	    }
	}
	finishedChecking();
    }
    else {
	if (!error) {
	    QFileInfo info(filePath(true));
	    if ((info.size() >= MIN_SIZE) && (info.size() <= MAX_SIZE)) {
		QFile::remove(filePath(false));
		m_install = QFile::rename(filePath(true),filePath(false));
	    }
	}
	QFile::remove(filePath(true));
	finishedDownloading();
    }
}


QHttp* QtUpdateHttp::http()
{
    QHttp* h = new QHttp(this);
    connect(h,SIGNAL(dataReadProgress(int,int)),this,SLOT(dataProgress(int,int)));
    connect(h,SIGNAL(done(bool)),this,SLOT(requestDone(bool)));
    return h;
}

void QtUpdateHttp::dataProgress(int done, int total)
{
    if (!m_logic)
	return;
    int percent = 0;
    if (done)
	percent = (done <= total) ? (done * 100 / total) : 50;
    m_logic->gotPercentage(percent);
}

void QtUpdateHttp::requestDone(bool error)
{
    if (m_logic)
	m_logic->endHttp(error);
}


Updater::Updater()
    : Plugin("updater",true), m_logic(0)
{
    Output("Loaded module Updater");
}

Updater::~Updater()
{
    Output("Unloading module Updater");
    TelEngine::destruct(m_logic);
}

void Updater::initialize()
{
    Output("Initializing module Updater");
    if (m_logic)
	return;
    m_logic = new UpdateLogic("updater");
}

}; // anonymous namespace

#include "updater.moc"

/* vi: set ts=8 sw=4 sts=4 noet: */
