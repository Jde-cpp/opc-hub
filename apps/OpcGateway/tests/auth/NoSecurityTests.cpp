#include <jde/fwk/io/json.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include "../utils/ClientFixture.h"
#include "../utils/Subscribing.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"
#include "../../src/ql/GatewayQL.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	//The no-security path - a connection row with no certificateUri.  The gateway stays on SecurityPolicy None:  no channel
	//certificate, the data in the clear.  The credential is another matter:  UAClient::Configuration still issues the
	//connection's certificate and builds the Basic256Sha256 auth policy from it, so a token the server will only take encrypted -
	//every one the embedded OpcServer offers, /opc/userTokenPolicyUri's default being stamped on its None endpoint too - is
	//encrypted to the server's certificate and rides the unsecured channel (reviews/security-matrix.md #1, ruled 09-18;  until
	//then the None path had no auth policy and this connect failed "No suitable endpoint found" as BadIdentityTokenRejected).
	//What still cannot work is a token type the server never offers, and the connection error has to say that rather than read
	//as a bad credential.  The third-party cells - and the url with no unsecured endpoint at all - run in ExternalServerTests.
	const string NoSecuritySlug{ "opcTestsNoSecurity" };
	class NoSecurityTests : public ClientFixture{
	protected:
		Ω SetUpTestSuite()->void{
			_connection = GetConnection( NoSecuritySlug, OpcUrl(), "" );
		}
		Ω TearDownTestSuite()->void{
			PurgeServerCnnctn( NoSecuritySlug );
			_connection = nullopt;
		}
		α Attempt( Credential cred )ι->string{//what the connect failed with, "" when it connected.
			let type = TokenTypeName( cred.Type() );
			return ClientFixture::Attempt( NoSecuritySlug, move(cred), Ƒ("None|{}|{}", type, _connection->Url) );
		}
		static optional<ServerCnnctn> _connection;
	};
	optional<ServerCnnctn> NoSecurityTests::_connection;

	TEST_F( NoSecurityTests, AnIssuedTokenRidesANoneChannelEncrypted ){
		let what = Attempt( SessionCredential() );//the issued token TokenTests presents over Basic256Sha256.
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
		let urn = OpcUrn();
		let cred = SessionCredential();
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
	TEST_F( NoSecurityTests, AnEditedConnectionsSubscriptionsComeBackOnItsNewSettings ){
		let urn = OpcUrn();
		let cred = SessionCredential();
		ASSERT_TRUE( Attempt(cred).empty() );
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) ) << "no initial push on the None client";
		let lost = _client->Handle();
		let pushes = listener->Pushes.load();
		auto update = [&]( sv certificateUri ){
			QL().QuerySync<jvalue>( Ƒ(R"(mutation updateServerConnection( id:{}, certificateUri:"{}" ))", _connection->Id, certificateUri), {}, {UserPK::System} );
		};
		update( urn );

		let revived = WaitRevived( lost, NoSecuritySlug );//the reconnect waits a second, then connects on the edited row, subscribes and re-creates the item;  null, not a throw, which would skip the unsubscribe and the restore below.
		let negotiated = revived ? Negotiated( revived ) : string{};
		let start = steady_clock::now();
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
		let cred = SessionCredential();
		ASSERT_TRUE( Attempt(cred).empty() );
		QL().QuerySync<jvalue>( Ƒ(R"(mutation updateServerConnection( id:{}, description:"edited" ))", _connection->Id), {}, {UserPK::System} );
		EXPECT_EQ( UAClient::Find(NoSecuritySlug, cred), _client );
	}

	TEST_F( NoSecurityTests, ADeletedConnectionClosesItsClients ){//and nothing reconnects it:  the row is gone.
		let cred = SessionCredential();
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
