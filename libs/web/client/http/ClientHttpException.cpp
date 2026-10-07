#include <jde/web/client/http/ClientHttpException.h>

namespace Jde::Web::Client{
	ClientHttpException::ClientHttpException( beast::error_code ec, ELogLevel level, SL sl )ι:
		ClientHttpException{ ec, {}, {}, level, sl }
	{}

	ClientHttpException::ClientHttpException( beast::error_code ec, str host, PortType port, ELogLevel level, SL sl )ι:
		CodeException{ static_cast<std::error_code>(ec), ELogTags::HttpClientWrite, level, sl },
		Host{ host },
		Port{ port }
	{}

	ClientHttpException::ClientHttpException( beast::error_code ec, str host, str target, PortType port, ELogLevel level, SL sl )ι:
		CodeException{ static_cast<std::error_code>(ec), ELogTags::HttpClientWrite, level, sl },
		Host{ host },
		Target{ target },
		Port{ port }
	{}
}