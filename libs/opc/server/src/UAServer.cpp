#include <jde/opc/server/UAServer.h>
#include <jde/fwk/process/thread.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <jde/opc/server/INodeAccess.h>

#define let const auto
namespace Jde::Opc::Server {
	UAServer::UAServer( const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess, sp<IHistoryHost> history )ε:
		_history{ settings, move(history) },
		_config{ settings, move(appClient), move(nodeAccess), &_history },
		_ua{ UA_Server_newWithConfig(&_config) }
	{
		THROW_IF( !_ua, "UA_Server_newWithConfig failed." );//it has cleared or taken _config either way, so nothing is left to free.
		_config.Trust().Install( *_ua );//must target the server-owned config - UA_Server_newWithConfig memset _config. Main thread, before Run() spawns the loop thread.
	}
	UAServer::~UAServer(){
		INFOT( ELogTags::App, "Stopping OPC UA server..." );
		if( _thread.has_value() ){
			_thread->request_stop();
			_thread->join();
			_thread.reset();
		}
		_history.Stop();
		if( _ua ){
			UA_Server_delete( _ua );
			_ua = nullptr;
		}
	}

	α UAServer::Run()ε->void{
		if( _thread )
			return;
		if( let sc = UA_Server_run_startup(_ua); sc ){
			//A failed startup - the port taken - stops the components it started but leaves the event loop running, which the
			//destructor's UA_Server_delete can't free:  stop it as UA_Server_run_shutdown does for a started server.
			auto& el = *UA_Server_getConfig( _ua )->eventLoop;
			if( el.state==UA_EVENTLOOPSTATE_STARTED ){
				el.stop( &el );
				while( el.state!=UA_EVENTLOOPSTATE_STOPPED && !el.run(&el, 100) ){}
			}
			throw UAException{ sc };
		}
		_thread = std::jthread{ [this](std::stop_token st){
			Thread::SetName( "UAServer" );
			//UA_Server_run(...) demands a volatile UA_Boolean*; run the loop manually against the stop token instead. This is what UA_Server_run does internally, and it also drops the original's double run_shutdown.
			while( !st.stop_requested() )
				UA_Server_run_iterate( _ua, true );
			UA_Server_run_shutdown( _ua );//the delete is the destructor's, after it joins this thread.
			INFOT( ELogTags::App, "OPC UA server stopped." );
		}};
	}

	α UAServer::Policies()Ι->vector<std::pair<string,UA_MessageSecurityMode>>{
		vector<std::pair<string,UA_MessageSecurityMode>> y;
		let& config = *UA_Server_getConfig( _ua );
		for( size_t i=0; i<config.endpointsSize; ++i )
			y.emplace_back( Opc::ToString(config.endpoints[i].securityPolicyUri), config.endpoints[i].securityMode );
		return y;
	}
	α UAServer::Namespaces()ι->flat_map<uint,string>{
		flat_map<uint,string> y;
		UA_String ns;
		for( uint i=0; !UA_Server_getNamespaceByIndex(_ua, i, &ns); ++i ){
			y.emplace( i, Opc::ToString(ns) );
			UA_String_clear( &ns );
		}
		return y;
	}
}