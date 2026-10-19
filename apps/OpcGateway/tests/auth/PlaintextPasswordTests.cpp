#include <open62541/plugin/accesscontrol_default.h>
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/settings.h>
#include "../utils/ClientFixture.h"
#include "../utils/TestUaServer.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	//security-matrix #6:  a credential in the clear.  A server that offers the Username token only under a token policy of None,
	//on a None endpoint, asks for the password unencrypted on the wire.  open62541's client refuses that by default
	//(matchUserTokenPolicy, "must not be transmitted without encryption"), which reaches the gateway as "No suitable endpoint
	//found" - BadIdentityTokenRejected - and the connection error has to say what was refused and what permits it:
	///gateway/allowPlaintextPassword, which sets open62541's allowNonePolicyPassword.  Neither the embedded OpcServer nor
	//External offers this shape - both encrypt the token on their None endpoints - so the suite brings its own server:  one None
	//endpoint, no certificate, one login, and the server-side allowNonePolicyPassword, without which open62541's server
	//refuses such a token just as its client does.
	Ω plainPasswordServer( EThrowawayPort port )ε->up<TestUaServer>{
		return mu<TestUaServer>( port, []( UA_ServerConfig& config ){
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, nullptr) );
			UA_UsernamePasswordLogin login{ UA_STRING_STATIC("plain"), UA_STRING_STATIC("in-the-clear") };
			UAε( UA_AccessControl_default(&config, false, nullptr, 1, &login) );//no anonymous; the token policy follows the only security policy there is - None.
			UAε( UA_ServerConfig_addAllEndpoints(&config) );
			config.allowNonePolicyPassword = true;
		});
	}

	class PlaintextPasswordTests : public ClientFixture{
	protected:
		static constexpr EThrowawayPort Port{ EThrowawayPort::PlaintextPassword };
		Ω SetUpTestSuite()->void{
			_server = plainPasswordServer( Port );
			_connection = GetConnection( Slug, Url(Port), "" );
		}
		Ω TearDownTestSuite()->void{
			PurgeServerCnnctn( Slug );
			_connection = nullopt;
			_server = nullptr;
		}
		α Attempt()ι->string{ return ClientFixture::Attempt( Slug, Credential{User{"plain", "in-the-clear"}}, Ƒ("None|Username@None|{}", _connection->Url) ); }//what the connect failed with, "" when it connected.
		inline static const string Slug{ "opcTestsPlaintextPassword" };
		inline static up<TestUaServer> _server;
		inline static optional<ServerCnnctn> _connection;
	};


	TEST_F( PlaintextPasswordTests, RefusedByDefault ){
		ASSERT_FALSE( Settings::FindBool("/gateway/allowPlaintextPassword").value_or(false) );
		let what = Attempt();
		EXPECT_FALSE( _client );
		EXPECT_TRUE( what.contains("BadIdentityTokenRejected") ) << what;
		EXPECT_TRUE( what.contains("in the clear") ) << what;//what was refused...
		EXPECT_TRUE( what.contains("/gateway/allowPlaintextPassword") ) << what;//...and what permits it.
		let errors = UAClient::ConnectErrors();//serverConnections{connectionStatus} shows the same line.
		auto error = errors.find( Slug ); ASSERT_NE( error, errors.end() );
		EXPECT_TRUE( error->second.contains("/gateway/allowPlaintextPassword") ) << error->second;
	}

	TEST_F( PlaintextPasswordTests, SentWhenTheSettingAllowsIt ){
		absl::Cleanup restore = []{ try{ Settings::Set("/gateway/allowPlaintextPassword", false); }catch( const std::exception& ){} };//every later client in the process reads it.
		Settings::Set( "/gateway/allowPlaintextPassword", true );
		let what = Attempt();
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (None/None)", Negotiated(_client) );
	}
}
