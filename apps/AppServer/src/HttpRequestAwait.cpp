#include "HttpRequestAwait.h"
#include <jde/fwk/process/execution.h>
#include "LocalClient.h"
#include "WebServer.h"
#include "types/rest/json.h"
#define let const auto

namespace Jde::App::Server{
	HttpRequestAwait::HttpRequestAwait( HttpRequest&& req, SL sl )ι:
		base{ move(req), sl }
	{}

	α ValueJson( string&& value )ι->jvalue{ return jobject{{"value", move(value)}}; }//build directly so quotes/backslashes are escaped (Ƒ+Parse threw on unescaped input).

	Ω googleAuthClientId()ι->jvalue{
		return ValueJson( Settings::FindString("/http/clientSettings/googleAuthClientId").value_or("GoogleAuthClientId Not Configured.") );//same pointer SettingQLAwait serves the ql `setting(target:"googleAuthClientId")` from - the old "GoogleAuthClientId" was both unrooted and a key no config defines, so this endpoint always answered "Not Configured".
	}
	Ω instances( bool opcServers, bool identified )ι->jvalue{//what discovery needs anonymously, the rest to a caller with a user.
		let apps = Server::FindApplications( opcServers ? "Jde.OpcServer" : "Jde.OpcGateway" );
		jarray japps;
		for( auto& app : apps )
			japps.push_back( identified ? ToJson(app) : ToDiscoveryJson(app) );
		return jobject{ {"servers", japps} };
	}
	α Routes::Ready( HttpRequest& req )ι->up<jvalue>{
		if( req.IsGet("/GoogleAuthClientId") ){
			req.LogRead();
			return mu<jvalue>( googleAuthClientId() );
		}
		if( req.IsGet("/opcGateways") || req.IsGet("/opcServers") ){
			req.LogRead();
			return mu<jvalue>( instances(req.Target()=="/opcServers", req.UserPK()!=Jde::UserPK{}) );//every http caller is minted a session; only a logged-in one has a user.
		}
		return {};
	}
	α Routes::LoginJwt( HttpRequest&& req, IHttpRequestAwait::Handle h )ι->void{
		try{
			req.LogRead();
			let authorization = req.Header( "Authorization" );
			THROW_IFX( authorization.empty() || !authorization.starts_with("Bearer "), RestException(EHttpStatus::Unauthorized,SRCE_CUR, move(req), "Missing or invalid Authorization header") );
			jobject j{ {"expiration", ToIsoString<seconds>(req.SessionInfo->Expiration)} };
			req.SessionInfo->IsInitialRequest = true;  //expecting sessionId to be set.
			h.promise().SetValue( {move(j), move(req)} );
		}
		catch( runtime_error& e ){
			h.promise().SetExp( move(e) );
		}
		h.resume();
	}

	α Routes::Logout( HttpRequest&& req, IHttpRequestAwait::Handle h )ι->void{
		try{
			req.LogRead();
			jobject j{ {"removed", Sessions::Remove(req.SessionInfo->SessionId)} };
			h.promise().SetValue( {move(j), move(req)} );
		}
		catch( runtime_error& e ){
			h.promise().SetExp( move(e) );
		}
		h.resume();
	}

	α HttpRequestAwait::await_ready()ι->bool{
		_readyResult = Routes::Ready( _request );
		return _readyResult!=nullptr;
	}
	α HttpRequestAwait::Suspend()ι->void{
		if( _request.IsPost("/login") )
			Routes::LoginJwt( move(_request), _h );
		else if( _request.IsPost("/logout") )
			Routes::Logout( move(_request), _h );
		else{
			_request.LogRead();
			auto target = _request.Target();
			ResumeExp( RestException{EHttpStatus::NotFound, SRCE_CUR, move(_request), "Unknown target '{}'", move(target)} );
		}
	}

	α HttpRequestAwait::await_resume()ε->HttpTaskResult{
		if( auto e = Promise() ? Promise()->MoveExp() : nullptr; e ){
			if( auto rest = dynamic_cast<RestException*>(e.get()); rest )
				rest->Throw();
			throw RestException{ e->HttpStatus(), move(*e), move(_request) };
		}
		return _readyResult
			? HttpTaskResult{ move(*_readyResult), move(_request) }
			: Promise()->Value() ? move( *Promise()->Value() ) : HttpTaskResult{ {}, move(_request) };
	}
}