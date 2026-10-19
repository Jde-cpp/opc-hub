#include "AppInstanceHook.h"
#include <jde/ql/types/TableQL.h>
#include "../WebServer.h"
#include "../ServerSocketSession.h"

#define let const auto

namespace Jde::App::Server{
	α AppInstanceHook::Start( const QL::MutationQL& m, UserPK, SL sl )ι->HookResult{
		return m.JsonTableName=="applicationInstance" ? mu<ExceptionAwait<jvalue>>( mu<Exception>(Exception{sl, ELogLevel::Critical, "Start instance not implemented."}), sl ) : nullptr;
	}

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

	α AppInstanceHook::Stop( const QL::MutationQL& m, UserPK, SL sl )ι->HookResult{
		return m.JsonTableName=="applicationInstance" ? mu<StopAwait>( m, _appClient, sl ) : nullptr;
	}
}