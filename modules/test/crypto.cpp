/**
 * crypto.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Cryptographic functions test
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

class TestCrypto : public Plugin
{
public:
    TestCrypto();
    virtual void initialize();
    void report(const char* test, const String& result, const char* expect);
};

TestCrypto::TestCrypto()
    : Plugin("testcrypto")
{
    Output("Hello, I am module TestCrypto");
}

void TestCrypto::report(const char* test, const String& result, const char* expect)
{
    if (result == expect)
	Debug(test,DebugInfo,"Computed expected '%s'",expect);
    else
	Debug(test,DebugWarn,"Computed '%s' but expected '%s'",result.c_str(),expect);
}

void TestCrypto::initialize()
{
    Output("Initializing module TestCrypto");

    MD5 md5("");
    report("md5-1",md5.hexDigest(),"d41d8cd98f00b204e9800998ecf8427e");
    md5.clear();
    md5 << "The quick brown fox jumps over the lazy dog";
    MD5 md_5(md5);
    report("md5-2",md5.hexDigest(),"9e107d9d372bb6826bd81d3542a419d6");
    md_5 << ".";
    report("md5-3",md_5.hexDigest(),"e4d909c290d0fb1ca068ffaddf22cbd0");
    md5.hmac("","");
    report("md5-hmac-1",md5.hexDigest(),"74e6f7298a9c2d168935f58c001bad88");
    md5.hmac("key","The quick brown fox jumps over the lazy dog");
    report("md5-hmac-2",md5.hexDigest(),"80070713463e7749b90c2dc24911e275");

    SHA1 sha1("");
    report("sha1-1",sha1.hexDigest(),"da39a3ee5e6b4b0d3255bfef95601890afd80709");
    sha1.clear();
    sha1 << "The quick brown fox jumps over the lazy ";
    SHA1 sha_1(sha1);
    sha1 << "dog";
    report("sha1-2",sha1.hexDigest(),"2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
    sha_1 << "cog";
    report("sha1-3",sha_1.hexDigest(),"de9f2c7fd25e1b3afad3e85a0bd17d9b100db4b3");
    sha1.hmac("","");
    report("sha1-hmac-1",sha1.hexDigest(),"fbdb1d1b18aa6c08324b7d64b71fb76370690e1d");
    sha1.hmac("key","The quick brown fox jumps over the lazy dog");
    report("sha1-hmac-2",sha1.hexDigest(),"de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9");

    SHA256 sha256("");
    report("sha256-1",sha256.hexDigest(),"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256.clear();
    sha256 << "The quick brown fox jumps over the lazy dog";
    SHA256 sha_256(sha256);
    report("sha256-2",sha256.hexDigest(),"d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
    sha_256 << ".";
    report("sha256-3",sha_256.hexDigest(),"ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c");
    sha256.hmac("","");
    report("sha256-hmac-1",sha256.hexDigest(),"b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad");
    sha256.hmac("key","The quick brown fox jumps over the lazy dog");
    report("sha256-hmac-2",sha256.hexDigest(),"f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
    sha256.hmac("0123456789abcdef","The quick brown fox jumps over the lazy dog");
    report("sha256-hmac-3",sha256.hexDigest(),"a3e7e77cecd85e7a46b1a1418702af9dfac4f480d5d489713f1a299c062711c3");
    sha256.hmac("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","The quick brown fox jumps over the lazy dog");
    report("sha256-hmac-4",sha256.hexDigest(),"2f80345156e6d8cc67c450d31c403a3440913081c8bce9737188439c8cdeb15d");
    DataBlock pad;
    sha256.hmacStart(pad,"otherkey");
    sha256.update("The quick brown fox");
    sha256.update(" jumps over");
    sha256.update(" the lazy dog");
    sha256.hmacFinal(pad);
    report("sha256-hmac-5",sha256.hexDigest(),"adea30df7e096340a0532da97d7cd62919cbfb41075d3597fd61b78f679c2a40");

    DataBlock seed, out;
    seed.unHexify("bd029bbe7f51960bcf9edb2b61f06f0feb5a38b6");
    SHA1::fips186prf(out,seed,40);
    String str;
    str.hexify(out.data(),out.length());
    report("fips-186-prf",str,"2070b3223dba372fde1c0ffc7b2e3b498b2606143c6c18bacb0f6c55babb13788e20d737a3275116");
}

INIT_PLUGIN(TestCrypto);

/* vi: set ts=8 sw=4 sts=4 noet: */
