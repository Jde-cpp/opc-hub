#include "HttpRequestAwait.h"
#include "ServerMock.h"//Mock::Port, for the redirect Location.
#include <jde/web/Jwt.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/str.h>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/process/thread.h>

#define let const auto

namespace Jde::Web::Mock{
	HttpRequestAwait::HttpRequestAwait( HttpRequest&& req, SL sl )ι:
		base{ move(req), sl }
	{}
	α CertificateLogin( const HttpRequest& req )ι->jobject{
		try{
			req.LogRead();
			Web::Jwt jwt{ Json::AsString(req.Body(), "jwt") };
			if( std::abs(time(nullptr)-jwt.Iat)>60*10 )
				THROW( "Invalid iat.  Expected ~'{}', found '{}'.", time(nullptr), jwt.Iat );

			Crypto::Verify( jwt.PublicKey, jwt.HeaderBodyEncoded, jwt.Signature );
			req.SessionInfo->UserPK = {42};
			jobject j{ {"expiration", ToIsoString(req.SessionInfo->Expiration)} };
			req.SessionInfo->IsInitialRequest = true;  //expecting sessionId to be set.
			return j;
		}
		catch( const runtime_error& ){
			ASSERT(false);
			return {};
		}
	}

	α EchoResult( const flat_map<string,string>& params )ι->jobject{
		jarray echo;
		for( let& [key,value] : params ){
			if( value.empty() )
				echo.emplace_back( key );
			else{
				jobject j;
				j[key] = value;
				echo.emplace_back(j);
			}
		}
		return jobject{ {"params", echo} };
	}

	α HttpRequestAwait::await_ready()ι->bool{
		optional<jobject> result;
		if( _request.IsGet("/echo") )
			result = EchoResult( _request.Params() );
		else if( _request.IsGet("/Authorization") ){
			_request.ResponseHeaders.emplace( "Authorization", Jde::format("{:x}", _request.SessionInfo->SessionId) );
			result = jobject{};
		}
		else if( _request.IsGet("/authHeader") )//echoes the credential as received, so a test can see whether a redirect carried it.
			result = jobject{ {"authorization", _request.Header("authorization")} };
		else if( _request.IsGet("/timeout") ){
			jobject j;
			let expiration = Chrono::ToClock<Clock,steady_clock>( _request.SessionInfo->Expiration );
			j["value"] = ToIsoString( expiration );
			result = j;
		}
		else if( _request.IsPost("/login") ){
			result = CertificateLogin( _request );
		}
		if( result ){
			_result = HttpTaskResult{ move(*result), move(_request) };
		}
		else if( _request.IsGet("/NoResult") )//#3: a result with no request drops ServerImpl into catch( const runtime_error& ).  leave _request with the await - that is what the error response has to be built from.
			_result = HttpTaskResult{};
		return _result.has_value();
	}
	α HttpRequestAwait::Suspend()ι->void{
		if( _request.Target()=="/delay" ){
			_thread = std::jthread( [this,h=_h]()mutable->void {
				Thread::SetName( "DelayHandler" );
				uint seconds = To<uint>( _request["seconds"] );
				DBGT( ELogTags::HttpServerWrite, "server sleeping for {}", seconds );
				std::this_thread::sleep_for( std::chrono::seconds{seconds} );
				Promise()->SetValue( {jobject{}, move(_request)} );
				net::post( *Executor(), [h](){ h.resume(); } );
				DBGT( ELogTags::HttpServerWrite, "~/delay handler" );
			});
		}
		else if( _request.Target()=="/BadAwaitable" ){
			_thread = std::jthread( [this,h=_h]()mutable->void {
				Thread::SetName( "BadAwaitable" );
				h.promise().SetExp( RestException{ EHttpStatus::InternalServerError, SRCE_CUR, move(_request), "BadAwaitable"} );//a handler that just fails is a server fault - the status RestException carried before it took one explicitly.
				net::post( *Executor(), [h](){ h.resume(); } );
				DBGT( ELogTags::HttpServerWrite, "~/BadAwaitable handler" );
			});
		}
		else if( _request.Target()=="/redirectLoop" || _request.Target()=="/redirectHost" || _request.Target()=="/redirectBadPort" || _request.Target()=="/redirectPlain" ){
			//302 straight back at the client: /redirectLoop points at itself so the hop budget is the only thing that stops it,
			///redirectHost sends it to the same server under a different host name so the Authorization drop can be observed.
			//L3: /redirectBadPort's port is all digits but far past unsigned long, so RedirectVariables' parse fails - the client
			//must fail the request rather than strand it.
			//L4: /redirectPlain names http:// on the same server (it answers both schemes on one port), so the scheme is the only
			//thing that changes - the client must follow it rather than retry over its original transport.
			let location = _request.Target()=="/redirectLoop" ? string{"/redirectLoop"}
				: _request.Target()=="/redirectBadPort" ? string{"https://127.0.0.1:99999999999999999999/authHeader"}
				: _request.Target()=="/redirectPlain" ? Ƒ( "http://127.0.0.1:{}/authHeader", Port )
				: Ƒ( "https://127.0.0.1:{}/authHeader", Port );
			_request.ResponseHeaders.emplace( "Location", location );//set before the move - RestException::Response() emits these.
			ResumeExp( RestException(EHttpStatus::Found, SRCE_CUR, move(_request), "redirecting") );
		}
		else{
			let target = _request.Target();//read before the move: argument evaluation is indeterminately sequenced, so Target() could otherwise run against a moved-from request.
			ResumeExp( RestException(EHttpStatus::NotFound, SRCE_CUR, move(_request), "Unknown target '{}'", target) );
		}
	}
	α HttpRequestAwait::await_resume()ε->HttpTaskResult{
		ASSERT( Promise() || _result );
		base::CheckException();
		return Promise() ? move(*Promise()->Value()) : move(*_result);
	}
}