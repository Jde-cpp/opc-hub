#pragma once
#include "../../OpcGateway/src/GatewayAppClient.h"

namespace Jde::Opc::Hub{
	//The gateway's app client when the AppServer is this process: every round trip the socket client makes - session
	//lookup, JWT validation, GraphQL to the app db, OPC user/password login, the ACL snapshot - is answered in-process
	//through the seams the embedded LocalClient already uses (IApp::IsLocal, IRequestHandler::AppServer, LocalQL).
	//Startup installs it with Opc::Gateway::SetAppClient before Gateway::Startup captures AppClient(), hands it the
	//AppServer's Authorize (SetAcl) and registration pks (SetAppPKs); there is no socket, so Connected() is unconditional.
	struct HubAppClient final : Opc::Gateway::GatewayAppClient{
		α IsLocal()Ι->bool override{ return true; }
		α Connected()Ι->bool override{ return true; }
		α QLServer()Ε->sp<QL::IQL> override;
		α UserPK()Ι->Jde::UserPK override{ return Jde::UserPK{Jde::UserPK::System}; }//as LocalClient: the hub is the AppServer.
		α PublicKey()Ι->const Crypto::PublicKey& override;//the AppServer's - the key that signs the JWTs the gateway verifies.
		//The session lookup a short issued token and a renewal make on the gateway's endpoint (UAAccess), answered from this
		//process's one session map as the AppServer answers a registered instance's ask:  Extend, which slides a live session and
		//revives no expired one.  Not IAppClient's socket round trip, and no longer IApp's empty pointer, which the endpoint dereferenced.
		α SessionInfoAwait( SessionPK id, SL sl )ι->up<TAwait<Web::FromServer::SessionInfo>> override;
		α AddSession( str domain, str loginName, Access::ProviderPK providerPK, str userEndPoint, bool isSocket, SRCE )ε->up<TAwait<Web::FromServer::SessionInfo>> override;
	};
}