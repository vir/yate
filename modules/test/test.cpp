/**
 * test.cpp
 * This file is part of the YATE Project http://YATE.null.ro
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

#include <yatengine.h>

using namespace TelEngine;

class TestPlugin : public Plugin
{
public:
    TestPlugin();
    virtual void initialize();
};

TestPlugin::TestPlugin()
{
    Output("Hello, I am module TestPlugin");
}

void TestPlugin::initialize()
{
    Output("Initializing module TestPlugin");
//    Regexp r("\\([a-z]*\\)\\(.*\\)");
    Regexp r("\\([a-z]\\+\\)\\([0-9]\\+\\)");
    String s("123abc456xyz");
    if (s.matches(r)) {
	Output("Found %d matches of '%s' in '%s'",s.matchCount(),r.c_str(),s.c_str());
	for (int i=0; i<=s.matchCount(); i++)
	    Output("match[%d]='%s' pos=%d len=%d",i,s.matchString(i).c_str(),s.matchOffset(i),s.matchLength(i));
	String t("\\0-ABC-\\1-DEF-\\2-GHI-\\\\");
	Output("Replacing matches in '%s' got '%s'",t.c_str(),s.replaceMatches(t).c_str());
    }
    r = "[a-z]\\+[0-9]\\+";
    s.matches(r);
    Output("Found %d matches of '%s' in '%s'",s.matchCount(),r.c_str(),s.c_str());
    for (int i=0; i<=s.matchCount(); i++)
	Output("match[%d]='%s' pos=%d len=%d",i,s.matchString(i).c_str(),s.matchOffset(i),s.matchLength(i));
}

INIT_PLUGIN(TestPlugin);

/* vi: set ts=8 sw=4 sts=4 noet: */
