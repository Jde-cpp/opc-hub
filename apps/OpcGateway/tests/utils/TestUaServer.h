#pragma once
#include <thread>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <open62541/plugin/accesscontrol_default.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/opc/uatypes/UAString.h>
#ifndef let
#define let const auto
#endif

namespace Jde::Opc::Gateway::Tests{
	//Each throwaway server's port, one a suite:  beside 4940 the embedded OpcServer and 4850-4852 the pubsub and emulator suites.
	enum class EThrowawayPort : uint16_t{ PlaintextPassword=4855, SecurityPolicyLegacy, RefusedCertificate, HostName, ApplicationUri, SecurityPolicyLying, SecurityPolicyDowngrade };
	Ξ Url( EThrowawayPort port, sv host="127.0.0.1" )ι->string{ return Ƒ( "opc.tcp://{}:{}", host, underlying(port) ); }
	//A throwaway open62541 server on loopback, for the shapes neither the embedded OpcServer nor External offers:  a password in
	//the clear (PlaintextPasswordTests), a security policy the gateway does not carry (SecurityPolicyTests).  `configure` gets the
	//config after UA_ServerConfig_setBasics_withPort and the loopback url - setBasics listens on every interface - and adds the
	//policies, the access control and the endpoints;  the server then runs on its own thread until the object goes.
	struct TestUaServer final{
		//As the other, serving `ssl`'s certificate as the gateway's harness trusts it:  the application uri is set to the
		//certificate's SAN uri, which open62541 holds it against at startup, and `configure` gets the key pair for its policies.
		TestUaServer( EThrowawayPort port, const Crypto::CryptoSettings& ssl, function<void(UA_ServerConfig&, const UA_ByteString* certificate, const UA_ByteString* privateKey)> configure )ε:
			TestUaServer{ port, [&ssl, &configure]( UA_ServerConfig& config ){
				const auto certificate = ToUAByteString( Crypto::ReadCertificate(ssl.Certificate.Path) );
				const auto privateKey = ToUAByteString( Crypto::ReadPrivateKey(ssl.PrivateKey) );
				const auto uri = ssl.Certificate.SanUri();
				THROW_IF( uri.empty(), "{} has no SAN uri.", ssl.Certificate.Path.string() );
				UA_String_clear( &config.applicationDescription.applicationUri );
				config.applicationDescription.applicationUri = UA_STRING_ALLOC( uri.c_str() );
				configure( config, certificate.get(), privateKey.get() );
			}}{}
		TestUaServer( EThrowawayPort port, function<void(UA_ServerConfig&)> configure )ε{
			UA_ServerConfig config{};
			config.logging = &_logger;
			try{
				if( const auto sc = UA_ServerConfig_setBasics_withPort(&config, underlying(port)); sc ) throw UAException{ sc };
				const auto url = Url( port );
				UA_Array_delete( config.serverUrls, config.serverUrlsSize, &UA_TYPES[UA_TYPES_STRING] );
				config.serverUrls = nullptr;
				config.serverUrlsSize = 0;
				UA_String urls[1]{ ToUV(url) };//a view: UA_Array_copy deep-copies.
				if( const auto sc = UA_Array_copy(urls, 1, (void**)&config.serverUrls, &UA_TYPES[UA_TYPES_STRING]); sc ) throw UAException{ sc };
				config.serverUrlsSize = 1;
				configure( config );
			}
			catch( ... ){
				UA_ServerConfig_clear( &config );
				throw;
			}
			_server = UA_Server_newWithConfig( &config );//takes the config over.
			THROW_IF( !_server, "UA_Server_newWithConfig failed." );
			if( const auto sc = UA_Server_run_startup(_server); sc ){
				UA_Server_delete( _server );
				throw UAException{ sc };
			}
			_thread = std::jthread{ [this]( std::stop_token stop ){ while( !stop.stop_requested() ) UA_Server_run_iterate( _server, true ); } };
		}
		~TestUaServer(){
			_thread.request_stop();
			_thread.join();
			UA_Server_run_shutdown( _server );
			UA_Server_delete( _server );
		}
		//Anonymous access on every endpoint the config's policies give, for a server whose test is about anything but credentials.
		Ω Anonymous( UA_ServerConfig& config )ε->void{
			UAε( UA_AccessControl_default(&config, true, nullptr, 0, nullptr) );
			UAε( UA_ServerConfig_addAllEndpoints(&config) );
		}
		α Server()ι->UA_Server*{ return _server; }//the server api is thread-safe (UA_MULTITHREADING) - ApplicationUriTests reads a session's attributes through it.
		TestUaServer( const TestUaServer& )=delete;
		TestUaServer& operator=( const TestUaServer& )=delete;
	private:
		Opc::Logger _logger{};
		UA_Server* _server{};
		std::jthread _thread;
	};
}