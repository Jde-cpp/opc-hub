#pragma once
#include <jde/ql/types/RequestQL.h>
#include "usings.h"
#include "HttpRequest.h"
#include "RestException.h"

namespace Jde::Web::Server{
	struct HttpTaskResult{
		HttpTaskResult()=default;
		HttpTaskResult( HttpRequest&& req )ι:Request{move(req)}{}
		HttpTaskResult( HttpTaskResult&& )ι=default;
		HttpTaskResult( jvalue&& j, HttpRequest&& req, SRCE )ι:Json( move(j) ), Request{ move(req) }, Source{sl}{}
		HttpTaskResult( string&& body, string contentType, HttpRequest&& req, SRCE )ι:Request{ move(req) }, Source{sl}, Body{ move(body) }, ContentType{ move(contentType) }{}//a file, not json - the site's (StaticSite).
		α operator=( HttpTaskResult&& )ι->HttpTaskResult& = default;

		jvalue Json;
		optional<HttpRequest> Request;//empty from the default constructor - ServerImpl answers that through the error funnel, which builds the response from the await's own request (the mock's /NoResult, web-review3 #3).
		optional<SL> Source;
		optional<string> Body;//set: sent as is with ContentType, Json ignored - a file of the site; the request's ResponseHeaders carry the rest (Cache-Control).
		string ContentType;
	};

	//As is, a server with no routes of its own:  every request suspends and answers 404.
	struct IHttpRequestAwait : TAwait<HttpTaskResult> {
		using base = TAwait<HttpTaskResult>;
		IHttpRequestAwait( HttpRequest&& req, SRCE )ι:base{sl},_request{ move(req) }{}
		α Suspend()ι->void override;//404 "Unknown target".
		α await_resume()ε->HttpTaskResult override;//a RestException rethrown, any other wrapped with its HttpStatus;  else the ready result, the promise's value, or an empty one.
		α Request()ι->HttpRequest&{ return _request; }//the await owns the request once HandleRequest takes it; error funnels need it back to build the response.  moved-from if the implementation forwarded it on (into the result or a RestException).
	protected:
		HttpRequest _request;
		up<jvalue> _readyResult;
	};
}