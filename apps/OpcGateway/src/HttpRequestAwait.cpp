#include "HttpRequestAwait.h"
#include <jde/ql/IQL.h>
#include <jde/app/client/IAppClient.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include "GatewayAppClient.h"
#include "UAClient.h"
#include "auth/PasswordAwait.h"
#include "jde/fwk/exceptions/Exception.h"
#include "jde/fwk/str.h"
#include "ql/GatewayQL.h"

#define let const auto

namespace Jde::Opc::Gateway{
	HttpRequestAwait::HttpRequestAwait( HttpRequest&& req, SL sl )ι:
		base{ move(req), sl }
	{}

	α HttpRequestAwait::await_ready()ι->bool{
		if( _request.IsGet("/ErrorCodes") ){
			vector<StatusCode> scs;
			string scsString = _request["scs"];
			auto strings = Str::Split( scsString );
			jarray j;
			for( let s : strings ){
				if( let sc = Str::TryTo<StatusCode>(s); sc )
					j.push_back( UAException::ToJson(*sc, true) );
			}
			_readyResult = mu<jvalue>( jobject{{"errorCodes", j}} );
		}
		return _readyResult!=nullptr;
	}
	α HttpRequestAwait::ParseNodes()ε->tuple<flat_set<NodeId>,jarray>{
		auto& nodeJson = _request["nodes"];
		auto jNodes = Json::AsArray( Json::ParseValue(string{nodeJson}) );
		flat_set<NodeId> nodes;
		for( let& node : jNodes )
			nodes.emplace( Json::AsObject(node) );
		if( nodes.empty() )
			throw RestException{ EHttpStatus::BadRequest, SRCE_CUR, move(_request), "empty nodes" };
		return make_tuple( nodes, move(jNodes) );
	}

	α HttpRequestAwait::ResumeSnapshots( flat_map<NodeId, Value>&& results, jarray&& j )ι->void{
		for( let& [nodeId, value] : results ){
			jobject node = nodeId.ToJson();
			node["value"] = value.ToJson();
			j.push_back( node );
		}
		Resume( {jobject{{"snapshots", j}}, move(_request)} );
	}


	α HttpRequestAwait::CoHandleRequest( [[maybe_unused]] ServerCnnctnNK&& opcId )ι->void{
		let& target = _request.Target();
		try{
			if( _request.IsGet() ){
				throw RestException{ EHttpStatus::NotFound, SRCE_CUR, move(_request), "Unknown get target '{}'", target };
			}
			else if( _request.IsPost() )
				throw RestException{ EHttpStatus::NotFound, SRCE_CUR, move(_request), "Post not supported for target '{}'", target };
			else
				throw RestException{ EHttpStatus::Forbidden, SRCE_CUR, move(_request), "Only get/post verb is supported for target '{}'", target };
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α HttpRequestAwait::Login( str endpoint )ι->TAwait<optional<Web::FromServer::SessionInfo>>::Task{
		try{
			let& body = _request.Body();
			auto domain = Json::FindString( body, "opc" );
			if( !domain )
				throw RestException{ EHttpStatus::BadRequest, SRCE_CUR, move(_request), "opc server not specified" };
			auto user = Json::FindString( body, "user" );
			if( !user )
				throw RestException{ EHttpStatus::BadRequest, SRCE_CUR, move(_request), "user not specified" };
			auto password = Json::AsString( body, "password" );
			_request.LogRead( Ƒ("(opc: {}, user: {})", *domain, *user) );
			THROW_IFX( !_request.SessionInfo, RestException(EHttpStatus::Unauthorized, SRCE_CUR, move(_request), "No session.") ); // no use case
			let sessionInfo = co_await PasswordAwait{ move(*user), move(password), move(*domain), endpoint, false, _request.SessionInfo->SessionId };
			if( sessionInfo ){
				_request.SessionInfo->SessionId = sessionInfo->session_id();
				_request.SessionInfo->IsInitialRequest = true;
			}
			Resume( move(_request) );
		}
		catch( RestException& e ){
			ResumeExp( move(e) );
		}
		catch( runtime_error& e ){
			ResumeExp( RestException{EHttpStatus::Unauthorized, move(e), move(_request)} );
		}
	}
	α HttpRequestAwait::Logout()ι->TAwait<jvalue>::Task{
		Gateway::Logout( _request.SessionId() );
		Sessions::Remove( _request.SessionId() );
		try{
			auto appClient = AppClient();
			co_await *( appClient->QLServer()->Query(Ƒ("purgeSession(id:\"{:x}\")", _request.SessionId()), {}, appClient->UserPK()) );
			Resume( move(_request) );
		}
		catch( runtime_error& e ){
			ResumeExp( RestException{EHttpStatus::InternalServerError, move(e), move(_request)} );
		}
	}

	α HttpRequestAwait::Suspend()ι->void{
 		if( _request.IsPost("/login") ) //used with user/password on Opc Server.
			Login( _request.UserEndpoint.address().to_string() );
		else if( _request.IsPost("/logout") )
			Logout();
		else{
			auto opc = _request["opc"];
			if( opc.size() )
				CoHandleRequest( move(opc) );
			else if( _request.Target().size() ){
				_request.LogRead();
				RestException e{ EHttpStatus::NotFound, SRCE_CUR, move(_request), "Unknown target '{}'", _request.Target() };
				ResumeExp( RestException{ EHttpStatus::NotFound, move(e), move(_request) } );
			}
		}
	}

	α HttpRequestAwait::await_resume()ε->HttpTaskResult{
		if( auto e = Promise() ? Promise()->MoveExp() : nullptr; e ){
			auto rest = dynamic_cast<RestException*>( e.get() );
			if( rest )
				rest->Throw();
			else{
				auto ua = dynamic_cast<UAClientException*>( e.get() );
				if( ua )
					ua->ThrowRest( move(*ua), move(_request) );
				else
					throw RestException{ EHttpStatus::InternalServerError, move(*e), move(_request) };
			}
		}
		return _readyResult
			? HttpTaskResult{ move(*_readyResult), move(_request) }
			: Promise()->Value() ? move( *Promise()->Value() ) : HttpTaskResult{ jobject{}, move(_request) };
	}
}