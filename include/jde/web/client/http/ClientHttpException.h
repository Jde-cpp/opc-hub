#pragma once
#include "../usings.h"
#include "../exports.h"

namespace Jde::Web::Client{
	struct ΓWC ClientHttpException : CodeException{
		ClientHttpException( beast::error_code ec, ELogLevel level=ELogLevel::Debug, SRCE )ι;
		ClientHttpException( beast::error_code ec, str host, PortType port={}, ELogLevel level=ELogLevel::Debug, SRCE )ι;
		ClientHttpException( beast::error_code ec, str host, str target, PortType port, ELogLevel level=ELogLevel::Debug, SRCE )ι;
		ClientHttpException( ClientHttpException&& e )ι:CodeException{ move(e) }, Host{ e.Host }, Target{ e.Target }, Port{ e.Port }{}
		ClientHttpException( const ClientHttpException& e )ι=default;

		α SslStreamTruncated()ι{ return _errorCode.category()==ssl::error::get_stream_category() && _errorCode.value()==ssl::error::stream_truncated; }
		α Move()ι->up<Exception> override{ return mu<ClientHttpException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
		const string Host;
		const string Target;
		const PortType Port{};
	};
}