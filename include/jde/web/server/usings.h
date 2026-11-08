#pragma once
#include <boost/unordered/concurrent_flat_map.hpp>
#include "../usings.h"

namespace Jde::Web::Server{
	using SocketId = uint32;
	using io_executor = net::io_context::executor_type;
	using executor_type = net::strand<io_executor>;//each session and listener has its own:  Stop's cancels are posted to it.
	using executor_with_default = net::as_tuple_t<net::use_awaitable_t<executor_type>>::executor_with_default<executor_type>;

	using TBody=http::string_body;
	using TAllocator=std::allocator<char>;
	using TRequestType = http::request<TBody, http::basic_fields<TAllocator>>;

	using StreamType = typename beast::tcp_stream::rebind_executor<executor_with_default>::other;
}