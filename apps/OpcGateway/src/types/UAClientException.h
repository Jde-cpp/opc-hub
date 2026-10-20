#pragma once
#include <jde/web/server/RestException.h>
#include <jde/opc/UAException.h>
#include "../usings.h"

#define UACε(f) if( let sc = f; sc ){ UAClient::RemoveIfDisconnected( sc, _client ); throw UAClientException{ sc, _client ? _client->Handle() : 0, _requestId, _sl, ELogLevel::Debug }; }
namespace Jde::Web::Server{ struct HttpRequest; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	struct UAClientException : UAException{
		UAClientException( StatusCode sc, Handle uaHandle, RequestId requestId=0, SRCE, ELogLevel level=ELogLevel::Debug )ι;
		UAClientException( StatusCode sc, Handle uaHandle, string description, SRCE )ι;
		UAClientException( UAClientException&& from )ι:UAException{ move(from) }, _handle{ from._handle }, _requestId{ from._requestId }, _userMessage{ move(from._userMessage) }{}
		UAClientException( const UAClientException& from )ι:UAException{ from }, _handle{ from._handle }, _requestId{ from._requestId }, _userMessage{ from._userMessage }{}

		α Move()ι->up<Exception> override{ return mu<UAClientException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
		α IsBadSession()Ι->bool{ return Code()==UA_STATUSCODE_BADSESSIONIDINVALID; }
		//status plus the throw-site reason; the description names an operation or a configuration mismatch, never internals.
		α UserMessage()Ι->const string& override;
		α ClientDetail()Ι->string override{ return UserMessage(); }//a 500 body otherwise carries the status alone, and a failed connect's reason is the fix.
		α what()const noexcept->const char* override;
		[[noreturn]] α ThrowRest( UAClientException&& e, Web::Server::HttpRequest&& request )ε->void;
	private:
		Handle _handle{};
		RequestId _requestId{};
		mutable string _userMessage;
	};

	Ξ UAClientException::ThrowRest( UAClientException&& e, Web::Server::HttpRequest&& request )ε->void{
		const auto status = e.HttpStatus();//the constructor's, from the status code.
		if( status==EHttpStatus::Forbidden )
			throw Web::Server::RestException{ status, move(e), move(request), "Bad identity token" };
		if( status==EHttpStatus::BadGateway )
			throw Web::Server::RestException{ status, move(e), move(request), "Opc Server not reachable" };
		throw Web::Server::RestException{ status, move(e), move(request) };
	}
}