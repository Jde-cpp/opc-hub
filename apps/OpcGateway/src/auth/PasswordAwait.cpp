#include "PasswordAwait.h"
#include <jde/app/client/IAppClient.h>
#include "../GatewayAppClient.h"
#include "../UAClient.h"
#include "UM.h"

#define let const auto

namespace Jde::Opc::Gateway{
	PasswordAwait::PasswordAwait( string loginName, string password, string opcNK, string endpoint, bool isSocket, SessionPK sessionId, SL sl )ι:
		base{ sl }, _cred{ User{move(loginName), move(password)} }, _opcNK{ move(opcNK) }, _endpoint{ move(endpoint) }, _isSocket{ isSocket }, _sessionId{ sessionId }
	{}
	α PasswordAwait::await_ready()ι->bool{
		return AuthCache( _cred, _opcNK, _sessionId ).value_or( false );
	}
	α PasswordAwait::Execute()ι->TAwait<sp<UAClient>>::Task{
		try{
			auto client = co_await UAClient::GetClient( _opcNK, _cred, _sl );
			if( _opcNK.empty() )//"" is the default connection (ServerCnnctnAwait) - the login page's username with no DOMAIN\.  Everything after the connect keys on the slug it resolved to:  the provider lookup, the credential cache (Gateway::AddSession - read back by slug), the session counts.
				_opcNK = client->Slug();
			ProviderAwait find{ _opcNK };
			auto providerPK = co_await Any( find );
			if( !providerPK ){//a connection without its provider row - seeded (release-opcServer.mutation) onto a db whose provider 7 was already taken, or a row purged by hand: create it here, as the insert hook would have.
				ProviderMAwait create{ DB::Key{_opcNK}, true };
				providerPK = co_await Any( create );
			}
			THROW_IF( providerPK==0, "Provider not found for '{}'.", _opcNK );
			auto sessionInfo = co_await Any( *AppClient()->AddSession(_opcNK, _cred.LoginName(), providerPK, _endpoint, _isSocket) );
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
		//Cache hit (await_ready()==true): AuthCache already registered _sessionId as authenticated for this opc, but Execute()/AddSession never ran so there's no SessionInfo. Return one carrying that session id instead of nullopt, otherwise Login skips setting the session id and the client gets none. A signed-in one:  AuthCache vouches for no other, so a login page's anonymous session takes the full path (install-issues #47). (The deeper "a fresh login shouldn't reuse another session's cache" concern is the AuthCache design, review #14.)
		Web::FromServer::SessionInfo info;
		info.set_session_id( _sessionId );
		return info;
	}
}