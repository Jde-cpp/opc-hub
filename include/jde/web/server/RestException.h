#pragma once
#include "HttpRequest.h"
#include "jde/fwk.h"
#include "jde/fwk/exceptions/Exception.h"
#include "jde/web/server/Server.h"

namespace Jde::Web::Server{
	struct RestException : Exception{
		RestException( Exception&& copyFrom, HttpRequest&& req )ι:Exception{ move(copyFrom) },_request{move(req)}{}
		RestException( const RestException& rhs );
		RestException( RestException&& ) = default;

		template<class... Args> RestException( EHttpStatus status, SL sl, HttpRequest&& req, Logging::Format<Args...> fmt, Args&&... args )ι:
			Exception( sl, {status}, fmt, FWD(args)... ),
			_request{ move(req) }
		{}
		template<class... Args> RestException( EHttpStatus status, Exception&& inner, HttpRequest&& req, Logging::Format<Args...> fmt="", Args&&... args )ι:
			Exception{
				inner.Source(),
				{inner.Level(), inner.Tags, inner.Code(), status},
				move(inner),
				fmt,
				FWD(args)...
			},
			_request{ move(req) }
		{}
		α Response()Ι->http::response<http::string_body>;
		α Request()ι->HttpRequest&{ return _request; }
		α Move()ι->up<Exception> override{ return mu<RestException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	private:
		HttpRequest _request;
	};

	inline RestException::RestException( const RestException& rhs ):
		Exception{rhs},
		_request{TRequestType{rhs._request._request}, rhs._request.UserEndpoint, rhs._request._isSsl, rhs._request._connectionId }{
		ASSERT(false);
	}
	Ξ RestException::Response()Ι->http::response<http::string_body>{
		auto res = _request.Response<http::string_body>( (http::status)HttpStatus() );
		res.body() = what();
		//every funnel gets the wrapped exception's client-safe detail here, rather than each catch site composing it.
		if( auto p = dynamic_cast<const Exception*>(_inner.get()) )
			if( auto detail = p->ClientDetail(); detail.size() )
				res.body() += "  " + detail;
		res.prepare_payload();
		LOGSL( ELogLevel::Debug, _sl, ELogTags::HttpServerWrite, "[{}.{}.{}]HttpResponse:  {}({}){}",
			hex(_request.SessionInfo ? _request.SessionInfo->SessionId : 0),
			hex(_request._connectionId),
			hex(_request._index),
			_request.Target(),
			(uint16)HttpStatus(),
			res.body().substr(0, MaxLogLength())
		);
		return res;
	}
}