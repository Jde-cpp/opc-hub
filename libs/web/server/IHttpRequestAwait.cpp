#include <jde/ql/ql.h>
#include <jde/ql/QLAwait.h>
#include <jde/web/server/IHttpRequestAwait.h>

namespace Jde::Web::Server{
	HttpTaskResult::HttpTaskResult( HttpTaskResult&& rhs )ι:
		Json( move(rhs.Json) ),
		Request{ move(rhs.Request) },
		Source{ move(rhs.Source) },
		Body{ move(rhs.Body) },
		ContentType{ move(rhs.ContentType) }
	{}

	α HttpTaskResult::operator=( HttpTaskResult&& rhs )ι->HttpTaskResult&{
		if( rhs.Request )
			Request.emplace( move(*rhs.Request) );
		else
			Request.reset();
		Json=move( rhs.Json );
		Source=move( rhs.Source );
		Body=move( rhs.Body );
		ContentType=move( rhs.ContentType );
		return *this;
	}

	α IHttpRequestAwait::Query( sp<QL::IQL> ql )ι->TAwait<jvalue>::Task{
		try{
			string query = _request.IsGet() ? _request["query"] : Json::AsString(_request.Body(), "query");
			THROW_IFX( query.empty(), RestException(EHttpStatus::BadRequest,SRCE_CUR, move(_request), "no query") );
			jobject vars;
			if( auto variableString = _request.IsGet() ? _request["variables"] : string{}; variableString.size() )
				vars = Json::Parse( variableString );
			else if( auto p = _request.IsPost() ? _request.Body().if_contains("variables") : nullptr; p && p->is_object() )
				vars = move( p->get_object() );
			if( query.starts_with("{") )
				query = Str::TrimFirstLast( move(query), '{', '}' );
			_request.LogRead( query );
			auto q = QL::Parse( move(query), move(vars), Schemas(), _request.Params().contains("raw") );
			THROW_IFX( q.IsMutation() && !_request.IsPost(), RestException(EHttpStatus::BadRequest, SRCE_CUR, move(_request), "Mutations must use post.") );
			auto y = co_await QL::QLAwait<>{ move(q), {_request.SessionInfo}, move(ql), _sl };
			Resume( HttpTaskResult{jobject{{"data", move(y)}}, move(_request)} );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}