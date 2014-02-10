/**
 * dbwave.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Wave record+playback helper for database storage, uses wavefile
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

#include <yatephone.h>

using namespace TelEngine;
namespace { // anonymous

class DbWriter : public MemoryStream, public GenObject
{
public:
    inline DbWriter(const char* account, const char* query)
	: m_account(account), m_query(query)
	{ }
    virtual ~DbWriter();
    virtual void* getObject(const String& name) const;
private:
    String m_account;
    String m_query;
};

class DbWave : public Plugin
{
public:
    DbWave();
    virtual void initialize();
private:
    bool m_init;
};

INIT_PLUGIN(DbWave);

class AttachHandler : public MessageHandler
{
public:
    AttachHandler() : MessageHandler("chan.attach",90,__plugin.name()) { }
    virtual bool received(Message& msg);
};

class RecordHandler : public MessageHandler
{
public:
    RecordHandler() : MessageHandler("chan.record",90,__plugin.name()) { }
    virtual bool received(Message& msg);
};

class ExecuteHandler : public MessageHandler
{
public:
    ExecuteHandler() : MessageHandler("call.execute",90,__plugin.name()) { }
    virtual bool received(Message& msg);
};


static void alterSource(Message& msg, const String& name)
{
    const String* param = msg.getParam(name);
    if (!param)
	return;
    if (!param->startsWith("dbwave/play/"))
	return;
    const char* account = msg.getValue(name + "_account");
    const char* query = msg.getValue(name + "_query");
    String file = param->substr(2);
    if (msg.getBoolValue(name + "_fallback",true))
	msg.setParam(name,file);
    if (!(account && query))
	return;
    Message m("database");
    m.addParam("account",account);
    m.addParam("query",query);
    if (!Engine::dispatch(m) || (m.getIntValue(YSTRING("rows")) != 1) || (m.getIntValue(YSTRING("columns")) != 1))
	return;
    Array* a = static_cast<Array*>(m.userObject(YATOM("Array")));
    if (!a)
	return;
    GenObject* obj = a->take(0,1);
    DataBlock* data = YOBJECT(DataBlock,obj);
    if (!data) {
	if (obj) {
	    GenObject* col = a->get(0,0);
	    Debug(DebugMild,"DbWave got on column '%s' non-binary data '%s'",
		(col ? col->toString().c_str() : ""),obj->toString().c_str());
	}
	TelEngine::destruct(obj);
	return;
    }
    DDebug(DebugInfo,"Query for '%s' account '%s' returned %u bytes",
	name.c_str(),account,data->length());
    msg.setParam(new NamedPointer(name,data,file));
}

static void alterConsumer(Message& msg, const String& name)
{
    const String* param = msg.getParam(name);
    if (!param)
	return;
    if (!param->startsWith("dbwave/record/"))
	return;
    const char* account = msg.getValue(name + "_account");
    const char* query = msg.getValue(name + "_query");
    if (!(account && query)) {
	if (msg.getBoolValue(name + "_fallback",false))
	    msg.setParam(name,param->substr(2));
	return;
    }
    DDebug(DebugInfo,"Creating DbWriter for '%s' account '%s' query='%s'",
	name.c_str(),account,query);
    msg.setParam(new NamedPointer(name,new DbWriter(account,query),param->substr(2)));
}


// chan.attach handler
bool AttachHandler::received(Message& msg)
{
    alterSource(msg,"source");
    alterConsumer(msg,"consumer");
    alterSource(msg,"override");
    alterSource(msg,"replace");
    return false;
}


// chan.record handler
bool RecordHandler::received(Message& msg)
{
    alterConsumer(msg,"call");
    alterConsumer(msg,"peer");
    return false;
}


// call.execute handler
bool ExecuteHandler::received(Message& msg)
{
    alterSource(msg,"callto");
    alterConsumer(msg,"callto");
    return false;
}


DbWriter::~DbWriter()
{
    if (data().null()) {
	Debug(DebugNote,"DbWriter collected no data!");
	return;
    }
    NamedList params("");
    params.addParam(new NamedPointer("data",new DataBlock(data())));
    params.addParam("length",String(data().length()));
    params.replaceParams(m_query,true);
    DDebug(DebugInfo,"DbWriter data size: %u query size: %u",data().length(),m_query.length());
    Message* m = new Message("database");
    m->addParam("account",m_account);
    m->addParam("query",m_query);
    Engine::enqueue(m);
}

void* DbWriter::getObject(const String& name) const
{
    if (name == YATOM("Stream"))
	return static_cast<Stream*>(const_cast<DbWriter*>(this));
    return GenObject::getObject(name);
}


DbWave::DbWave()
    : Plugin("dbwave",true), m_init(true)
{
    Output("Loaded module DbWave");
}

void DbWave::initialize()
{
    Output("Initializing module DbWave");
    if (m_init) {
	m_init = false;
	Engine::install(new AttachHandler);
	Engine::install(new RecordHandler);
	Engine::install(new ExecuteHandler);
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
