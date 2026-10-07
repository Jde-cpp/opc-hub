#pragma once
#include "../usings.h"
#include <boost/beast/ssl/ssl_stream.hpp>

namespace Jde::Web::Client{
	using SslSocketStream =	websocket::stream<beast::ssl_stream<beast::tcp_stream>>;
	α UserAgent( bool ssl )ι->str;//the User-Agent header: product and beast versions, SSL noted - the client's twin of Server::ServerVersion.
}