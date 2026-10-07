#include <jde/web/client/socket/ClientSocketAwait.h>
#include <jde/web/client/socket/IClientSocketSession.h>

namespace Jde::Web::Client{
	α IClientSocketVoidAwait::SessionId()ι->SessionPK{ return _session->Id(); }

	α IClientSocketVoidAwait::Suspend( std::any hCoroutine )ι->void{
		_session->AddTask( _requestId, hCoroutine );
		_session->AddTimeout( _requestId );//C6: every request goes through here, so this is the one place a deadline covers them all.
		_session->Write( move(_request) );
	}
}