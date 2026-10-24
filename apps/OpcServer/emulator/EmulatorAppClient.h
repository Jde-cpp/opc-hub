#pragma once
#include <jde/app/client/IAppClient.h>

namespace Jde::Opc::Emulator{
	α AppClient()ι->sp<App::Client::IAppClient>;

	//The emulator's AppServer client: logs in (the session id becomes the OPC UA issued token on the OpcServer) and answers the
	//log-settings routes.  Under -transport=server it is the PLC server's hub too (UAAccess):  a session's issued token is checked
	//by SessionInfoAwait and a certificate or username is looked up by QuerySync, IAppClient's defaults over the socket session it
	//already has, as this process's user - so once access.users is enforced that user needs Read on it.
	struct EmulatorAppClient final : App::Client::IAppClient{
		α ClientQuery( QL::RequestQL&& q, Jde::UserPK executer, SRCE )ε->up<TAwait<jvalue>> override;
	};
}