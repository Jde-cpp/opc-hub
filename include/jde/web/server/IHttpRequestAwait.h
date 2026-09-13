#pragma once
#include <jde/ql/types/RequestQL.h>
#include "usings.h"
#include "HttpRequest.h"
#include "RestException.h"

namespace Jde::QL{ struct IQL; }
namespace Jde::Web::Server{
	struct ΓWS HttpTaskResult{
		HttpTaskResult()=default;
		HttpTaskResult( HttpRequest&& req )ι:Request{move(req)}{}
		HttpTaskResult( HttpTaskResult&& rhs )ι;
		HttpTaskResult( jvalue&& j, HttpRequest&& req, SRCE )ι:Json( move(j) ), Request{ move(req) }, Source{sl}{}
		HttpTaskResult( string&& body, string contentType, HttpRequest&& req, SRCE )ι:Request{ move(req) }, Source{sl}, Body{ move(body) }, ContentType{ move(contentType) }{}//a file, not json - the site's (StaticSite).
		α operator=( HttpTaskResult&& rhs)ι->HttpTaskResult&;

		jvalue Json;
		optional<HttpRequest> Request; //why optional?
		optional<SL> Source;
		optional<string> Body;//set: sent as is with ContentType, Json ignored - a file of the site; the request's ResponseHeaders carry the rest (Cache-Control).
		string ContentType;
	};

	struct IHttpRequestAwait : TAwait<HttpTaskResult> {
		using base = TAwait<HttpTaskResult>;
		IHttpRequestAwait( HttpRequest&& req, SRCE )ι:base{sl},_request{ move(req) }{}
		virtual ~IHttpRequestAwait()=0;
		α Request()ι->HttpRequest&{ return _request; }//the await owns the request once HandleRequest takes it; error funnels need it back to build the response.  moved-from if the implementation forwarded it on (into the result or a RestException).
	protected:
		HttpRequest _request;
		up<jvalue> _readyResult;
		α Query( sp<QL::IQL> ql )ι->TAwait<jvalue>::Task;
	private:
		β Schemas()Ι->const vector<sp<DB::AppSchema>>& = 0;
	};
	inline IHttpRequestAwait::~IHttpRequestAwait(){}
}