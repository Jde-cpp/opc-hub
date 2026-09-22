#include <jde/fwk/io/json.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include "../utils/helpers.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"
#include "../../src/ql/GatewayQL.h"
#include "../../src/async/Subscriptions.h"
#include <jde/fwk/utils/Stopwatch.h>

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	//The no-security path - a connection row with no certificateUri.  The gateway stays on SecurityPolicy None:  no channel
	//certificate, the data in the clear.  The credential is another matter:  UAClient::Configuration still issues the
	//connection's certificate and builds the Basic256Sha256 auth policy from it, so a token the server will only take encrypted -
	//every one the embedded OpcServer offers, /opc/userTokenPolicyUri's default being stamped on its None endpoint too - is
	//encrypted to the server's certificate and rides the unsecured channel (reviews/security-matrix.md #1, ruled 09-18;  until
	//then the None path had no auth policy and this connect failed "No suitable endpoint found" as BadIdentityTokenRejected).
	//What still cannot work is a token type the server never offers, and the connection error has to say that rather than read
	//as a bad credential.  The third-party cells - and the url with no unsecured endpoint at all - run in ExternalServerTests.
	const string NoSecuritySlug{ "opcTestsNoSecurity" };
	class NoSecurityTests : public ::testing::Test{
	protected:
		Ω SetUpTestSuite()->void{
			let url = Settings::FindSV( "/opc/url" ).value_or( "opc.tcp://127.0.0.1:4840" );
			_connection = GetConnection( NoSecuritySlug, string{url}, "" );
			_jwt = BlockAwait<Web::Client::ClientSocketAwait<Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		}
		Ω TearDownTestSuite()->void{
			if( _connection ){
				PurgeServerCnnctn( _connection->Id );
				_connection = nullopt;
			}
		}
		α TearDown()ι->void override{
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		α Connect( Credential cred )ι->ConnectAwait::Task;
		α Attempt( Credential cred )ε->string;//what the connect failed with, "" when it connected.
		static optional<ServerCnnctn> _connection;
		static optional<Web::Jwt> _jwt;
		up<Exception> _exception;
		sp<UAClient> _client;
		atomic_flag _done;
	};
	optional<ServerCnnctn> NoSecurityTests::_connection;
	optional<Web::Jwt> NoSecurityTests::_jwt;

	α NoSecurityTests::Connect( Credential cred )ι->ConnectAwait::Task{
		try{
			_client = co_await UAClient::GetClient( NoSecuritySlug, move(cred) );
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		_done.test_and_set();
		_done.notify_all();
	}
	α NoSecurityTests::Attempt( Credential cred )ε->string{
		let type = TokenTypeName( cred.Type() );
		_exception = nullptr; _done.clear();
		Connect( move(cred) );
		_done.wait( false );
		let what = _exception ? string{_exception->what()} : string{};
		INFO( "MATRIX|None|{}|{}|{}", type, _connection->Url, what.empty() ? Negotiated(_client) : what );
		return what;
	}

	TEST_F( NoSecurityTests, AnIssuedTokenRidesANoneChannelEncrypted ){
		let what = Attempt( Credential{_jwt->Payload()} );//the issued token TokenTests presents over Basic256Sha256.
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (None/None)", Negotiated(_client) );//the channel stayed unsecured;  only the token was encrypted - the server's token policy is Basic256Sha256, and it activates nothing else.
		EXPECT_FALSE( UAClient::ConnectErrors().contains(NoSecuritySlug) );//serverConnections{connectionStatus} has nothing to report.
	}

	//reviews/m3-closing.md #4:  a UAClient copies its connection row once, and ConnectAwait hands back the cached client, so an
	//edit never reached it - the Gateways help's own step (copy the Application URI into Certificate URI for Sign & Encrypt) left
	//the traffic on None/None under a row that said otherwise, for as long as anything kept the client busy.  An edit that
	//changes how the client connects or translates paths now takes every client on the row off the registry, parking what they
	//monitor for the reconnect, which builds its client from the row as it now is.
	TEST_F( NoSecurityTests, AnEditedConnectionReconnectsOnItsNewSettings ){
		let urn = Settings::FindSV( "/opc/urn" ).value_or( "urn:open62541.server.application" );
		const Credential cred{ _jwt->Payload() };
		ASSERT_TRUE( Attempt(cred).empty() );
		ASSERT_EQ( "ok (None/None)", Negotiated(_client) );
		let before = _client;
		auto update = [&]( sv certificateUri ){
			QL().QuerySync<jvalue>( Ƒ(R"(mutation updateServerConnection( id:{}, certificateUri:"{}" ))", _connection->Id, certificateUri), {}, {UserPK::System} );
		};
		update( urn );
		EXPECT_FALSE( UAClient::Find(NoSecuritySlug, cred) ) << "the pre-edit client is still registered";
		_client = nullptr;
		let what = Attempt( cred );
		let negotiated = what.empty() ? Negotiated( _client ) : string{};//before the restore below, which rebuilds this client in turn
		update( "" );//back as the suite made it, for the cells after this one.
		ASSERT_TRUE( what.empty() ) << what;
		EXPECT_NE( before, _client );
		EXPECT_TRUE( negotiated.ends_with("/SignAndEncrypt)") ) << negotiated;
	}

	//The same edit, seen from a subscription:  what the pre-edit client monitored - another session's watch page, say - is parked and
	//comes back by itself on a client built from the edited row.  Before, a subscription pinned the pre-edit client indefinitely.
	struct PushCount final : IDataChange{
		α SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value& )ι->void override{ ++Pushes; }
		α to_string()Ι->string override{ return "NoSecurityTests.PushCount"; }
		atomic<uint> Pushes;
	};
	TEST_F( NoSecurityTests, AnEditedConnectionsSubscriptionsComeBackOnItsNewSettings ){
		let urn = Settings::FindSV( "/opc/urn" ).value_or( "urn:open62541.server.application" );
		const NodeId nodeId{ 4, 6017 };
		const Credential cred{ _jwt->Payload() };
		ASSERT_TRUE( Attempt(cred).empty() );
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{_client}) );
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{nodeId}, listener, _client}) );
		Stopwatch sw;
		while( !listener->Pushes )
			ASSERT_NO_THROW( sw.CheckTimeout(6s, 1ms) ) << "no initial push on the None client";
		let lost = _client->Handle();
		let pushes = listener->Pushes.load();
		auto update = [&]( sv certificateUri ){
			QL().QuerySync<jvalue>( Ƒ(R"(mutation updateServerConnection( id:{}, certificateUri:"{}" ))", _connection->Id, certificateUri), {}, {UserPK::System} );
		};
		update( urn );

		sp<UAClient> revived;//the reconnect waits a second, then connects on the edited row, subscribes and re-creates the item.
		auto start = steady_clock::now();//not CheckTimeout:  a throw here would skip the unsubscribe and the restore below.
		while( !revived ){
			for( let& client : UAClient::LiveClients() ){
				let nodes = client->Handle()!=lost && client->Slug()==NoSecuritySlug ? client->TryMonitoredNodes() : nullptr;
				if( nodes && nodes->Count() )
					revived = client;
			}
			if( steady_clock::now()-start>30s )
				break;
			std::this_thread::sleep_for( 10ms );
		}
		let negotiated = revived ? Negotiated( revived ) : string{};
		start = steady_clock::now();
		while( revived && listener->Pushes==pushes && steady_clock::now()-start<6s )
			std::this_thread::sleep_for( 1ms );
		let pushed = listener->Pushes.load()>pushes;
		UAClient::Unsubscribe( listener );//before the restore, or its rebuild would park the listener again.
		update( "" );
		_client = revived;
		ASSERT_TRUE( revived ) << "no replacement client came up with the monitored node restored";
		EXPECT_TRUE( negotiated.ends_with("/SignAndEncrypt)") ) << negotiated;
		EXPECT_TRUE( pushed ) << "the listener heard nothing from the replacement client";
	}

	TEST_F( NoSecurityTests, AnEditThatChangesNothingTheClientUsesKeepsIt ){//the description, say:  no reason to drop anyone's session.
		const Credential cred{ _jwt->Payload() };
		ASSERT_TRUE( Attempt(cred).empty() );
		QL().QuerySync<jvalue>( Ƒ(R"(mutation updateServerConnection( id:{}, description:"edited" ))", _connection->Id), {}, {UserPK::System} );
		EXPECT_EQ( UAClient::Find(NoSecuritySlug, cred), _client );
	}

	TEST_F( NoSecurityTests, ADeletedConnectionClosesItsClients ){//and nothing reconnects it:  the row is gone.
		const Credential cred{ _jwt->Payload() };
		ASSERT_TRUE( Attempt(cred).empty() );
		QL().QuerySync<jvalue>( Ƒ("mutation deleteServerConnection( id:{} )", _connection->Id), {}, {UserPK::System} );
		EXPECT_FALSE( UAClient::Find(NoSecuritySlug, cred) );
		_client = nullptr;
		let what = Attempt( cred );
		QL().QuerySync<jvalue>( Ƒ("mutation restoreServerConnection( id:{} )", _connection->Id), {}, {UserPK::System} );
		EXPECT_TRUE( what.contains("Could not find connection") ) << what;
	}

	TEST_F( NoSecurityTests, AnonymousIsNotOfferedAtAll ){
		let what = Attempt( Credential{} );
		EXPECT_TRUE( what.contains("BadIdentityTokenRejected") ) << what;
		EXPECT_TRUE( what.contains("does not offer Anonymous") ) << what;
	}
}
