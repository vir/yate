/*
    test.c
    This file holds the entry point of the Telephony Engine
*/

#include <telengine.h>

#include <unistd.h>

using namespace TelEngine;

class MsgSniff : public Plugin
{
public:
    MsgSniff();
    virtual void initialize();
private:
    bool m_first;
};

class SniffHandler : public MessageHandler
{
public:
    SniffHandler() : MessageHandler(0,0) { }
    virtual bool received(Message &msg);
};

bool SniffHandler::received(Message &msg)
{
    Output("Sniffed message '%s' time=%llu thread=%p",
	msg.c_str(),msg.msgTime().usec(),Thread::current());
    unsigned n = msg.length();
    for (unsigned i = 0; i < n; i++) {
	NamedString *s = msg.getParam(i);
	if (s)
	    Output("  param['%s']='%s'",s->name().c_str(),s->c_str());
    }
    return false;
};

MsgSniff::MsgSniff()
    : m_first(true)
{
    Output("Loaded module MsgSniffer");
}

void MsgSniff::initialize()
{
    Output("Initializing module MsgSniffer");
    if (m_first) {
	m_first = false;
	Engine::install(new SniffHandler);
    }
}

INIT_PLUGIN(MsgSniff);

/* vi: set ts=8 sw=4 sts=4 noet: */
