#pragma once
#include <jde/access/usings.h>
#include <jde/web/client/proto/Web.FromServer.pb.h>
#include "AuthAwait.h"

namespace Jde::Opc::Gateway{
	struct UAClient;

	struct PasswordAwait : AuthAwait<optional<Web::FromServer::SessionInfo>>{// nullopt=use current session.
		PasswordAwait( string loginName, string password, string opcNK, string endpoint, bool isSocket, SessionPK sessionId, SRCE )ι;
		α await_resume()ε->optional<Web::FromServer::SessionInfo>;
	private:
		α OnSuccess()ι->void{ CheckProvider(); }
		α CheckProvider()ι->TAwait<Access::ProviderPK>::Task;
		α AddSession( Access::ProviderPK providerPK )ι->TAwait<Web::FromServer::SessionInfo>::Task;//IAppClient::AddSession's await type - socket or in-process.
	};
}