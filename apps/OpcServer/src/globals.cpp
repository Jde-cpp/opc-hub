#include "globals.h"
#include "jde/fwk/process/process.h"
#include <jde/db/meta/AppSchema.h>
#include <jde/access/access.h>
#include <jde/app/client/IAppClient.h>
#include "OpcServerAppClient.h"
#include "UAServer.h"
#include "pubsub/PubSubReader.h"

namespace Jde::Opc {
	sp<DB::AppSchema> _appSchema;
	up<Server::UAServer> _ua;
	up<Server::PubSubReader> _pubSub;//after _ua: destroyed first, its connection belongs to that server.
	static sp<App::Client::IAppClient> _appClient = ms<Server::OpcServerAppClient>();

	α Server::Initialize( sp<DB::AppSchema> schema )ε->void{
		_appSchema = schema;
		_pubSub.reset();//a re-Initialize (test fixtures) replaces the server it was built on.
		_ua.reset();//before the next is made:  its historian takes the lock on hist.path that this one's holds.
		_ua = mu<UAServer>();//throws
		static std::once_flag registered;//once, not per Initialize:  it resets whichever server is current.
		std::call_once( registered, []{
			Process::AddShutdownFunction( [](bool, SL){
				_pubSub.reset();
				_ua.reset();
			} );
		} );
	}

	α Server::ResourceSuffix()ι->string{ return Settings::FindString( "/opcServer/resource" ).value_or( "" ); }
	α Server::InstanceSchema()ι->string{ return Access::InstanceSchemaName( "opc", ResourceSuffix() ); }
	α Server::AppClient()ι->sp<App::Client::IAppClient>{ return _appClient; }

	α Server::GetSchema()ι->DB::AppSchema&{ return *_appSchema; }
	α Server::GetSchemaPtr()ι->sp<DB::AppSchema>{ return _appSchema; }
	α Server::GetUAServer()ι->UAServer&{ return *_ua; }
	α Server::FindUAServer()ι->UAServer*{ return _ua.get(); }
	α Server::StartPubSub( const jobject& settings, SL sl )ε->void{ _pubSub = mu<PubSubReader>( GetUAServer(), settings, sl ); }
	α Server::PubSub()ι->PubSubReader*{ return _pubSub.get(); }
}