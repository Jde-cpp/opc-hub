#pragma once
#include <boost/beast.hpp>

namespace Jde{
	using RequestId = uint32;
	namespace net = boost::asio;
	namespace beast = boost::beast;
	namespace http = beast::http;
	namespace websocket = beast::websocket;
	namespace ssl = net::ssl;
	using tcp = net::ip::tcp;
}
namespace Jde::Web{
	α MaxLogLength()ι->uint16;//how much of a request, response or query a log line carries - /http/maxLogLength, for both sides.
	//Where a beast error_code arrived.  The same code is routine in one place and a fault in another, so the level is looked up per role.
	enum class EErrorRole : uint8{ HttpServerRead, HttpServerWrite, SocketServerRead, SocketClient, HttpClientShutdown };
	α ErrorSeverity( beast::error_code ec, EErrorRole role )ι->ELogLevel;//NoLog for a code that is just how a connection ends in that role.
}
