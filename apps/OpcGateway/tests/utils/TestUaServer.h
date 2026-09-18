#pragma once
#include <thread>
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/Logger.h>

namespace Jde::Opc::Gateway::Tests{
	//A throwaway open62541 server on loopback, for the shapes neither the embedded OpcServer nor Kepware offers:  a password in
	//the clear (PlaintextPasswordTests), a security policy the gateway does not carry (SecurityPolicyTests).  `configure` gets the
	//config after UA_ServerConfig_setBasics_withPort and the loopback url - setBasics listens on every interface - and adds the
	//policies, the access control and the endpoints;  the server then runs on its own thread until the object goes.
	struct TestUaServer final{
		TestUaServer( uint16_t port, function<void(UA_ServerConfig&)> configure )ε{
			UA_ServerConfig config{};
			config.logging = &_logger;
			try{
				if( const auto sc = UA_ServerConfig_setBasics_withPort(&config, port); sc ) throw UAException{ sc };
				const auto url = Ƒ( "opc.tcp://127.0.0.1:{}", port );
				UA_Array_delete( config.serverUrls, config.serverUrlsSize, &UA_TYPES[UA_TYPES_STRING] );
				config.serverUrls = nullptr;
				config.serverUrlsSize = 0;
				UA_String urls[1]{ UA_String{url.size(), (UA_Byte*)url.data()} };//a view: UA_Array_copy deep-copies.
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
		α Server()ι->UA_Server*{ return _server; }//the server api is thread-safe (UA_MULTITHREADING) - ApplicationUriTests reads a session's attributes through it.
		TestUaServer( const TestUaServer& )=delete;
		TestUaServer& operator=( const TestUaServer& )=delete;
	private:
		Opc::Logger _logger{};
		UA_Server* _server{};
		std::jthread _thread;
	};
}
