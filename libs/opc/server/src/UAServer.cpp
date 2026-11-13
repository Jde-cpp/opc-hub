#include <jde/opc/server/UAServer.h>
#include <jde/fwk/process/thread.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <jde/opc/server/INodeAccess.h>
#include <jde/opc/server/UAAccess.h>

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
			Wake( *_ua );
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
			RunShutdown( *_ua );//the delete is the destructor's, after it joins this thread.
			INFOT( ELogTags::App, "OPC UA server stopped." );
		}};
	}
	//A no-op if the loop is between polls:  the stop flag is then seen before the next one.
	α UAServer::Wake( UA_Server& server )ι->void{
		auto& el = *UA_Server_getConfig( &server )->eventLoop;
		el.cancel( &el );
	}
	//UA_Server_run_shutdown iterates once more, then until its components have stopped, and with no timer due each poll waits its whole
	//timeout:  a timer every 10 ms ends each one.  A cancel can't stand in - it only reaches a poll already running.
	α UAServer::RunShutdown( UA_Server& server )ι->void{
		auto& el = *UA_Server_getConfig( &server )->eventLoop;
		UA_UInt64 tick{};
		let added = el.addTimer( &el, []( void*, void* ){}, nullptr, nullptr, 10.0, nullptr, UA_TIMERPOLICY_CURRENTTIME, &tick );
		UA_Server_run_shutdown( &server );
		if( added==UA_STATUSCODE_GOOD )
			el.removeTimer( &el, tick );
	}
	//A delayed callback, open62541's own way onto the loop's thread from another (UA_AsyncManager's):  the queue is lock-free and
	//runs in order at the top of each pass, before the poll, and the cancel ends a poll running now.
	struct Posted final{
		UA_DelayedCallback Callback{};
		absl::AnyInvocable<void(UA_Server&)> F;
	};
	α UAServer::Post( UA_Server& server, absl::AnyInvocable<void(UA_Server&)> f )ι->void{
		auto posted = new Posted{ .F=std::move(f) };
		posted->Callback.callback = []( void* application, void* context ){
			up<Posted> p{ static_cast<Posted*>(context) };
			try{
				p->F( *static_cast<UA_Server*>(application) );
			}
			catch( const std::exception& e ){
				ERRT( ELogTags::App, "A post to the OPC UA server's thread threw:  {}", e.what() );
			}
		};
		posted->Callback.application = &server;
		posted->Callback.context = posted;
		auto& el = *UA_Server_getConfig( &server )->eventLoop;
		el.addDelayedCallback( &el, &posted->Callback );
		el.cancel( &el );
	}

	α UAServer::Sessions()ι->sp<UAAccess::SessionList>{ return UAAccess::Sessions( *_ua ); }
	α UAServer::Policies()Ι->vector<std::pair<string,UA_MessageSecurityMode>>{
		vector<std::pair<string,UA_MessageSecurityMode>> y;
		let& config = *UA_Server_getConfig( _ua );
		for( uint i=0; i<config.endpointsSize; ++i )
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