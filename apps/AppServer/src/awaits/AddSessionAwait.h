#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/access/usings.h>
#include <jde/web/server/Web.FromServer.h>

namespace Jde::App::Server{
	//Authenticate, mint a web session, hand back its proto:  ServerSocketSession's kAddSession, and an in-process app client
	//(OpcHub's gateway) whose OPC user/password login used to travel that round trip.  mintedBy:  the account that may end
	//what it began (SessionGraphQL::Purge) - the asking socket's;  none in-process.
	struct AddSessionAwait final : TAwaitEx<Web::FromServer::SessionInfo, TAwait<Jde::UserPK>::Task>{
		using base = TAwaitEx<Web::FromServer::SessionInfo, TAwait<Jde::UserPK>::Task>;
		AddSessionAwait( string domain, string loginName, Access::ProviderPK providerPK, string userEndPoint, bool isSocket, Jde::UserPK mintedBy, SRCE )ι:
			base{sl}, _domain{move(domain)}, _loginName{move(loginName)}, _userEndPoint{move(userEndPoint)}, _providerPK{providerPK}, _isSocket{isSocket}, _mintedBy{mintedBy}{}
	private:
		α Execute()ι->TAwait<Jde::UserPK>::Task override;
		string _domain, _loginName, _userEndPoint;
		Access::ProviderPK _providerPK;
		bool _isSocket;
		Jde::UserPK _mintedBy;
	};
}
