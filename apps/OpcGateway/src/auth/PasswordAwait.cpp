#include "PasswordAwait.h"
#include <jde/app/client/IAppClient.h>
#include "../GatewayAppClient.h"
#include "../UAClient.h"
#include "UM.h"

#define let const auto

namespace Jde::Opc::Gateway{
	PasswordAwait::PasswordAwait( string loginName, string password, string opcNK, string endpoint, bool isSocket, SessionPK sessionId, SL sl )ι:
		AuthAwait{ { {move(loginName), move(password)} }, move(opcNK), move(endpoint), isSocket, sessionId, sl }{}

	α PasswordAwait::CheckProvider()ι->TAwait<Access::ProviderPK>::Task{
		try{
			auto providerPK = co_await ProviderAwait{ _opcNK };
			if( !providerPK )//a connection without its provider row - seeded (release-opcServer.mutation) onto a db whose provider 7 was already taken, or a row purged by hand: create it here, as the insert hook would have.
				providerPK = co_await ProviderMAwait{ DB::Key{_opcNK}, true };
			THROW_IF( providerPK==0, "Provider not found for '{}'.", _opcNK );
			AddSession( providerPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α PasswordAwait::AddSession( Access::ProviderPK providerPK )ι->TAwait<Web::FromServer::SessionInfo>::Task{
		try{
			auto sessionInfo = co_await *AppClient()->AddSession( _opcNK, _cred.LoginName(), providerPK, _endpoint, false );
			_cred.SetUserPK( Jde::UserPK{sessionInfo.user_pk()} ); //the identity AppServer resolved for this login - opcSessions reports it.
			Gateway::AddSession( sessionInfo.session_id(), _opcNK, move(_cred) );
			Resume( move(sessionInfo) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
	α PasswordAwait::await_resume()ε->optional<Web::FromServer::SessionInfo>{
		if( Promise() )
			return base::await_resume();
		//Cache hit (await_ready()==true): AuthCache already registered _sessionId as authenticated for this opc, but Execute()/AddSession never ran so there's no SessionInfo. Return one carrying that session id instead of nullopt, otherwise Login skips setting the session id and the client gets none. (The deeper "a fresh login shouldn't reuse another session's cache" concern is the AuthCache design, review #14.)
		Web::FromServer::SessionInfo info;
		info.set_session_id( _sessionId );
		return info;
	}
}