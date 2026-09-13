#pragma once
#include "OpcServerSession.h"
#include "../UAClient.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	Τ struct AuthAwait : TAwaitEx<T, TAwait<sp<UAClient>>::Task>{
		using base = TAwaitEx<T, TAwait<sp<UAClient>>::Task>;
		AuthAwait( Credential cred, ServerCnnctnNK opcNK, string endpoint, bool isSocket, SessionPK sessionId, SRCE )ι:
			base{ sl }, _cred{ move(cred) }, _opcNK{ move(opcNK) }, _endpoint{ move(endpoint) }, _isSocket{ isSocket }, _sessionId{sessionId}{}
		α await_ready()ι->bool override;
		α Execute()ι->TAwait<sp<UAClient>>::Task override;
		β OnSuccess()ι->void=0;
	protected:
		Credential _cred; ServerCnnctnNK _opcNK; string _endpoint; bool _isSocket; sp<UAClient> _client; SessionPK _sessionId;
	};

	Ŧ AuthAwait<T>::await_ready()ι->bool{
		return AuthCache( _cred, _opcNK, _sessionId ).value_or(false);
	}
	Ŧ AuthAwait<T>::Execute()ι->TAwait<sp<UAClient>>::Task{
		try{
			_client = co_await UAClient::GetClient( _opcNK, _cred, base::_sl );
			if( _opcNK.empty() )//"" is the default connection (ServerCnnctnAwait) - the login page's username with no DOMAIN\.  Everything after the connect keys on the slug it resolved to:  the provider lookup (PasswordAwait), the credential cache (Gateway::AddSession - read back by slug), the session counts.
				_opcNK = _client->Slug();
			OnSuccess();
		}
		catch( runtime_error& e ){
			base::ResumeExp( move(e) );
		}
	}
}