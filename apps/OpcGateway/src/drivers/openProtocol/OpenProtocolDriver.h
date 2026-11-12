#pragma once
#include <jde/opc/usings.h>
#include <jde/opc/openProtocol/Session.h>
#include "../IDriver.h"

namespace Jde::Opc::Gateway{
	//The Open Protocol driver (docs/open-protocol/spec.md *The driver server*, *Address space*), its skeleton:  a row's session
	//with its controller on the row's strand of the executor, its settings the /gateway/drivers/openProtocol block's and the zone
	//and the ceilings of the row's `driver`, and `_Op`, the diagnostics, in the row's namespace:  eleven variables, each a stored
	//value written from the strand as the session reports.  Connected and Since follow the link, LastError each failed attempt and
	//each MID 0004, Reconnects the links that came back after a loss, as the endpoint's `_Connection` counts them;  Identity,
	//LinkLevel and Revisions are the last connection's, kept through a loss;  Skew is the controller's clock less the gateway's
	//at each connect, MID 0080's, with a warning past maxSkew, Bad_WaitingForInitialData before the first and Bad_NotSupported
	//where the controller gives none;  Results
	//and LostResults stay 0 until the results and their recovery are written (plan Phases 5 and 6).  A controller whose MID 0002
	//names a system whose results are not MID 0061, an MT Focus 6000 or a PowerMACS, is refused:  the session stops for good,
	//with the reason in LastError and the log (spec *Controllers*).
	struct OpenProtocolDriver final : IDriver{
		OpenProtocolDriver( const ServerCnnctn& row, SRCE )ε;//the row's own refusal, OpenProtocolRow's, for one written past the save's rules.
		Ω Make( const ServerCnnctn& row, SL sl )ε->up<IDriver>{ return mu<OpenProtocolDriver>( row, sl ); }
		//MID 0002's fields under the names testConnection answers, each null where the revision it came at carries none.
		Ω Identity( const OpenProtocol::Mid0002& id )ι->jobject;
		α Configure( Opc::Server::UASettings& settings )ε->void override;//maxAsyncOperationQueueSize, the block's maxAsyncOperations.
		α Build( UA_Server& server, UA_UInt16 ns )ε->void override;
		α Start()ι->void override;
		α Stop( absl::AnyInvocable<void()> stopped )ι->void override;//MID 0003 where the link is up, then the close.
	private:
		//These on the strand.
		α OnConnected( OpenProtocol::Session::Connection&& connection )ι->void;
		α OnLost( const OpenProtocol::Failure& why )ι->void;
		α OnTime( OpenProtocol::Session::Reply&& reply )ι->void;//MID 0081, or why there is none.
		α Write( sv name, const void* scalar, const UA_DataType& type, UA_StatusCode status=UA_STATUSCODE_GOOD )ι->void;//`_Op`'s `name`.
		α WriteText( sv name, sv text )ι->void;
		α WriteNoSkew( UA_StatusCode why )ι->void;
		α WriteConnected( bool connected )ι->void;//Connected, and Since now.
		α WriteRevisions()ι->void;
		const ServerCnnctnNK _slug;
		const string _url;
		const std::chrono::time_zone* _zone{};//the row's, else the gateway's own.
		Duration _maxSkew;
		optional<uint> _maxAsyncOperations;
		optional<OpenProtocol::Session> _session;
		UA_Server* _server{};//Build's, which outlives Stop's `stopped`.
		UA_UInt16 _ns{};
		bool _connected{};
		bool _wasConnected{};//a connection has stood:  the next is a reconnect.
		UA_UInt32 _reconnects{};
	};
}