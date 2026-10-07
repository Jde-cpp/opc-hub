#pragma once
#include "ClientHttpRes.h"
#include <jde/fwk/co/Await.h>
#include "../exports.h"
#include "../usings.h"

namespace Jde::Web::Client{
	struct HttpAwaitArgs {
		string Authorization;
		string Origin;//browsers set this themselves; here it is what lets a test exercise the server's cross-origin policy.
		string ContentType{ "application/x-www-form-urlencoded" };
		optional<http::verb> Verb{ http::verb::unknown };
		bool IsSsl{ true };
		bool AllowRedirects{ true };
		uint8 Redirects{ 5 };//hop budget: each redirect forwards a copy with one fewer, and running out is an error.
	};
	//One http request and the redirects that follow it, each hop on a connection of its own.  An error status throws ClientHttpResException.
	struct ΓWC ClientHttpAwait : TAwait<ClientHttpRes>{
		using base = TAwait<ClientHttpRes>;
		ClientHttpAwait( string host, string target, string body, PortType port=443, HttpAwaitArgs args={}, SRCE )ι;
		ClientHttpAwait( string host, string target, PortType port=443, HttpAwaitArgs args={}, SRCE )ι;
		α await_ready()ι->bool override{ return _ioContext==nullptr; }//executor down - await_resume says so.
		α Suspend()ι->void override;
		α await_resume()ε->ClientHttpRes override;
	private:
		string _host;
		string _target;
		string _body;
		PortType _port;
		HttpAwaitArgs _args;
		sp<net::io_context> _ioContext;
	};
}