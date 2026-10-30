#include "AppInstanceMAwait.h"
#include <jde/ql/types/TableQL.h>
#include "../WebServer.h"
#include "../ServerSocketSession.h"

#define let const auto

namespace Jde::App::Server{
	struct StopAwait : TAwait<jvalue>{
		StopAwait( QL::MutationQL mutation, sp<IApp> appClient, SL sl )ι:
			TAwait<jvalue>{ sl },
			_mutation{ mutation },
			_appClient{ move(appClient) }
		{}
		α Suspend()ι->void override{
			let id = _mutation.Id<ConnectionPK>();
			auto pid = id==_appClient->ConnectionPK() ? Process::ProcessId() : 0;
			if( auto p = pid ? sp<ServerSocketSession>{} : Server::FindConnection(id); p ){
				let& instance = p->Instance();
				//Process::Kill signals a pid in THIS host's namespace, but instance.pid() was measured on the instance's own
				//host - killing it here would hit whatever local process holds that number.  Only stop an instance that
				//advertises this AppServer's host (computed the same way the client advertises it, so it is symmetric).
				let thisHost = Settings::FindString("/http/host").value_or( Process::HostName() );
				if( instance.host()!=thisHost ){
					ResumeExp( Exception{_sl, {ELogTags::SocketServerRead, EHttpStatus::Unauthorized}, "Cannot stop instance on remote host '{}' (this host is '{}'); forward a stop over its socket instead.", instance.host(), thisHost} );
					return;
				}
				pid = instance.pid();
			}
			if( pid ){
				if( Process::Kill(pid) )
					ResumeScaler( 0 );
				else
					ResumeExp( Exception{_sl, {ELogTags::SocketServerRead}, "Failed to kill process."} );
			}
			else
				ResumeExp( Exception{_sl, {ELogTags::SocketServerRead}, "Instance not found."} );
		}
		QL::MutationQL _mutation;
		sp<IApp> _appClient;
	};
}
namespace Jde::App{
	α Server::AppInstanceMutation( const QL::MutationQL& m, sp<IApp> appClient, SL sl )ι->up<TAwait<jvalue>>{
		using enum QL::EMutationQL;
		if( m.JsonTableName!="applicationInstance" || (m.Type!=Start && m.Type!=Stop) )
			return nullptr;
		if( m.Type==Start )
			return mu<ExceptionAwait<jvalue>>( mu<Exception>(Exception{sl, ELogLevel::Critical, "Start instance not implemented."}), sl );
		return mu<Server::StopAwait>( m, move(appClient), sl );
	}
}