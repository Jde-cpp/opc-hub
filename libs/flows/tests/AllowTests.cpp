#include <jde/flows/Allow.h>
#include <jde/flows/Template.h>
#include "helpers.h"

//The match an `http request` makes of a url's host against /gateway/flows/http/allow (docs/flows/spec.md *Sinks*), and the
//mustache a host's node writes text with.
namespace Jde::Opc::Flows::Tests{
	Ω Allowed( sv host, vector<string> allow )ι->bool{ return HostAllowed( host, allow ); }

	TEST( AllowTests, AnEmptyListAllowsNone ){
		EXPECT_FALSE( Allowed("example.com", {}) );
		EXPECT_FALSE( Allowed("127.0.0.1", {}) );
		EXPECT_FALSE( Allowed("", {"*"}) ) << "no host is no match";
		EXPECT_FALSE( Allowed("example.com", {""}) ) << "an empty entry names nothing";
		EXPECT_FALSE( Allowed("evil.org@hooks.example.com", {"*.example.com"}) ) << "a user is no part of a host";
		EXPECT_FALSE( Allowed("hooks.example.com/path", {"hooks.example.com*"}) );
		EXPECT_FALSE( Allowed("hooks.example.com?x", {"*"}) );
	}
	TEST( AllowTests, ANameIsItself ){
		EXPECT_TRUE( Allowed("example.com", {"example.com"}) );
		EXPECT_TRUE( Allowed("Example.COM", {"example.com"}) ) << "a host has no case";
		EXPECT_TRUE( Allowed("example.com", {"EXAMPLE.com"}) );
		EXPECT_TRUE( Allowed("example.com.", {"example.com"}) ) << "the root's dot is the same name";
		EXPECT_TRUE( Allowed("127.0.0.1", {"hooks.example.com", "127.0.0.1"}) ) << "any entry";
		EXPECT_FALSE( Allowed("example.com.evil.org", {"example.com"}) );
		EXPECT_FALSE( Allowed("evil-example.com", {"example.com"}) );
		EXPECT_FALSE( Allowed("sub.example.com", {"example.com"}) ) << "a name is not its subdomains";
		EXPECT_FALSE( Allowed("localhost", {"127.0.0.1"}) ) << "names are compared, not addresses";
	}
	TEST( AllowTests, APatternIsAGlob ){
		EXPECT_TRUE( Allowed("hooks.example.com", {"*.example.com"}) );
		EXPECT_TRUE( Allowed("a.b.example.com", {"*.example.com"}) ) << "a star is any run, dots among it";
		EXPECT_FALSE( Allowed("example.com", {"*.example.com"}) );
		EXPECT_FALSE( Allowed("example.com.evil.org", {"*.example.com"}) );
		EXPECT_TRUE( Allowed("10.0.7.5", {"10.0.?.5"}) );
		EXPECT_FALSE( Allowed("10.0.17.5", {"10.0.?.5"}) ) << "a question mark is one character";
		EXPECT_TRUE( Allowed("plant-3.local", {"plant-*.local"}) );
		EXPECT_TRUE( Allowed("plant-.local", {"plant-*.local"}) ) << "a star is none too";
		EXPECT_TRUE( Allowed("anything.at.all", {"*"}) );
		EXPECT_TRUE( Allowed("abcabd", {"*ab?"}) ) << "a star backs up";
		EXPECT_FALSE( Allowed("abcabcd", {"*ab?"}) );
	}
	TEST( AllowTests, AnIPv6LiteralIsItsAddress ){
		EXPECT_TRUE( Allowed("[::1]", {"::1"}) );
		EXPECT_TRUE( Allowed("::1", {"[::1]"}) );
		EXPECT_TRUE( Allowed("[FE80::1]", {"fe80::*"}) );
		EXPECT_FALSE( Allowed("[::2]", {"::1"}) );
	}

	TEST( AllowTests, AMustacheRendersAsItIsWhereNothingIsHtml ){
		const Message m{ jobject{{"payload", "a/b <c>"}, {"n", 1450.5}} };
		const Store flow{ jobject{{"line", "L1"}} };
		const GlobalStore global{ jobject{{"site", "plant"}} };
		const Mustache line{ "{{flow.line}}@{{global.site}}:  {{payload}} at {{n}}{{! a comment}}{{absent}}" };
		EXPECT_EQ( line.Render(m, flow, global, false), "L1@plant:  a/b <c> at 1450.5" );
		EXPECT_EQ( line.Render(m, flow, global), "L1@plant:  a&#x2F;b &lt;c&gt; at 1450.5" ) << "escaped as mustache.js escapes, where it is";
		EXPECT_THROW( Mustache{"{{#each}}"}, std::exception );
		EXPECT_THROW( Mustache{"{{open"}, std::exception );
	}
}