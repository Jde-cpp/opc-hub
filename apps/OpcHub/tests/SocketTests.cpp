#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/web/server/Sessions.h>
#include "../../AppServer/tests/helpers.h"
#include "../../OpcGateway/tests/utils/GatewayClientSocket.h"
#include "../../OpcGateway/tests/utils/helpers.h"
#include "helpers.h"
#define let const auto

//One listener, two protocols by path (src/WebServer.cpp): an app socket on `/` and a gateway socket on `/opc`, bound to one
//session - revoking the session closes both.  (Before the merge the two listeners shared one socket map with per-listener
//ids, so the second one's socket collided and was never reached.)
namespace Jde::Opc::Hub::Tests{
	using App::Server::Tests::RawClientSession;
	using Gateway::Tests::GatewayClientSocket;
	using Gateway::Tests::GatewayPort;

	TEST( SocketTests, SessionRevokeClosesBothListeners ){
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{1}, "127.0.0.1", true )->SessionId;
		auto app = ms<RawClientSession>( Executor() );
		BlockAwait( app->RunSession("127.0.0.1", AppPort()) );
		auto gateway = GatewayClientSocket::Open( "127.0.0.1", GatewayPort(), sessionId );
		{//bind the app socket to the session: kSessionId
			App::Proto::FromClient::Transmission t;
			auto& m = *t.add_messages();
			m.set_request_id( app->NextRequestId() );
			m.set_session_id( sessionId );
			app->Write( move(t) );
		}
		let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( gateway->Query("status{ memory }", {}, true) );
		EXPECT_TRUE( value.as_object().contains("memory") ) << serialize( value );

		EXPECT_TRUE( Web::Server::Sessions::Remove(sessionId) );
		EXPECT_TRUE( app->WaitForClose() ) << "the app-protocol socket was not closed by the revoke.";
		auto queryAgain = [&]{ return BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( gateway->Query("status{ memory }", {}, true) ); };
		EXPECT_ANY_THROW( queryAgain() ) << "the gateway-protocol socket was not closed by the revoke.";
	}

	//A historian subscription is a message of the gateway socket (GatewaySocketSession::HistSubscribe), so the hub's /opc answers
	//one as the gateway's historian does:  a group the hub doesn't host is refused by it, over the one listener (#225).
	TEST( SocketTests, HistSubscribeReachesTheHistorian ){
		let sessionId = Web::Server::Sessions::Add( Jde::UserPK{1}, "127.0.0.1", true )->SessionId;
		auto gateway = GatewayClientSocket::Open( "127.0.0.1", GatewayPort(), sessionId );
		const vector<ExNodeId> nodes{ ExNodeId{jvalue{jobject{{"nsu", "urn:jde:pumps"}, {"i", 6054}}}} };
		try{
			BlockAwait<Web::Client::ClientSocketAwait<vector<StatusCode>>,vector<StatusCode>>( gateway->HistSubscribe(999'999, nodes, nullopt, nullptr) );
			ADD_FAILURE() << "a group the hub doesn't host was subscribed to";
		}
		catch( const Gateway::Tests::GatewayErrorResponse& e ){
			EXPECT_NE( string{e.what()}.find("isn't one this gateway historizes"), string::npos ) << e.what();
		}
		EXPECT_TRUE( Web::Server::Sessions::Remove(sessionId) );
		auto unsubscribe = [&]{ return BlockAwait<Web::Client::ClientSocketAwait<bool>,bool>( gateway->HistUnsubscribe(1) ); };
		EXPECT_ANY_THROW( unsubscribe() ) << "the revoke closed the socket";
	}

	//an upgrade on a path that names no protocol is declined - the connection closes without an ack.
	TEST( SocketTests, UnknownPathDeclined ){
		auto app = ms<RawClientSession>( Executor() );
		bool threw{};
		try{
			BlockAwait( app->RunSession("127.0.0.1", AppPort(), "/nope") );
		}
		catch( const std::exception& ){
			threw = true;
		}
		EXPECT_TRUE( threw || app->WaitForClose() ) << "an upgrade on /nope was accepted.";
	}
}