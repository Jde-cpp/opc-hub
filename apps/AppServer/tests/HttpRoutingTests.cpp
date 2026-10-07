#include <jde/fwk/log/MemoryLog.h>
#include <jde/ql/QLAwait.h>
#include <jde/web/Jwt.h>
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/http/ClientHttpResException.h>
#include "helpers.h"
#include "../src/LocalClient.h"
#include "../src/WebServer.h"
#define let const auto

//the REST surface: HttpRequestAwait routing (/GoogleAuthClientId, /opcGateways, /opcServers, /login, /logout, 404s)
//plus the /graphql dispatch in Web::Server.  All plain http - the server detects ssl per connection.
namespace Jde::App::Server::Tests{
	using Web::Client::ClientHttpAwait; using Web::Client::ClientHttpRes; using Web::Client::ClientHttpResException;
	namespace http = boost::beast::http;

	struct HttpRoutingTests : ::testing::Test{
		Ω Get( string target, string authorization={} )->ClientHttpRes{
			return BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{string{Host}, move(target), Port(), {.Authorization=move(authorization), .IsSsl=false}} );
		}
		Ω Post( string target, string body, string authorization={}, string contentType="application/json", string origin={} )->ClientHttpRes{
			return BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{string{Host}, move(target), move(body), Port(), {.Authorization=move(authorization), .Origin=move(origin), .ContentType=move(contentType), .IsSsl=false}} );
		}
		Ω EncodeJwt( jobject body )ι->string{
			let head = jobject{ {"alg","RS256"}, {"typ","JWT"} };
			return Str::Encode64( serialize(head), true )+"."+Str::Encode64( serialize(body), true )+"."+Str::Encode64( string{"notVerifiedHere"}, true );
		}
		//the thrown status for calls that must fail; unset if no ClientHttpResException was thrown.
		Ŧ FailureStatus( T&& call )->optional<http::status>{
			try{
				call();
			}
			catch( ClientHttpResException& e ){
				return e.Status();
			}
			catch( const std::exception& ){
			}
			return {};
		}
	};

	TEST_F( HttpRoutingTests, GoogleAuthClientId ){
		let res = Get( "/GoogleAuthClientId" );
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
		let anonymous = findGateway( Get("/opcGateways") );
		ASSERT_FALSE( anonymous.empty() ) << "discovery must still work with no credential";
		EXPECT_EQ( Json::AsString(anonymous, "host"), "gw-host" );
		EXPECT_EQ( anonymous.at("port").to_number<uint32>(), 4321u );
		EXPECT_FALSE( anonymous.contains("pid") ) << "the pid stopApplicationInstance signals, handed to anyone who asks";
		EXPECT_FALSE( anonymous.contains("startTime") );
		EXPECT_FALSE( anonymous.contains("application") );

		let identified = findGateway( Get("/opcGateways", Ƒ("{:x}", MintSession())) );
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
		let res = Get( "/opcServers" );
		let servers = Json::AsArray( res.Json(), "servers" );
		EXPECT_TRUE( std::ranges::any_of(servers, [](let& s){ return Json::AsString(s.as_object(), "instanceName")=="http-opc"; }) );
		BlockVoidAwait( client->Close(true, SRCE_CUR) );
	}

	TEST_F( HttpRoutingTests, LoginRequiresBearer ){
		EXPECT_EQ( FailureStatus([&]{ Post("/login", "{}"); }), http::status::unauthorized );//no Authorization at all.
		EXPECT_EQ( FailureStatus([&]{ Post("/login", "{}", "Basic dXNlcjpwd2Q="); }), http::status::unauthorized );//wrong scheme.
	}

	TEST_F( HttpRoutingTests, LoginRejectsGarbageJwt ){
		let status = FailureStatus( [&]{ Post("/login", "{}", "Bearer not-a-jwt"); } );
		ASSERT_TRUE( status );
		EXPECT_NE( *status, http::status::ok );
	}

	//security-review #1:  a malformed n claim, unauthenticated, terminated the server - this whole suite - before any signature check.
	TEST_F( HttpRoutingTests, LoginRejectsMalformedKeyClaim ){
		let jwt = EncodeJwt( jobject{{"iat", time(nullptr)}, {"n", "!!!"}, {"e", "AQAB"}} );
		let status = FailureStatus( [&]{ Post("/login", "{}", "Bearer "+jwt); } );
		ASSERT_TRUE( status );
		EXPECT_NE( *status, http::status::ok );
		EXPECT_EQ( Json::AsString(Get("/GoogleAuthClientId").Json(), "value"), "app-server-tests-google-client-id" ) << "the server still answers";
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
			Post( "/login", "{}", "Bearer "+jwt, "application/json", origin );
			ADD_FAILURE() << "an expired jwt must not log in";
		}
		catch( ClientHttpResException& e ){
			EXPECT_EQ( e.Status(), http::status::unauthorized );
			EXPECT_EQ( e.Res()[http::field::access_control_allow_origin], origin ) << "the browser cannot read a response without it, so the spa never sees the 401";
		}
	}

	TEST_F( HttpRoutingTests, LogoutRemovesSession ){
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{1}, string{Host}, false )->SessionId;
		let res = Post( "/logout", "{}", Ƒ("{:x}", sessionId) );
		EXPECT_TRUE( Json::AsBool(res.Json(), "removed") );
		EXPECT_FALSE( Web::Server::Sessions::Find(sessionId) );
		EXPECT_EQ( FailureStatus([&]{ Post("/logout", "{}", Ƒ("{:x}", sessionId)); }), http::status::unauthorized );//the removed id is no longer a credential.
	}

	TEST_F( HttpRoutingTests, UnknownTarget ){
		EXPECT_EQ( FailureStatus([&]{ Get("/definitely-not-a-route"); }), http::status::not_found );
	}

	TEST_F( HttpRoutingTests, GraphQLPost ){
		let res = Post( "/graphql", serialize(jobject{{"query", "setting(target:\"googleAuthClientId\"){target value}"}}) );
		let data = Json::AsObject( res.Json(), "data" );
		let setting = Json::AsObject( data, "setting" );
		EXPECT_EQ( Json::AsString(setting, "target"), "googleAuthClientId" );
		EXPECT_EQ( Json::AsString(setting, "value"), "app-server-tests-google-client-id" );
	}

	TEST_F( HttpRoutingTests, GraphQLGet ){
		let res = Get( "/graphql?query=setting(target:%22googleAuthClientId%22)%7Btarget%20value%7D" );
		let data = Json::AsObject( res.Json(), "data" );
		EXPECT_EQ( Json::AsString(Json::AsObject(data, "setting"), "value"), "app-server-tests-google-client-id" );
	}

	//web-refactor A4: a GET must not have side effects.  The rule sat on a branch /graphql never reached, so it was not enforced.
	TEST_F( HttpRoutingTests, GraphQLGetRefusesMutation ){
		try{
			Get( "/graphql?query=mutation%20createUser(slug:%22getMutation%22,name:%22getMutation%22)%7Bid%7D" );
			ADD_FAILURE() << "a mutation over GET must be refused";
		}
		catch( ClientHttpResException& e ){
			EXPECT_EQ( e.Status(), http::status::bad_request );
			EXPECT_TRUE( e.Res().Body().contains("Mutations must use post.") ) << e.Res().Body();
		}
	}

	TEST_F( HttpRoutingTests, GraphQLRequiresQuery ){
		EXPECT_EQ( FailureStatus([&]{ Post("/graphql", "{}"); }), http::status::bad_request );
	}
}
