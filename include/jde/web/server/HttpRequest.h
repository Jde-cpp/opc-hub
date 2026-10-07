#pragma once
#include <jde/db/usings.h>
#include <jde/fwk/io/json.h>
#include "Sessions.h"
#include "usings.h"

namespace Jde::Web::Server{
	α AccessControlAllowOrigin()ι->str;
	//the allowOrigin setting value that selects same-host-any-port reflection instead of a literal header.
	constexpr sv SameHostOrigin{ "sameHost" };
	α ServerVersion( bool isSsl )ι->str;

	struct RestException;
	struct ΓWS HttpRequest final{
		HttpRequest( TRequestType&& request, tcp::endpoint userEndpoint, bool isSsl, uint32 connectionId )ι;
		HttpRequest( const HttpRequest& ) = delete;
		HttpRequest( HttpRequest&& ) = default;
		α operator=( const HttpRequest& ) = delete;
		α operator=( HttpRequest&& )->HttpRequest& = default;
		α operator[]( str x )Ι->const string&;

		α Body()ε->jobject&;//parsed on first call, from the raw body moved out - which is why there is no string accessor.
		α Body()Ε->const jobject&{ return const_cast<HttpRequest*>(this)->Body(); }
		α GraphQLQuery()ε->std::pair<string,jobject>;//{query, variables} - from the params of a GET, the json body otherwise.
		α Contains( str param )Ι->bool{ return _params.contains( param ); }
		α Header( sv header )Ι->string{ return _request.base()[header]; }
		α IsGet()Ι->bool{ return _request.method() == http::verb::get; }
		α IsGet( str target)Ι->bool{ return _request.method() == http::verb::get && Target()==target; }
		α IsPost()Ι->bool{ return _request.method() == http::verb::post; }
		α IsPost( str target)Ι->bool{ return _request.method() == http::verb::post && Target()==target; }
		α Method()Ι->http::verb{ return _request.method(); }
		α Params()Ι->const flat_map<string,string>&{ return _params; }
		α SessionId()Ι->SessionPK;
		α Target()Ι->const string&{ return _target; }
		α UserPK()Ι->UserPK{ return SessionInfo ? SessionInfo->UserPK : Jde::UserPK{}; }//no session is anonymous, so a gate on this fails closed.

		//this request's Access-Control-Allow-Origin value - empty to omit the header - and whether the answer depended on the
		//request's Origin, which is what obliges us to send Vary.
		α AllowOrigin()Ι->tuple<string,bool>;
		ψ BadRequest( SL sl, fmt::format_string<Args...> format, Args&&... args )Ι->http::response<http::string_body>;
		α LogRead( str text="", ELogLevel level=ELogLevel::Debug, SRCE )Ι->void;
		template<class T=http::string_body>
		α Response( http::status status=http::status::ok )Ι->http::response<T>;
		α Response( jvalue j, SRCE )Ι->http::response<http::string_body>;
		α Response( string&& body, sv contentType, SRCE )Ι->http::response<http::string_body>;//a file of the site, sent as is.
		sp<Server::SessionInfo> SessionInfo;
		tcp::endpoint UserEndpoint;//read-only in use; not const, so the request can be move-assigned (HttpTaskResult).
		flat_map<string,string> ResponseHeaders;

	private:
		α ParseUri()->void;

		mutable optional<jobject> _body;
		uint32 _connectionId;
		uint32 _index;
		bool _isSsl;
		flat_map<string,string> _params;
		TRequestType _request;
		steady_clock::time_point _start;
		string _target;

		friend RestException;
	};

	template<class T>
	α HttpRequest::Response( http::status status )Ι->http::response<T>{
		http::response<T> res{ status, _request.version() };
		res.set( http::field::server, ServerVersion(_isSsl) );
		auto [allowOrigin, varyOrigin] = AllowOrigin();
		if( allowOrigin.size() )
			res.set( http::field::access_control_allow_origin, allowOrigin );
		if( varyOrigin )//a cache must not hand one origin's Allow-Origin to another; true even when we refused, since the refusal is origin-specific too.
			res.set( http::field::vary, "Origin" );
		if( SessionInfo && SessionInfo->IsInitialRequest ){
			res.set( http::field::access_control_expose_headers, "Authorization" );
			res.set( http::field::authorization, Jde::format("{:x}", SessionInfo->SessionId) );
		}
		for( auto&& [key,value] : ResponseHeaders )
			res.set( key, value );
		res.keep_alive( _request.keep_alive() );
		return res;
	}

	template<class... Args>
	α HttpRequest::BadRequest( SL, fmt::format_string<Args...> format, Args&&... args )Ι->http::response<http::string_body>{
		auto res = Response<http::string_body>( http::status::bad_request );
		res.body() = Jde::format( format, args... );
		res.prepare_payload();
		return res;
	}
}