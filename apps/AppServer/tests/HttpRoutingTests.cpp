#include <jde/fwk/log/MemoryLog.h>
#include <jde/ql/QLAwait.h>
#include <jde/web/Jwt.h>
#include <jde/tests/HttpTest.h>
#include "helpers.h"
#include "../src/LocalClient.h"
#include "../src/WebServer.h"
#define let const auto

//the REST surface: HttpRequestAwait routing (/GoogleAuthClientId, /opcGateways, /opcServers, /login, /logout, 404s)
//plus the /graphql dispatch in Web::Server.  All plain http - the server detects ssl per connection.
namespace Jde::App::Server::Tests{
	using Web::Client::ClientHttpRes; using Web::Client::ClientHttpResException;
	namespace http = boost::beast::http;

	struct HttpRoutingTests : HttpTest{
		Ω EncodeJwt( jobject body )ι->string{
			let head = jobject{ {"alg","RS256"}, {"typ","JWT"} };
			return Str::Encode64( serialize(head), true )+"."+Str::Encode64( serialize(body), true )+"."+Str::Encode64( string{"notVerifiedHere"}, true );
		}
	};

	TEST_F( HttpRoutingTests, GoogleAuthClientId ){
		let res = Get( Port(), "/GoogleAuthClientId" );
		EXPECT_EQ( Json::AsString(res.Json(), "value"), "app-server-tests-google-client-id" );//the /http/clientSettings pointer - the old unrooted key always answered "Not Configured".
	}

	//#20: the endpoint stays anonymous - the spa has to find a gateway before there is anyone to log in as - but an
	//anonymous caller gets only what discovery needs.  The pid especially: it is what stopApplicationInstance signals.
	TEST_F( HttpRoutingTests, OpcGatewaysListsInstances ){
		auto client = Connect();
		RegisterInstance( *client, "Jde.OpcGateway", "http-gw", "gw-host", 4321, 4242 );
		auto findGateway = []( const ClientHttpRes& res )->jobject{
			for( let& jserver : Json::AsArray(res.Json(), "servers") ){
				let& o = jserver.as_object();
				if( Json::AsString(o, "instanceName")=="http-gw" )
					return o;
			}
			return {};
		};
		let anonymous = findGateway( Get(Port(), "/opcGateways") );
		ASSERT_FALSE( anonymous.empty() ) << "discovery must still work with no credential";
		EXPECT_EQ( Json::AsString(anonymous, "host"), "gw-host" );
		EXPECT_EQ( anonymous.at("port").to_number<uint32>(), 4321u );
		EXPECT_FALSE( anonymous.contains("pid") ) << "the pid stopApplicationInstance signals, handed to anyone who asks";
		EXPECT_FALSE( anonymous.contains("startTime") );
		EXPECT_FALSE( anonymous.contains("application") );

		let identified = findGateway( Get(Port(), "/opcGateways", Ƒ("{:x}", MintSession())) );
		ASSERT_FALSE( identified.empty() );
		EXPECT_EQ( Json::AsString(identified, "host"), "gw-host" );
		EXPECT_EQ( Json::AsString(identified, "application"), "Jde.OpcGateway" );
		EXPECT_EQ( identified.at("pid").to_number<uint32>(), 4242u );
		EXPECT_TRUE( identified.contains("startTime") );
		BlockVoidAwait( client->Close(true, SRCE_CUR) );
	}

	TEST_F( HttpRoutingTests, OpcServersListsInstances ){
		auto client = Connect();
		RegisterInstance( *client, "Jde.OpcServer", "http-opc", "opc-host", 4322 );
		let res = Get( Port(), "/opcServers" );
		let servers = Json::AsArray( res.Json(), "servers" );
		EXPECT_TRUE( std::ranges::any_of(servers, [](let& s){ return Json::AsString(s.as_object(), "instanceName")=="http-opc"; }) );
		BlockVoidAwait( client->Close(true, SRCE_CUR) );
	}

	TEST_F( HttpRoutingTests, LoginRequiresBearer ){
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/login", "{}"); }), http::status::unauthorized );//no Authorization at all.
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/login", "{}", "Basic dXNlcjpwd2Q="); }), http::status::unauthorized );//wrong scheme.
	}

	TEST_F( HttpRoutingTests, LoginRejectsGarbageJwt ){
		let status = FailureStatus( [&]{ Post(Port(), "/login", "{}", "Bearer not-a-jwt"); } );
		ASSERT_TRUE( status );
		EXPECT_NE( *status, http::status::ok );
	}

	//security-review #1:  a malformed n claim, unauthenticated, terminated the server - this whole suite - before any signature check.
	TEST_F( HttpRoutingTests, LoginRejectsMalformedKeyClaim ){
		let jwt = EncodeJwt( jobject{{"iat", time(nullptr)}, {"n", "!!!"}, {"e", "AQAB"}} );
		let status = FailureStatus( [&]{ Post(Port(), "/login", "{}", "Bearer "+jwt); } );
		ASSERT_TRUE( status );
		EXPECT_NE( *status, http::status::ok );
		EXPECT_EQ( Json::AsString(Get(Port(), "/GoogleAuthClientId").Json(), "value"), "app-server-tests-google-client-id" ) << "the server still answers";
	}

	//The contract the spa depends on: a rejected /login answers 401, and answers it in a form the browser will hand to
	//fetch - without Access-Control-Allow-Origin the response is blocked outright, `status` reads 0, and proto-service's
	//`if( e["status"]!=401 )` skips the re-login.  Server::HandleRequest resolves the bearer token before HttpRequestAwait
	//runs and answers this failure itself, so that funnel is what is pinned here - and since #19 it is the only place the
	//token is resolved at all, so an expired one never reaches login().  login()'s own error path (#15) was verified
	//separately by injecting a plain Exception into it: unfixed it answered 500 with no allow-origin header, the error
	//response having been built from a moved-from request.
	TEST_F( HttpRoutingTests, LoginExpiredJwtIsUnauthorizedAndReadableCrossOrigin ){
		let now = time( nullptr );
		let jwt = EncodeJwt( jobject{{"iat", now}, {"exp", now-1}} );//Jwt's parse throws a plain Exception carrying Unauthorized, not a RestException - the case that took the 500 path.
		let origin = Ƒ( "https://{}:9999", Host );//sameHost: the reflection is by host, so any port on Host is allowed.
		try{
			Post( Port(), "/login", "{}", "Bearer "+jwt, "application/json", origin );
			ADD_FAILURE() << "an expired jwt must not log in";
		}
		catch( ClientHttpResException& e ){
			EXPECT_EQ( e.Status(), http::status::unauthorized );
			EXPECT_EQ( e.Res()[http::field::access_control_allow_origin], origin ) << "the browser cannot read a response without it, so the spa never sees the 401";
		}
	}

	TEST_F( HttpRoutingTests, LogoutRemovesSession ){
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{1}, string{Host}, false )->SessionId;
		let res = Post( Port(), "/logout", "{}", Ƒ("{:x}", sessionId) );
		EXPECT_TRUE( Json::AsBool(res.Json(), "removed") );
		EXPECT_FALSE( Web::Server::Sessions::Find(sessionId) );
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/logout", "{}", Ƒ("{:x}", sessionId)); }), http::status::unauthorized );//the removed id is no longer a credential.
	}

	//install-issues #54:  a logout with no Authorization names no session, so it answers false and a signed-in session lives on.
	TEST_F( HttpRoutingTests, LogoutWithoutAuthorizationRemovesNothing ){
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{1}, string{Host}, false )->SessionId;
		EXPECT_FALSE( Json::AsBool(Post(Port(), "/logout", "{}").Json(), "removed") ) << "a logout naming no session reported one removed";
		EXPECT_TRUE( Web::Server::Sessions::Find(sessionId) );
		Web::Server::Sessions::Remove( sessionId );
	}

	TEST_F( HttpRoutingTests, UnknownTarget ){
		EXPECT_EQ( FailureStatus([&]{ Get(Port(), "/definitely-not-a-route"); }), http::status::not_found );
	}

	TEST_F( HttpRoutingTests, GraphQLPost ){
		let res = Post( Port(), "/graphql", serialize(jobject{{"query", "setting(target:\"googleAuthClientId\"){target value}"}}) );
		let data = Json::AsObject( res.Json(), "data" );
		let setting = Json::AsObject( data, "setting" );
		EXPECT_EQ( Json::AsString(setting, "target"), "googleAuthClientId" );
		EXPECT_EQ( Json::AsString(setting, "value"), "app-server-tests-google-client-id" );
	}

	TEST_F( HttpRoutingTests, GraphQLGet ){
		let res = Get( Port(), "/graphql?query=setting(target:%22googleAuthClientId%22)%7Btarget%20value%7D" );
		let data = Json::AsObject( res.Json(), "data" );
		EXPECT_EQ( Json::AsString(Json::AsObject(data, "setting"), "value"), "app-server-tests-google-client-id" );
	}

	//web-refactor A4: a GET must not have side effects.  The rule sat on a branch /graphql never reached, so it was not enforced.
	TEST_F( HttpRoutingTests, GraphQLGetRefusesMutation ){
		try{
			Get( Port(), "/graphql?query=mutation%20createUser(slug:%22getMutation%22,name:%22getMutation%22)%7Bid%7D" );
			ADD_FAILURE() << "a mutation over GET must be refused";
		}
		catch( ClientHttpResException& e ){
			EXPECT_EQ( e.Status(), http::status::bad_request );
			EXPECT_TRUE( e.Res().Body().contains("Mutations must use post.") ) << e.Res().Body();
		}
	}

	TEST_F( HttpRoutingTests, GraphQLRequiresQuery ){
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/graphql", "{}"); }), http::status::bad_request );
	}

	//GHSA-g354-grf2-r8vh: the advisory's own vector - an anonymous post of a write on a table that defines rights.  identities
	//declares no ops, so Table::Authorize tested nothing and the row landed; a write a table's ops do not declare is the system's
	//alone now, and an executer nobody knows is a 401.
	TEST_F( HttpRoutingTests, AnonymousMutationOnARightsTableIsRefused ){
		constexpr sv slug{ "ghsa-g354-http" };
		let body = serialize( jobject{{"query", Ƒ("mutation createIdentity( name:\"{0}\", slug:\"{0}\" ){{ id }}", slug)}} );
		EXPECT_EQ( FailureStatus([&]{ Post(Port(), "/graphql", body); }), http::status::unauthorized );
		let rows = RunQL( Ƒ("identities( slug:\"{}\" ){{ id }}", slug) );
		EXPECT_TRUE( rows.is_array() && rows.get_array().empty() ) << "the identity was created anyway: " << serialize( rows );
	}
}