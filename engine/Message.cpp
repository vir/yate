/**
 * Message.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 */

#include "telengine.h"
#include <string.h>

using namespace TelEngine;

Message::Message(const char *name, const char *retval)
    : NamedList(name), m_return(retval), m_data(0)
{
#ifdef DEBUG
    Debug(DebugAll,"Message::Message(\"%s\",\"%s\") [%p]",name,retval,this);
#endif
}

String Message::encode(const char *id) const
{
    String s("%%>message:");
    s << String::msgEscape(id,':') << ":" << (unsigned int)m_time.sec() << ":";
    commonEncode(s);
    return s;
}

String Message::encode(bool received, const char *id) const
{
    String s("%%<message:");
    s << String::msgEscape(id,':') << ":" << received << ":";
    commonEncode(s);
    return s;
}

int Message::decode(const char *str, String &id)
{
    String s("%%>message:");
    if (!str || ::strncmp(str,s.c_str(),s.length()))
	return -1;
    // locate the SEP after id
    const char *sep = ::strchr(str+s.length(),':');
    if (!sep)
	return s.length();
    // locate the SEP after time
    const char *sep2 = ::strchr(sep+1,':');
    if (!sep2)
	return sep-str;
    id.assign(str+s.length(),(sep-str)-s.length()-1);
    int err = -1;
    id = id.msgUnescape(&err,':');
    if (err >= 0)
	return err+s.length();
    String t(sep+1,sep2-sep-1);
    unsigned int tm;
    t >> tm;
    if (!t.null())
	return sep-str;
    m_time=1000000ULL*tm;
    return commonDecode(str,sep2-str);
}

int Message::decode(const char *str, bool &received, const char *id)
{
    String s("%%<message:");
    s << id << ":";
    if (!str || ::strncmp(str,s.c_str(),s.length()))
	return -1;
    // locate the SEP after received
    const char *sep = ::strchr(str+s.length(),':');
    if (!sep)
	return s.length();
    String rcvd(str+s.length(),(sep-str)-s.length()-1);
    rcvd >> received;
    if (!rcvd.null())
	return s.length();
    return commonDecode(str,sep-str);
}

void Message::commonEncode(String &str) const
{
    str << msgEscape(':') << ":" << m_return.msgEscape(':');
    unsigned n = length();
    for (unsigned i = 0; i < n; i++) {
	NamedString *s = getParam(i);
	if (s)
	    str << ":" << s->name().msgEscape(':') << "=" << s->msgEscape(':');
    }
}

int Message::commonDecode(const char *str, int offs)
{
    str += offs;
    // locate SEP after name
    const char *sep = ::strchr(str,':');
    if (!sep)
	return offs;
    String chunk(str,sep-str);
    int err = -1;
    chunk = chunk.msgUnescape(&err,':');
    if (err >= 0)
	return offs+err;
    if (!chunk.null())
	*this = chunk;
    offs += (sep-str+1);
    str = sep+1;
    // locate SEP or EOL after retval
    sep = ::strchr(str,':');
    if (sep)
	chunk.assign(str,sep-str);
    else
	chunk.assign(str);
    chunk = chunk.msgUnescape(&err,':');
    if (err >= 0)
	return offs+err;
    m_return = chunk;
    // find and assign name=value pairs
    while (sep) {
	offs += (sep-str+1);
	str = sep+1;
	sep = ::strchr(str,':');
	if (sep)
	    chunk.assign(str,sep-str);
	else
	    chunk.assign(str);
	if (chunk.null())
	    continue;
	chunk = chunk.msgUnescape(&err,':');
	if (err >= 0)
	    return offs+err;
	int pos = chunk.find('=');
	switch (pos) {
	    case -1:
		clearParam(chunk);
		break;
	    case 0:
		return offs+err;
	    default:
		setParam(chunk.substr(0,pos),chunk.substr(pos+1));
	}
    }
    return -2;
}

MessageHandler::MessageHandler(const char *name, unsigned priority)
    : String(name), m_priority(priority), m_dispatcher(0)
{
#ifdef DEBUG
    Debug(DebugAll,"MessageHandler::MessageHandler(\"%s\",%u) [%p]",name,priority,this);
#endif
}

MessageHandler::~MessageHandler()
{
#ifdef DEBUG
    Debug(DebugAll,"MessageHandler::~MessageHandler() [%p]",this);
#endif
    if (m_dispatcher)
	m_dispatcher->uninstall(this);
}

MessageDispatcher::MessageDispatcher()
    : m_hook(0)
{
#ifdef DEBUG
    Debug(DebugAll,"MessageDispatcher::MessageDispatcher() [%p]",this);
#endif
}

MessageDispatcher::~MessageDispatcher()
{
#ifdef DEBUG
    Debug(DebugAll,"MessageDispatcher::~MessageDispatcher() [%p]",this);
#endif
    m_handlers.clear();
}

bool MessageDispatcher::install(MessageHandler *handler)
{
#ifdef DEBUG
    Debug(DebugAll,"MessageDispatcher::install(%p)",handler);
#endif
    if (!handler)
	return false;
    ObjList *l = m_handlers.find(handler);
    if (l)
	return false;
    unsigned p = handler->priority();
    int pos = 0;
    for (l=&m_handlers; l; l=l->next(),pos++) {
	MessageHandler *h = static_cast<MessageHandler *>(l->get());
	if (h && (h->priority() > p))
	    break;
    }
    if (l) {
#ifdef DEBUG
    Debug(DebugAll,"Inserting handler [%p] on place #%d",handler,pos);
#endif
	l->insert(handler);
    }
    else {
#ifdef DEBUG
    Debug(DebugAll,"Appending handler [%p] on place #%d",handler,pos);
#endif
	m_handlers.append(handler);
    }
    handler->m_dispatcher = this;
    if (handler->null())
	Debug(DebugInfo,"Registered broadcast message handler %p",handler);
    return true;
}

bool MessageDispatcher::uninstall(MessageHandler *handler)
{
#ifdef DEBUG
    Debug(DebugAll,"MessageDispatcher::uninstall(%p)",handler);
#endif
    handler = static_cast<MessageHandler *>(m_handlers.remove(handler,false));
    if (handler)
	handler->m_dispatcher = 0;
    return (handler != 0);
}

bool MessageDispatcher::dispatch(Message &msg)
{
#ifdef DEBUG
    Debugger debug("MessageDispatcher::dispatch","(%p) (\"%s\")",&msg,msg.c_str());
#endif
    bool retv = false;
    ObjList *l = &m_handlers;
    for (; l; l=l->next()) {
	MessageHandler *h = static_cast<MessageHandler *>(l->get());
	if (h && (h->null() || *h == msg) && h->received(msg)) {
	    retv = true;
	    break;
	}
    }
    msg.dispatched(retv);
    if (m_hook)
	(*m_hook)(msg,retv);
    return retv;
}

bool MessageDispatcher::enqueue(Message *msg)
{
    if (!msg || m_messages.find(msg))
	return false;
    m_mutex.lock();
    m_messages.append(msg);
    m_mutex.unlock();
    return true;
}

bool MessageDispatcher::dequeueOne()
{
    m_mutex.lock();
    Message *msg = static_cast<Message *>(m_messages.remove(false));
    m_mutex.unlock();
    if (!msg)
	return false;
    dispatch(*msg);
    msg->destruct();
    return true;
}

void MessageDispatcher::dequeue()
{
    while (dequeueOne())
	;
}
