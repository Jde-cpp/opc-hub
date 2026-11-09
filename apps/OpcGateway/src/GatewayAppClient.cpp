#include "GatewayAppClient.h"
#include <jde/ql/QLAwait.h>
#include <jde/web/server/SessionGraphQL.h>
#include <jde/calendar/client/Calendars.h>
#include "ql/GatewayQL.h"

namespace Jde::Opc{
	static sp<App::Client::IAppClient> _appClient = ms<Gateway::GatewayAppClient>();
	α Gateway::AppClient()ι->sp<App::Client::IAppClient>{ return _appClient; }
	α Gateway::SetAppClient( sp<App::Client::IAppClient> client )ι->void{ _appClient = move(client); }//the displaced client's dtor removes its IShutdown registration.
}
namespace Jde::Opc::Gateway{
	//The instance page's Web Sessions tab (#364):  this socket is the only way to this process's sessions, as the OpcServer's -
	//GatewayQL routes none, since its acl holds no app schema and so no gate it could refuse with.  The AppServer gated the caller.
	α GatewayAppClient::ClientQuery( QL::RequestQL&& q, Jde::UserPK executer, SL sl )ε->up<TAwait<jvalue>>{
		if( Web::Server::SessionGraphQL::IsForwarded(q) )
			return Web::Server::SessionGraphQL::Forwarded( AppClient() ).Select( q.Queries().front(), executer, sl );
		return mu<QL::QLAwait<>>( move(q), QL::Creds{executer}, Gateway::QLPtr(), sl );
	}
	//The calendar cache's QL was the old session's and died with it, and the edits made while this process was away raised no
	//event here:  it takes the new session's and reads the calendars again (docs/calendar/spec.md *The client*).
	α GatewayAppClient::OnReconnected()ι->void{
		try{
			Calendar::Client::Calendars::Reload( QLServer() );
		}
		catch( const std::exception& e ){//the new session is gone already:  its close reconnects, and that runs this again.
			WARNT( ELogTags::App, "The calendars were not reloaded on the new session:  {}", e.what() );
		}
	}
}