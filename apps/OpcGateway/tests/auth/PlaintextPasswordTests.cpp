#include <open62541/plugin/accesscontrol_default.h>
#include <jde/fwk/settings.h>
#include "../utils/helpers.h"
#include "../utils/TestUaServer.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	//security-matrix #6:  a credential in the clear.  A server that offers the Username token only under a token policy of None,
	//on a None endpoint, asks for the password unencrypted on the wire.  open62541's client refuses that by default
	//(matchUserTokenPolicy, "must not be transmitted without encryption"), which reaches the gateway as "No suitable endpoint
	//found" - BadIdentityTokenRejected - and the connection error has to say what was refused and what permits it:
	///gateway/allowPlaintextPassword, which sets open62541's allowNonePolicyPassword.  Neither the embedded OpcServer nor
	//Kepware offers this shape - both encrypt the token on their None endpoints - so the suite brings its own server:  one None
	//endpoint, no certificate, one login, and the server-side allowNonePolicyPassword, without which open62541's server
	//refuses such a token just as its client does.
	Ω plainPasswordServer( uint16_t port )ε->up<TestUaServer>{
		return mu<TestUaServer>( port, []( UA_ServerConfig& config ){
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, nullptr) );
			UA_UsernamePasswordLogin login{ UA_STRING_STATIC("plain"), UA_STRING_STATIC("in-the-clear") };
			UAε( UA_AccessControl_default(&config, false, nullptr, 1, &login) );//no anonymous; the token policy follows the only security policy there is - None.
			UAε( UA_ServerConfig_addAllEndpoints(&config) );
			config.allowNonePolicyPassword = true;
		});
	}

	class PlaintextPasswordTests : public ::testing::Test{
	protected:
		static constexpr uint16_t Port{ 4855 };//4840 the embedded OpcServer, 4850-4852 the pubsub and emulator suites, 4856 SecurityPolicyTests.
		Ω SetUpTestSuite()->void{
			_server = plainPasswordServer( Port );
			_connection = GetConnection( Slug, Ƒ("opc.tcp://127.0.0.1:{}", Port), "" );
		}
		Ω TearDownTestSuite()->void{
			if( _connection ){
				PurgeServerCnnctn( _connection->Id );
				_connection = nullopt;
			}
			_server = nullptr;
		}
		α TearDown()ι->void override{
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		α Connect()ι->ConnectAwait::Task;
		α Attempt()ε->string;//what the connect failed with, "" when it connected.
		inline static const string Slug{ "opcTestsPlaintextPassword" };
		inline static up<TestUaServer> _server;
		inline static optional<ServerCnnctn> _connection;
		up<Exception> _exception;
		sp<UAClient> _client;
		atomic_flag _done;
	};

	α PlaintextPasswordTests::Connect()ι->ConnectAwait::Task{
		try{
			_client = co_await UAClient::GetClient( Slug, Credential{User{"plain", "in-the-clear"}} );
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		_done.test_and_set();
		_done.notify_all();
	}
	α PlaintextPasswordTests::Attempt()ε->string{
		_exception = nullptr; _done.clear();
		Connect();
		_done.wait( false );
		let what = _exception ? string{_exception->what()} : string{};
		INFO( "MATRIX|None|Username@None|{}|{}", _connection->Url, what.empty() ? Negotiated(_client) : what );
		return what;
	}

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
		struct Restore final{ ~Restore(){ try{ Settings::Set("/gateway/allowPlaintextPassword", false); }catch( const std::exception& ){} } } restore;//every later client in the process reads it.
		Settings::Set( "/gateway/allowPlaintextPassword", true );
		let what = Attempt();
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (None/None)", Negotiated(_client) );
	}
}
