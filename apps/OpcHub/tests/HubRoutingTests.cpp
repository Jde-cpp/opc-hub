#include <jde/fwk/io/file.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/web/client/http/ClientHttpAwait.h>
#include <jde/web/client/http/ClientHttpResException.h>
#include <jde/web/server/Sessions.h>
#include "../../AppServer/src/appStartup.h"
#include "../../AppServer/src/LocalClient.h"
#include "../../OpcGateway/src/GatewayAppClient.h"
#include "../../OpcGateway/src/auth/OpcServerSession.h"
#include "../../OpcServer/src/access/UAAccess.h"
#define let const auto

//One listener for both roles (src/HttpRequestAwait.cpp, src/ql/HubQL.cpp): the AppServer's and the gateway's REST routes, one
///graphql over access+app+gateway, one session table and one live logger.  AppPort()==GatewayPort() - the split names stay so
//the two apps' helpers keep working.  Plain http - the server detects ssl per connection.
namespace Jde::Opc::Hub::Tests{
	using Web::Client::ClientHttpAwait; using Web::Client::ClientHttpRes; using Web::Client::ClientHttpResException;
	namespace http = boost::beast::http;
	constexpr sv Host{ "127.0.0.1" };//sessions are endpoint-bound; connect by ip so the socket's remote address matches the endpoint sessions are minted with.
	Ξ AppPort()ι->PortType{ return Settings::FindNumber<PortType>("/http/app/port").value_or(1973); }
	using Gateway::Tests::GatewayPort;

	struct HubRoutingTests : ::testing::Test{
		Ω Get( PortType port, string target, string authorization={} )->ClientHttpRes{
			return BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{string{Host}, move(target), port, {.Authorization=move(authorization), .IsSsl=false}} );
		}
		Ω Post( PortType port, string target, string body, string authorization={} )->ClientHttpRes{
			return BlockAwait<ClientHttpAwait,ClientHttpRes>( ClientHttpAwait{string{Host}, move(target), move(body), port, {.Authorization=move(authorization), .ContentType="application/json", .IsSsl=false}} );
		}
		Ω QL( PortType port, string query, string authorization={} )->jvalue{//`data`: an object for a query, an array for a mutation.
			let res = Post( port, "/graphql", serialize(jobject{{"query", move(query)}}), move(authorization) );
			let json = res.Json();
			return json.at( "data" );
		}
	};

	TEST_F( HubRoutingTests, BothRestSurfaces ){
		let clientId = Get( AppPort(), "/GoogleAuthClientId" );
		EXPECT_EQ( Json::AsString(clientId.Json(), "value"), "opc-hub-tests-google-client-id" );
		let errorCodes = Get( GatewayPort(), "/ErrorCodes?scs=2150891520" );//BadCertificateUntrusted
		EXPECT_EQ( Json::AsArray(errorCodes.Json(), "errorCodes").size(), 1u );
	}

	//what the SPA discovers: the gateway role of this process, at the one port - a local registration, no socket.
	TEST_F( HubRoutingTests, OpcGatewaysListsSelf ){
		let res = Get( AppPort(), "/opcGateways" );
		let json = res.Json();//Json() builds a value - hold it, the array below is a reference into it.
		let& servers = Json::AsArray( json, "servers" );
		ASSERT_GE( servers.size(), 1u ) << serialize( json );
		let& self = servers[0].as_object();
		EXPECT_EQ( Json::AsNumber<PortType>(self, "port"), GatewayPort() );
		EXPECT_EQ( Json::AsString(self, "host"), Settings::FindString("/http/host").value_or(Process::HostName()) );
		EXPECT_EQ( Json::AsString(self, "instanceName"), *Settings::FindString("/instanceName") );
	}

	//one process, one registration: a Tests.OpcHub row and no separate gateway row.
	TEST_F( HubRoutingTests, OneConnectionRow ){
		let data = QL( AppPort(), "connections{ id programName instanceName instanceId }" ).as_object();
		uint hub{}, gateway{};
		for( let& c : Json::AsArray(data, "connections") ){
			let program = Json::AsString( c.as_object(), "programName" );
			hub += program==Process::AppName();
			gateway += program=="Jde.OpcGateway";
		}
		EXPECT_EQ( hub, 1u ) << serialize( data );
		EXPECT_EQ( gateway, 0u ) << serialize( data );
	}

	//one /graphql over the three schemas: the gateway's, the app's and access' tables, the merged custom queries and the
	//merged status - and access tables stay off the gateway's ConnectAwait (GatewayQLAwait::IsApplicable).
	TEST_F( HubRoutingTests, MergedGraphQL ){
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{1}, string{Host}, false )->SessionId;
		let authorization = Ƒ( "{:x}", sessionId );
		EXPECT_TRUE( QL(AppPort(), "serverConnections{ id slug url }").as_object().contains("serverConnections") );
		EXPECT_TRUE( QL(AppPort(), "connections{ id programName }").as_object().contains("connections") );
		EXPECT_TRUE( QL(AppPort(), "users{ id name }", authorization).as_object().contains("users") );
		let status = Json::AsObject( QL(AppPort(), "status{ memory clients monitoredItems }", authorization).as_object(), "status" );
		EXPECT_TRUE( status.contains("memory") && status.contains("clients") && status.contains("monitoredItems") ) << serialize( status );
		let type = QL( AppPort(), "__type(name:\"ServerConnection\"){ name fields{ name } }" ).as_object();
		EXPECT_TRUE( type.contains("__type") ) << serialize( type );
		Web::Server::Sessions::Remove( sessionId );
	}

	//POST /login by shape: a Bearer JWT is the app's, a JSON body with `opc` the gateway's OPC login, neither is the app's 401.
	TEST_F( HubRoutingTests, LoginByShape ){
		auto failure = [&]( function<void()> call )->optional<http::status>{
			try{ call(); }
			catch( ClientHttpResException& e ){ return e.Status(); }
			catch( const std::exception& ){}
			return {};
		};
		EXPECT_EQ( failure([&]{ Post(AppPort(), "/login", "{}"); }), http::status::unauthorized );
		EXPECT_NE( failure([&]{ Post(AppPort(), "/login", "{}", "Bearer not-a-jwt"); }), optional<http::status>{} );
		let opc = failure( [&]{ Post(AppPort(), "/login", serialize(jobject{{"opc","noSuchServer"},{"user","u"},{"password","p"}})); } );
		EXPECT_TRUE( opc.has_value() && *opc!=http::status::not_found ) << "the opc login was not routed to the gateway.";
	}

	//POST /logout ends the web session (and so both protocols' sockets) - the gateway's and the app's logout in one.
	TEST_F( HubRoutingTests, LogoutRemovesSession ){
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{1}, string{Host}, false )->SessionId;
		let authorization = Ƒ( "{:x}", sessionId );
		let res = Post( AppPort(), "/logout", "{}", authorization );
		EXPECT_TRUE( Json::AsBool(res.Json(), "removed") );
		EXPECT_FALSE( Web::Server::Sessions::Find(sessionId) );
	}

	//a session minted through the AppServer role is honoured by the gateway role without a lookup - one table, IsLocal.
	TEST_F( HubRoutingTests, SessionShared ){
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{1}, string{Host}, false )->SessionId;
		let authorization = Ƒ( "{:x}", sessionId );
		let data = QL( GatewayPort(), "status{ memory }", authorization ).as_object();
		EXPECT_TRUE( Json::AsObject(data, "status").contains("memory") ) << serialize( data );
		EXPECT_TRUE( Web::Server::Sessions::Remove(sessionId) );
		EXPECT_THROW( QL(GatewayPort(), "status{ memory }", authorization), ClientHttpResException );//revoked on one surface, gone on the other.
	}

	//a level set through the AppServer role's mutation is read straight back from the live logger through the gateway role
	//(logSetting{} wants an authenticated caller - a session minted in-process, as the SPA's would be after login).
	TEST_F( HubRoutingTests, LogLevelRoundTrip ){
		let instance = Gateway::AppClient()->InstancePK();
		ASSERT_NE( instance, 0u );
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{1}, string{Host}, false )->SessionId;
		let authorization = Ƒ( "{:x}", sessionId );
		auto level = [&]()->string{
			let data = QL( GatewayPort(), "logSetting{ text }", authorization ).as_object();
			let& text = Json::AsObject( Json::AsObject(data, "logSetting"), "text" );
			return text.contains("test") ? string{ text.at("test").as_string() } : string{};
		};
		QL( AppPort(), Ƒ("mutation updateInstanceTagLevel( \"id\":{}, \"text\":[{{tags:[\"test\"],level:\"Critical\"}}] )", instance), authorization );
		EXPECT_EQ( level(), "Critical" );
		QL( AppPort(), Ƒ("mutation updateInstanceTagLevel( \"id\":{}, \"text\":[{{tags:[\"test\"],level:null}}] )", instance), authorization );
		EXPECT_NE( level(), "Critical" );
		Web::Server::Sessions::Remove( sessionId );
	}

	//install-issues #3/#4: the hub serves the site (/http/site) from the listener the api shares - the page (index.html, always
	//revalidated), the build's hashed assets (cached for good), a nested asset - and an extension-less path that is no route of
	//the api is the page, the deep-link fallback IIS needed the rewrite module for.  A missing asset is 404, a dot segment never
	//resolves, and the api's own GETs come first.
	TEST_F( HubRoutingTests, SiteServedFromTheHub ){
		let index = Get( AppPort(), "/" );
		EXPECT_EQ( index.Status(), http::status::ok );
		EXPECT_EQ( string{index.Headers()[http::field::content_type]}, "text/html; charset=utf-8" );
		EXPECT_EQ( string{index.Headers()[http::field::cache_control]}, "no-cache" );
		EXPECT_NE( index.Body().find("<app-root>"), string::npos );
		let js = Get( AppPort(), "/main-ABCDEFGH.js" );
		EXPECT_EQ( string{js.Headers()[http::field::content_type]}, "text/javascript; charset=utf-8" );
		EXPECT_EQ( string{js.Headers()[http::field::cache_control]}, "public, max-age=31536000, immutable" );
		EXPECT_EQ( Get(AppPort(), "/assets/site/hello.txt").Body(), "hello\n" );
		for( let route : {"/login", "/apps/gateways", "/access/users"} )
			EXPECT_EQ( Get(AppPort(), route).Body(), index.Body() ) << route;
		auto status = [&]( string target )->optional<http::status>{
			try{ return Get( AppPort(), move(target) ).Status(); }
			catch( ClientHttpResException& e ){ return e.Status(); }
			catch( const std::exception& ){ return {}; }
		};
		EXPECT_EQ( status("/missing.js"), optional{http::status::not_found} );
		EXPECT_EQ( status("/assets/../Opc.Hub.Tests.jsonnet"), optional{http::status::not_found} );
		EXPECT_EQ( status("/.git/config"), optional{http::status::not_found} );
		EXPECT_TRUE( Get(AppPort(), "/opcGateways").Json().contains("servers") );//the api's own GET, ahead of the page
	}

	//install-issues #2's certificate:  the hub's web certificate names this machine - DNS:$(HostName) in the config's subjectAltName,
	//the settings expander's built-in - beside localhost, so https://<machine>:1967 passes the name check once the certificate is
	//trusted there; a config change re-issues on the same key (Crypto::ReissueReason), so an existing install picks it up.
	TEST_F( HubRoutingTests, CertificateNamesTheHost ){
		let ssl = Crypto::CryptoSettings{ Json::FindDefaultObject(Settings::AsObject("/http"), "ssl"), {} };
		let san = Crypto::Certificate{ Crypto::ReadCertificate(ssl.Certificate.Path), SRCE_CUR }.SubjectAltName;
		EXPECT_NE( san.find("DNS:"+Process::HostName()), string::npos ) << san;
		EXPECT_NE( san.find("DNS:localhost"), string::npos ) << san;
	}

	//install-issues #1 (b):  the login page's username with no DOMAIN\ posts an empty `opc`, which ServerCnnctnAwait reads as
	//the default connection.  What follows the connect has to key on the slug it resolved to (AuthAwait::Execute) - the provider
	//lookup, and the credential cache GetCredential reads by slug - and the OpcServer's side of the login (UAAccess::ResolveUser)
	//has to name the user the hub's AddSession did.  A wrong password is the server's BadUserAccessDenied, surfaced as 401.
	TEST_F( HubRoutingTests, LoginDefaultConnection ){
		let rootSession = Web::Server::Sessions::Add( Jde::UserPK{1}, string{Host}, false )->SessionId;
		let root = Ƒ( "{:x}", rootSession );
		let opc = Gateway::Tests::GetConnection( Gateway::Tests::OpcServerSlug );//the embedded OpcServer's row, with its provider row
		auto setDefault = [&]( bool isDefault ){ QL( AppPort(), Ƒ("mutation updateServerConnection( id:{}, isDefault:{} )", opc.Id, isDefault), root ); };
		setDefault( true );
		auto body = []( sv password ){ return serialize( jobject{{"opc",""},{"user","user1"},{"password",password}} ); };
		let res = Post( AppPort(), "/login", body("0123456789ABCD") );
		let authorization = string{ res.Headers()[http::field::authorization] };
		ASSERT_FALSE( authorization.empty() ) << "the login minted no session";
		let sessionId = Str::TryTo<SessionPK>( authorization, 0, 16 ).value_or( 0 );
		let cred = Gateway::GetCredential( sessionId, Gateway::Tests::OpcServerSlug );//keyed by the resolved slug, not ""
		ASSERT_TRUE( cred ) << "the credential is not cached under the default connection's slug";
		EXPECT_EQ( cred->LoginName(), "user1" );
		EXPECT_TRUE( cred->UserPK() ) << "AddSession resolved no user";
		EXPECT_EQ( Opc::Server::UAAccess::ResolveUser("user1").Value, cred->UserPK().Value ) << "the OpcServer resolves the login to another user than the hub";
		optional<http::status> bad;
		try{ Post( AppPort(), "/login", body("nope") ); }
		catch( ClientHttpResException& e ){ bad = e.Status(); }
		EXPECT_EQ( bad, optional{http::status::unauthorized} );
		Post( AppPort(), "/logout", "{}", authorization );
		setDefault( false );
		Web::Server::Sessions::Remove( rootSession );
	}

	//The "OPC UA Server" component's seeds (setup/OpcHubSetup.nsi SEC_OPCSERVER, setup/linux/build-deb.sh), applied the way the
	//hub applies them - LocalQL::Upsert, twice:  the Google provider type and row, provider 7 for the bundled server, the server
	//as the default connection.  Here the connection insert also meets the gateway's hook (registered by now, unlike on an
	//install's first sync), which has to reuse the seeded provider row rather than insert a second one (ProviderMAwait::Check).
	TEST_F( HubRoutingTests, OpcServerComponentSeeds ){
		let rootSession = Web::Server::Sessions::Add( Jde::UserPK{1}, string{Host}, false )->SessionId;
		let root = Ƒ( "{:x}", rootSession );
		let& scriptPaths = Settings::FindDefaultArray( "/dbServers/scriptPaths" );//<repo>/apps/AppServer/config/sql/<dialect>, libs/access/config/sql/<dialect>, apps/OpcGateway/config/sql/<dialect>
		ASSERT_EQ( scriptPaths.size(), 3u );
		auto configDir = [&]( uint i ){ return fs::path{ string{Json::AsSV(scriptPaths[i])} }.parent_path().parent_path(); };
		let ql = App::Server::QLPtr();
		for( uint pass=0; pass<2; ++pass ){
			for( let& file : {configDir(1)/"release-google.mutation", configDir(1)/"release-opcServer.mutation", configDir(2)/"release-opcServer.mutation"} )
				ASSERT_NO_THROW( ql->Upsert(IO::Load(file), {}, {UserPK::System}) ) << file.string() << " pass " << pass;
		}
		auto one = [&]( string query, sv name )->jobject{ let d = QL( AppPort(), move(query), root ); let& v = d.as_object().at( name ); return v.is_object() ? v.get_object() : jobject{}; };
		EXPECT_EQ( Json::FindDefaultSV(one("provider(id:1){ id providerTypeId }", "provider"), "providerTypeId"), "Google" );//the ql spells the enum by name
		EXPECT_EQ( Json::FindDefaultSV(one("provider(name:\"OpcServer\"){ id providerTypeId }", "provider"), "providerTypeId"), "OpcServer" );//id 7 on an install; here the test connection's provider took 7 first, so the seeded insert was skipped and the connection's hook created the row
		let connection = one( "serverConnection(slug:\"OpcServer\"){ id url isDefault }", "serverConnection" );
		EXPECT_EQ( Json::FindDefaultSV(connection, "url"), "opc.tcp://127.0.0.1:4840" );
		EXPECT_TRUE( Json::FindBool(connection, "isDefault").value_or(false) );
		Gateway::Tests::PurgeServerCnnctn( Json::FindNumber<Gateway::ServerCnnctnPK>(connection, "id").value_or(0) );//the hook purges its provider with it
		EXPECT_TRUE( one("provider(name:\"OpcServer\"){ id }", "provider").empty() );
		Web::Server::Sessions::Remove( rootSession );
	}
}
