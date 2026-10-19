#include "QuerySessionsAwait.h"
#include "../ServerSocketSession.h"

#define let const auto

namespace Jde::App::Server{
	α QuerySessionsAwait::Suspend()ι->void{
		//Iterate a local copy and pass the total by value: a dead session throws while constructing its QueryClient (before the
		//co_await), so the last SessionQuery completes synchronously, resumes the awaiter and destroys this temporary (with
		//_sessions) mid-loop.  The loop iterator and the completion total must therefore not live in `this`.
		let sessions = _sessions;
		let total = (uint)sessions.size();
		for( let& session : sessions )
			SessionQuery( session, total );
	}
	α QuerySessionsAwait::SessionQuery( sp<ServerSocketSession> session, uint total )ι->TAwait<jvalue>::Task{
		uint completed;//count responses independently of _results.size(): a duplicate ConnectionPK (e.g. 0) collapses map entries and would otherwise wedge completion.
		try{
			auto result = co_await session->QueryClient( _ql, _executer );
			lg _{ _resultsMutex };
			_results.emplace( session->ConnectionPK(), move(result) );
			completed = ++_completed;
		}
		catch( const std::exception& e ){
			lg _{ _resultsMutex };
			_results.emplace( session->ConnectionPK(), jobject{{"error", e.what()}} );
			completed = ++_completed;
		}
		if( completed == total )
			Resume( move(_results) );//may synchronously destroy `this` - touch no member afterward.
	}
	α QuerySessionsAwait::await_resume()ε->flat_map<ConnectionPK, jvalue>{
		return Promise() ? base::await_resume() : _results;
	}
}