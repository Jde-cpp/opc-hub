#include <jde/fwk/io/json.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include "../utils/helpers.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"

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

	TEST_F( NoSecurityTests, AnonymousIsNotOfferedAtAll ){
		let what = Attempt( Credential{} );
		EXPECT_TRUE( what.contains("BadIdentityTokenRejected") ) << what;
		EXPECT_TRUE( what.contains("does not offer Anonymous") ) << what;
	}
}
