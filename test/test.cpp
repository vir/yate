/*
    test.c
    This file holds the entry point of the Telephony Engine
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

/*
 * vim:ts=4:et:sw=4:ht=8
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2006 Null Team
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */
