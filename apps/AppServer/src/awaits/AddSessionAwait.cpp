#include "AddSessionAwait.h"
#include <jde/access/server/accessServer.h>
#include <jde/web/server/Sessions.h>

#define let const auto
namespace Jde::App::Server{
	α AddSessionAwait::Execute()ι->TAwait<Jde::UserPK>::Task{
		try{
			TRACET( ELogTags::Access, "AddSession user: '{}', endpoint: '{}', provider: {}, is_socket: {}", _domain+"/"+_loginName, _userEndPoint, _providerPK, _isSocket );
			let userPK = co_await Access::Server::Authenticate( _loginName, _providerPK, _domain );
			auto info = Web::Server::Sessions::Add( userPK, move(_userEndPoint), _isSocket, _mintedBy );
			TRACET( ELogTags::Access, "AddSession id: {:x}", info->SessionId );
			Resume( Web::Server::ToProto(*info) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}
