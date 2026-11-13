#pragma once
#include <jde/opc/usings.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/openProtocol/Session.h>
#include "../IDriver.h"

namespace Jde::Opc::Gateway{
	//The Open Protocol driver (docs/open-protocol/spec.md *The driver server*, *Address space*, *Results*):  a row's session
	//with its controller on the row's strand of the executor, its settings the /gateway/drivers/openProtocol block's and the zone
	//and the ceilings of the row's `driver`, and, in the row's namespace, `Result`, `Counters` and `_Op`:  each variable a stored
	//value the strand posts to the server's own thread as the session reports.
	//`Result` is a variable per field of MID 0061 (TighteningResult.h), subscribed to by MID 0060 at each connect.  Each reads
	//Bad_WaitingForInitialData until the first result, and Bad_NotSupported where the revision the results come at does not carry
	//it, set when the subscription is accepted and by a result of a lower revision.  A result is one post:  a write per field,
	//TighteningId last, then the counters that moved, then `_Op.Results`.  Every write of one result has one source
	//timestamp, the controller's time read in the row's zone, a millisecond past the last result's where it is not after it, so
	//the row's are distinct and increasing, and the gateway's receipt where the result has no time.  `Counters` are Total, Ok and
	//Nok since the driver started:  every result below revision 3, and from 3 a result of type 1, a tightening, or of no type,
	//Ok and Nok by the tightening status.  Both keep their last value and status through a loss of the link.
	//`_Op` is the diagnostics:  Connected and Since follow the link, LastError each failed attempt and each MID 0004, Reconnects
	//the links that came back after a loss, as the endpoint's `_Connection` counts them;  Identity, LinkLevel and Revisions are the
	//last connection's, kept through a loss;  Skew is the controller's clock less the gateway's at each connect, MID 0080's, with a
	//warning past maxSkew, Bad_WaitingForInitialData before the first and Bad_NotSupported where the controller gives none;
	//Results counts the results written, and LostResults stays 0 until their recovery is (plan Phase 6).  A controller whose
	//MID 0002 names a system whose results are not MID 0061, an MT Focus 6000 or a PowerMACS, is refused:  the session stops for
	//good, with the reason in LastError and the log (spec *Controllers*).
	struct OpenProtocolDriver final : IDriver{
		OpenProtocolDriver( const ServerCnnctn& row, SRCE )ε;//the row's own refusal, OpenProtocolRow's, for one written past the save's rules.
		Ω Make( const ServerCnnctn& row, SL sl )ε->up<IDriver>{ return mu<OpenProtocolDriver>( row, sl ); }
		α Configure( Opc::Server::UASettings& settings )ε->void override;//maxAsyncOperationQueueSize, the block's maxAsyncOperations.
		α Build( UA_Server& server, UA_UInt16 ns )ε->void override;
		α Start()ι->void override;
		α Stop( absl::AnyInvocable<void()> stopped )ι->void override;//MID 0003 where the link is up, then the close.
	private:
		using Writes = vector<std::pair<string,Value>>;//a node's path, `Result.Torque`, and what it is to hold.
		//These on the strand.
		α OnConnected( OpenProtocol::Session::Connection&& connection )ι->void;
		α OnLost( const OpenProtocol::Failure& why )ι->void;
		α OnTime( OpenProtocol::Session::Reply&& reply )ι->void;//MID 0081, or why there is none.
		α OnSubscribed( OpenProtocol::Session::Reply&& reply )ι->void;//MID 0060's answer.
		α OnResult( uint16_t revision, const OpenProtocol::Mid0061& result )ι->void;
		//Bad_NotSupported for each of `Result`'s variables `revision` does not carry, where it does not read so already.
		α SetRevision( uint16_t revision, UA_DateTime at, Writes& writes )ι->void;
		α Post( Writes&& writes )ι->void;//in order, in one call on the server's thread.
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
		UA_Server* _server{};//Build's:  posted to until Stop's `stopped`, which the loop outlives (UAServer::Post).
		UA_UInt16 _ns{};
		bool _connected{};
		bool _wasConnected{};//a connection has stood:  the next is a reconnect.
		UA_UInt32 _reconnects{};
		uint16_t _resultRevision{};//MID 0061's that `Result`'s Bad_NotSupported variables stand at:  0 before the first subscription.
		uint16_t _shownRevision{};//MID 0061's as `_Op.Revisions` last showed it.
		optional<TimePoint> _lastResult;//the last result's source timestamp, or the last NotSupported marks'.
		bool _clockBack{};//results are taking the last's time after a clock went back past maxSkew:  warned once.
		UA_UInt64 _results{}, _total{}, _ok{}, _nok{};
	};
}