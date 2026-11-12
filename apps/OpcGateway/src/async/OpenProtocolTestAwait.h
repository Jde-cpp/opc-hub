#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include <jde/opc/openProtocol/Session.h>
#include "../types/OpenProtocolRow.h"

namespace Jde::Opc::Gateway{
	//testConnection on an Open Protocol row (docs/open-protocol/spec.md *The row*):  a session to the row's controller that starts
	//communication, MID 0001 stepping down under the row's ceilings, stops it, MID 0003, and closes.  Resumes on the executor with
	//what MID 0002 answered, or with why the first attempt failed, the socket's error or MID 0004's among them:  BadGateway, and
	//GatewayTimeout where nothing came within the session's replyTimeout.  It builds no driver, no server and no client, and
	//registers nothing.  An AnyAwait, so any coroutine can co_await it.
	struct OpenProtocolTestAwait final : AnyAwait<OpenProtocol::Session::Connection>{
		using base = AnyAwait<OpenProtocol::Session::Connection>;
		OpenProtocolTestAwait( OpenProtocolRow row, SRCE )ι:base{sl}, _row{move(row)}{}
	protected:
		α Suspend()ι->void override;
	private:
		using Outcome = std::expected<OpenProtocol::Session::Connection,OpenProtocol::Failure>;
		α Finish( Outcome&& outcome )ι->void;//on the session's strand:  the first outcome is kept, and the session stopped.
		OpenProtocolRow _row;
		optional<OpenProtocol::Session> _session;
		optional<Outcome> _outcome;
	};
}