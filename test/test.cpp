/*
    test.c
    This file holds the entry point of the Telephony Engine
*/

#include <telengine.h>

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
 */
