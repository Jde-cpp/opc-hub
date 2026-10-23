#include "HubAppClient.h"
#include <jde/fwk/str.h>
#include <jde/web/server/Sessions.h>
#include <jde/web/server/Web.FromServer.h>
#include "../../AppServer/src/LocalClient.h"
#include "../../AppServer/src/awaits/AddSessionAwait.h"

namespace Jde::Opc::Hub{
	α HubAppClient::SessionInfoAwait( SessionPK id, SL sl )ι->up<TAwait<Web::FromServer::SessionInfo>>{
		return mu<CompletedAwait<Web::FromServer::SessionInfo>>( [id, sl]{
			auto info = Web::Server::Sessions::Extend( id );
			if( !info )//NotFound, as the AppServer answers over the socket:  no such session, not could not ask.
				throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "[{}] Session not found.", hex(id) };
			return Web::Server::ToProto( *info );
		}, sl );
	}
	α HubAppClient::QLServer()Ε->sp<QL::IQL>{
		return App::Server::QLPtr();
	}
	α HubAppClient::PublicKey()Ι->const Crypto::PublicKey&{
		return App::Server::AppClient()->PublicKey();
	}
	α HubAppClient::AddSession( str domain, str loginName, Access::ProviderPK providerPK, str userEndPoint, bool isSocket, SL sl )ε->up<TAwait<Web::FromServer::SessionInfo>>{
		return mu<App::Server::AddSessionAwait>( domain, loginName, providerPK, userEndPoint, isSocket, Jde::UserPK{}, sl );
	}
}
