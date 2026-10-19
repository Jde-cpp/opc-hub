#include "HubAppClient.h"
#include "../../AppServer/src/LocalClient.h"
#include "../../AppServer/src/awaits/AddSessionAwait.h"

namespace Jde::Opc::Hub{
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
