#pragma once
#include <jde/access/usings.h>
#include <jde/web/client/proto/Web.FromServer.pb.h>
#include "OpcServerSession.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//A user/password login on an OPC server:  the connect with that credential, the server's provider row, then the web session
	//AppServer mints for it.  A credential already authenticated for the session completes in await_ready (AuthCache).
	struct PasswordAwait final : TAwaitEx<optional<Web::FromServer::SessionInfo>, TAwait<sp<UAClient>>::Task>{// nullopt=use current session.
		using base = TAwaitEx<optional<Web::FromServer::SessionInfo>, TAwait<sp<UAClient>>::Task>;
		PasswordAwait( string loginName, string password, string opcNK, string endpoint, bool isSocket, SessionPK sessionId, SRCE )ι;
		α await_ready()ι->bool override;
		α await_resume()ε->optional<Web::FromServer::SessionInfo> override;
	private:
		α Execute()ι->TAwait<sp<UAClient>>::Task override;//its own type is the connect's - UAClient.cpp's reconnect chain keeps ConnectAwait out of Any() - and the rest go through Any().
		Credential _cred; ServerCnnctnNK _opcNK; string _endpoint; bool _isSocket; SessionPK _sessionId;
	};
}