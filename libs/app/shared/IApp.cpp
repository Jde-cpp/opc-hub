#include <jde/app/IApp.h>
#include <jde/fwk/settings.h>
#include <jde/ql/types/RequestQL.h>

#define let const auto

namespace Jde::App{
	//`status` from IApp::Status(), and nothing else:  what an app serving no tables of its own answers the AppServer.
	struct StatusQLAwait final : TAwait<jvalue>{
		StatusQLAwait( QL::RequestQL&& q, SL sl )ι:TAwait<jvalue>{ sl }, _q{ move(q) }{}
		α await_ready()ι->bool override{ return true; }
		α await_resume()ε->jvalue override{
			THROW_IF( !_q.IsQueries(), "Only `status` queries are supported;  got {}.", _q.ToString() );
			jvalue y;
			for( let& table : _q.Queries() ){
				THROW_IF( table.JsonName!="status", "Table {} not supported.", table.JsonName );
				y = IApp::Status();
			}
			return y;
		}
	private:
		α Suspend()ι->void override{ ASSERT(false); }
		QL::RequestQL _q;
	};
	α IApp::ClientQuery( QL::RequestQL&& q, UserPK, SL sl )ε->up<TAwait<jvalue>>{
		return mu<StatusQLAwait>( move(q), sl );
	}
}
namespace Jde{
	α App::InstanceName()ι->string{
		auto instanceName = Settings::FindString( "/instanceName" ).value_or( "" );
		return instanceName.empty() ? _debug ? "Debug" : "Release" : instanceName;
	}
}