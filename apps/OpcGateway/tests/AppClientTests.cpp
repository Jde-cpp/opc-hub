#include <jde/fwk/chrono.h>
#include <jde/fwk/process/process.h>
#include <jde/fwk/process/execution.h>
#include <jde/app/client/AppClientSocketSession.h>
#include <jde/app/client/appClient.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/web/Jwt.h>
#include <jde/web/server/Sessions.h>
#include "utils/helpers.h"
#include "../src/GatewayAppClient.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	struct AppClientTests : ::testing::Test{

	};

	TEST_F( AppClientTests, ConnectionRow ){
		auto vars = jobject{
			{ "programName", "Tests.Opc" },
			{ "name", *Settings::FindString("/instanceName") }
		};
		auto q = "connections( programName: $programName, instanceName: $name ) { created }";
		auto await = AppClient()->Query<jarray>( move(q), move(vars) );
		let connections = BlockTAwait<jarray>( move(*await) );
		ASSERT_EQ( connections.size(), 1 );
		let startTime = string{ connections.at(0).as_object().at("created").get_string() };
		TRACE( "Process::StartTime: '{}', startTime: '{}'.", ToIsoString(Process::StartTime()), startTime );
		ASSERT_TRUE( std::chrono::abs(Process::StartTime()-Chrono::ToTimePoint(string{startTime})) < 120s );
	}

	TEST_F( AppClientTests, Status ){
		auto q = "connections{id programName instanceName hostName created status{ memory values } }";
		auto await = AppClient()->Query<jarray>( move(q), {} );
		let connections = BlockTAwait<jarray>( move(*await) );
		TRACE( "Connections: {}", serialize(connections) );
		int count = 1;
		if( !Settings::FindBool("/testing/embeddedAppServer").value_or(true) )
			++count;
		if( !Settings::FindBool("/testing/embeddedOpcServer").value_or(true) )
			++count;
		ASSERT_EQ( connections.size(), count );
	}

	TEST_F( AppClientTests, ConnectBadSessionId ){
		//static: keep the socket open for the rest of the suite. Torn down via shutdown function, NOT at static destruction: the
		//beast stream must be destroyed while the io_context is alive (~stream touches the ioc's service registry - UAF after
		//cleanup destroys the ioc). Shutdown() closes the socket ourselves - an external AppServer (/testing/embeddedAppServer=false)
		//never closes its end, and the pending read would keep ioc->run() from returning and wedge shutdown at the executor join.
		static sp<App::Client::AppClientSocketSession> session;
		if( !session ){
			session = ms<App::Client::AppClientSocketSession>( Executor(), optional<ssl::context>{}, AppClient()->Acl(), AppClient() );
			Process::AddShutdownFunction( [](bool terminate, SL sl){
				session->Shutdown( terminate, sl );
				session = nullptr;
			});
		}
		BlockVoidAwait( session->RunSession(App::Client::ServerSettings::Host(), App::Client::ServerSettings::Port()) );
		using ConnectionInfo = App::Proto::FromServer::ConnectionInfo;
		try{
			BlockAwait<Web::Client::ClientSocketAwait<ConnectionInfo>, ConnectionInfo>( session->Connect(0xBAD5E55) );
			FAIL() << "Connect with an invalid session id should throw.";
		}
		catch( const std::exception& e ){
			TRACE( "Expected exception: {}", e.what() );
		}
	}

	//web-refactor A3: the gateway validates a Bearer token by relay - JwtLoginAwait hands it to the AppServer over the gateway's own
	//app socket, and UpsertAwait mints a local session for the user that answers.  The token is this process's own, built as the
	//app client's login builds it, so the AppServer answers with the user it enrolled the gateway as.
	TEST_F( AppClientTests, BearerIsRelayedToAppServer ){
		using Web::Server::SessionInfo; using Web::Server::Sessions::UpsertAwait;
		ASSERT_FALSE( AppClient()->IsLocal() );
		let& settings = *AppClient()->SslSettings;
		auto certificate = Crypto::ReadCertificate( settings.Certificate.Path );
		const Crypto::Certificate info{ certificate };
		let name = info.Upn.size() ? info.Upn : info.Email.size() ? info.Email : info.CommonName;
		const Web::Jwt jwt{ {}, {0}, name, info.CommonName, 0, {}, TimePoint::min(), "BearerIsRelayedToAppServer", settings.PrivateKey, move(certificate) };
		let session = BlockAwait<UpsertAwait,sp<SessionInfo>>( UpsertAwait{"Bearer "+jwt.Payload(), "127.0.0.1", false, AppClient()} );
		ASSERT_TRUE( session );
		EXPECT_NE( session->UserPK.Value, 0u );
		EXPECT_EQ( session->UserPK.Value, AppClient()->UserPK().Value ) << "the relayed answer names the user behind the token";
		Web::Server::Sessions::Remove( session->SessionId );
	}

	TEST_F( AppClientTests, Login ){
		using Web::FromServer::SessionInfo;
		auto payload = Process::GetEnv( "JDE_GOOGLE_JWT" );
		if( !payload || payload->empty() )
			GTEST_SKIP() << "JDE_GOOGLE_JWT not set.";
		else{
			let value = BlockTAwait<SessionInfo>( move(*AppClient()->Login(Web::Jwt{*payload}, SL{})) );
			ASSERT_TRUE( value.session_id()>0 );
		}
	}
}
