#include "SoakAppClient.h"
#include <jde/app/IApp.h>

namespace Jde::Opc::Gateway::Soak{
	static sp<App::Client::IAppClient> _appClient = ms<SoakAppClient>();
	α AppClient()ι->sp<App::Client::IAppClient>{ return _appClient; }

	struct SoakQL : TAwait<jvalue>{
		using base = TAwait<jvalue>;
		SoakQL( QL::RequestQL&& q, Jde::UserPK executer, SL sl )ι:
			base{ sl },
			_q{ move(q) },
			_executer{ executer }
		{}
		α await_ready()ι->bool override{ return true; }
		α await_resume()ε->jvalue override;
	private:
		α Suspend()ι->void override{ ASSERT(false); }
		QL::RequestQL _q;
		Jde::UserPK _executer;
	};
	α SoakAppClient::ClientQuery( QL::RequestQL&& q, Jde::UserPK executer, SL sl )ε->up<TAwait<jvalue>>{
		return mu<SoakQL>( move(q), executer, sl );
	}

	α SoakQL::await_resume()ε->jvalue{
		THROW_IF( !_q.IsQueries(), "Only queries are supported." );
		jvalue y;
		for( const auto& table : _q.Queries() ){
			THROW_IF( table.JsonName!="status", "Table {} not supported.", table.JsonName );
			y = App::IApp::Status();
		}
		return y;
	}
}