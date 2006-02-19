/**
 * Message.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004, 2005 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "yatengine.h"
#include <string.h>

using namespace TelEngine;

Message::Message(const char* name, const char* retval)
    : NamedList(name), m_return(retval), m_data(0), m_notify(false)
{
    XDebug(DebugAll,"Message::Message(\"%s\",\"%s\") [%p]",name,retval,this);
}

Message::~Message()
{
    XDebug(DebugAll,"Message::~Message() '%s' [%p]",c_str(),this);
    userData(0);
}

void* Message::getObject(const String& name) const
{
    if (name == "Message")
	return const_cast<Message*>(this);
    return NamedList::getObject(name);
}

void Message::userData(RefObject* data)
{
    if (data == m_data)
	return;
    m_notify = false;
    RefObject* tmp = m_data;
    if (data && !data->ref())
	data = 0;
    m_data = data;
    if (tmp)
	tmp->deref();
}

void Message::dispatched(bool accepted)
{
    if (!m_notify)
	return;
    MessageNotifier* hook = YOBJECT(MessageNotifier,m_data);
    if (hook)
	hook->dispatched(*this,accepted);
}

String Message::encode(const char* id) const
{
    String s("%%>message:");
    s << String::msgEscape(id) << ":" << (unsigned int)m_time.sec() << ":";
    commonEncode(s);
    return s;
}

String Message::encode(bool received, const char* id) const
{
    String s("%%<message:");
    s << String::msgEscape(id) << ":" << received << ":";
    commonEncode(s);
    return s;
}

int Message::decode(const char* str, String& id)
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
    id.assign(str+s.length(),(sep-str)-s.length());
    int err = -1;
    id = id.msgUnescape(&err,':');
    if (err >= 0)
	return err+s.length();
    String t(sep+1,sep2-sep-1);
    unsigned int tm = 0;
    t >> tm;
    if (!t.null())
	return sep-str;
    m_time=tm ? ((u_int64_t)1000000)*tm : Time::now();
    return commonDecode(str,sep2-str+1);
}

int Message::decode(const char* str, bool& received, const char* id)
{
    String s("%%<message:");
    s << id << ":";
    if (!str || ::strncmp(str,s.c_str(),s.length()))
	return -1;
    // locate the SEP after received
    const char *sep = ::strchr(str+s.length(),':');
    if (!sep)
	return s.length();
    String rcvd(str+s.length(),(sep-str)-s.length());
    rcvd >> received;
    if (!rcvd.null())
	return s.length();
    return sep[1] ? commonDecode(str,sep-str+1) : -2;
}

void Message::commonEncode(String& str) const
{
    str << msgEscape() << ":" << m_return.msgEscape();
    unsigned n = length();
    for (unsigned i = 0; i < n; i++) {
	NamedString *s = getParam(i);
	if (s)
	    str << ":" << s->name().msgEscape('=') << "=" << s->msgEscape();
    }
}

int Message::commonDecode(const char* str, int offs)
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

MessageHandler::MessageHandler(const char* name, unsigned priority)
    : String(name), m_priority(priority), m_dispatcher(0), m_filter(0)
{
    XDebug(DebugAll,"MessageHandler::MessageHandler(\"%s\",%u) [%p]",name,priority,this);
}

MessageHandler::~MessageHandler()
{
    XDebug(DebugAll,"MessageHandler::~MessageHandler() [%p]",this);
    if (m_dispatcher)
	m_dispatcher->uninstall(this);
    clearFilter();
}

void MessageHandler::setFilter(NamedString* filter)
{
    clearFilter();
    m_filter = filter;
}

void MessageHandler::clearFilter()
{
    if (m_filter) {
	NamedString* tmp = m_filter;
	m_filter = 0;
	delete tmp;
    }
}

MessageDispatcher::MessageDispatcher()
    : m_changes(0)
{
    XDebug(DebugAll,"MessageDispatcher::MessageDispatcher() [%p]",this);
}

MessageDispatcher::~MessageDispatcher()
{
    XDebug(DebugAll,"MessageDispatcher::~MessageDispatcher() [%p]",this);
    m_mutex.lock();
    m_handlers.clear();
    m_hooks.clear();
    m_mutex.unlock();
}

bool MessageDispatcher::install(MessageHandler* handler)
{
    XDebug(DebugAll,"MessageDispatcher::install(%p)",handler);
    if (!handler)
	return false;
    Lock lock(m_mutex);
    ObjList *l = m_handlers.find(handler);
    if (l)
	return false;
    unsigned p = handler->priority();
    int pos = 0;
    for (l=&m_handlers; l; l=l->next(),pos++) {
	MessageHandler *h = static_cast<MessageHandler *>(l->get());
	if (!h)
	    continue;
	if (h->priority() < p)
	    continue;
	if (h->priority() > p)
	    break;
	// at the same priority we sort them in pointer address order
	if (h > handler)
	    break;
    }
    m_changes++;
    if (l) {
	XDebug(DebugAll,"Inserting handler [%p] on place #%d",handler,pos);
	l->insert(handler);
    }
    else {
	XDebug(DebugAll,"Appending handler [%p] on place #%d",handler,pos);
	m_handlers.append(handler);
    }
    handler->m_dispatcher = this;
    if (handler->null())
	Debug(DebugInfo,"Registered broadcast message handler %p",handler);
    return true;
}

bool MessageDispatcher::uninstall(MessageHandler* handler)
{
    XDebug(DebugAll,"MessageDispatcher::uninstall(%p)",handler);
    Lock lock(m_mutex);
    handler = static_cast<MessageHandler *>(m_handlers.remove(handler,false));
    if (handler) {
	m_changes++;
	handler->m_dispatcher = 0;
    }
    return (handler != 0);
}

bool MessageDispatcher::dispatch(Message& msg)
{
#ifdef XDEBUG
    Debugger debug("MessageDispatcher::dispatch","(%p) (\"%s\")",&msg,msg.c_str());
#endif
#ifndef NDEBUG
    u_int64_t t = Time::now();
#endif
    bool retv = false;
    ObjList *l = &m_handlers;
    m_mutex.lock();
    for (; l; l=l->next()) {
	MessageHandler *h = static_cast<MessageHandler*>(l->get());
	if (h && (h->null() || *h == msg)) {
	    if (h->filter() && (*(h->filter()) != msg.getValue(h->filter()->name())))
		continue;
	    unsigned int c = m_changes;
	    unsigned int p = h->priority();
	    m_mutex.unlock();
#ifdef DEBUG
	    u_int64_t tm = Time::now();
#endif
	    retv = h->received(msg);
#ifdef DEBUG
	    tm = Time::now() - tm;
	    if (tm > 200000)
		Debug(DebugInfo,"Message '%s' [%p] passed trough %p in " FMT64U " usec",
		    msg.c_str(),&msg,h,tm);
#endif
	    if (retv)
		break;
	    m_mutex.lock();
	    if (c == m_changes)
		continue;
	    // the handler list has changed - find again
	    NDebug(DebugAll,"Rescanning handler list for '%s' [%p] at priority %u",
		msg.c_str(),&msg,p);
	    for (l = &m_handlers; l; l=l->next()) {
		MessageHandler *mh = static_cast<MessageHandler*>(l->get());
		if (!mh)
		    continue;
		if (mh == h)
		    // exact match - silently continue where we left
		    break;

		// gone past last handler priority - exit with last handler
		if ((mh->priority() > p) || ((mh->priority() == p) && (mh > h))) {
		    Debug(DebugAll,"Handler list for '%s' [%p] changed, skipping from %p (%u) to %p (%u)",
			msg.c_str(),&msg,h,p,mh,mh->priority());
		    break;
		}
	    }
	}
    }
    if (!l)
	m_mutex.unlock();
    msg.dispatched(retv);
#ifndef NDEBUG
    t = Time::now() - t;
    if (t > 200000) {
	unsigned n = msg.length();
	String p;
	for (unsigned i = 0; i < n; i++) {
	    NamedString *s = msg.getParam(i);
	    if (s)
		p << "\n  ['" << s->name() << "']='" << *s << "'";
	}
	Debug("Performance",DebugMild,"Message %p '%s' retval '%s' returned %s in " FMT64U " usec%s",
	    &msg,msg.c_str(),msg.retValue().c_str(),retv ? "true" : "false",t,p.safe());
    }
#endif
    l = &m_hooks;
    for (; l; l=l->next()) {
	MessagePostHook *h = static_cast<MessagePostHook*>(l->get());
	if (h)
	    h->dispatched(msg,retv);
    }
    return retv;
}

bool MessageDispatcher::enqueue(Message* msg)
{
    Lock lock(m_mutex);
    if (!msg || m_messages.find(msg))
	return false;
    m_messages.append(msg);
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

unsigned int MessageDispatcher::messageCount()
{
    Lock lock(m_mutex);
    return m_messages.count();
}

unsigned int MessageDispatcher::handlerCount()
{
    Lock lock(m_mutex);
    return m_handlers.count();
}

void MessageDispatcher::setHook(MessagePostHook* hook, bool remove)
{
    m_mutex.lock();
    if (remove)
	m_hooks.remove(hook,false);
    else
	m_hooks.append(hook);
    m_mutex.unlock();
}


MessageNotifier::~MessageNotifier()
{
}

/* vi: set ts=8 sw=4 sts=4 noet: */
