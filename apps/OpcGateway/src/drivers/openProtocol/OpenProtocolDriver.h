#pragma once
#include <jde/opc/usings.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/openProtocol/Session.h>
#include "../IDriver.h"

namespace Jde::Opc::Gateway{
	//The Open Protocol driver (docs/open-protocol/spec.md *The driver server*, *Address space*, *Results*, *Backfill*):  a row's
	//session with its controller on the row's strand of the executor, its settings the /gateway/drivers/openProtocol block's and
	//the zone and the ceilings of the row's `driver`, and, in the row's namespace, `Result`, `Counters` and `_Op`:  each variable a
	//stored value the strand posts to the server's own thread as the session reports.
	//`Result` is a variable per field of MID 0061 (TighteningResult.h), subscribed to by MID 0060 at each connect.  Each reads
	//Bad_WaitingForInitialData until the first result, and Bad_NotSupported where the revision the results come at, or the
	//result last written, does not carry it, set when the subscription is accepted and by a result that lacks it.  A result is
	//one post:  a write per field, TighteningId last, then the counters that moved, then `_Op.Results`.  Every write of one
	//result has one source timestamp, the controller's time read in the row's zone, a millisecond past the last result's where
	//it is not after it, so the row's are distinct and increasing, and the gateway's receipt where the result has no time.
	//`Counters` are Total, Ok and Nok since the driver started:  every result below revision 3, and from 3 a result of type 1, a
	//tightening, or of no type, Ok and Nok by the tightening status.  Both keep their last value and status through a loss of the
	//link.
	//A link that returns once a result has been written is filled:  the results pushed from the subscription on are held, and
	//after the clock's read MID 0064 walks back, from the oldest held id less one, or from id 0, the latest, where none is held,
	//then from each id it is answered less one.  The walk ends at the last id written, or at a lower id timed no later than the
	//last write, whichever station made it.  It ends short of that at a MID 0004, at maxBackfill requests, at id 1 and at a
	//reply with no id, which adds one to `_Op.LostResults` with a warning that names both ends.  A result of another station of
	//the controller, MID 0065 revision 7's station id against MID 0002 revision 6's, is dropped.  What was found is then written
	//oldest first and the held results after it, each id once, the pushed one where a result came both ways.  A found result is
	//a MID 0065, which carries no limits:  what it lacks of `Result` reads Bad_NotSupported from its time until the next pushed
	//result.  A link lost during the walk drops what was found and held, and the next connection walks again from the latest.
	//`_Op` is the diagnostics:  Connected and Since follow the link, LastError each failed attempt and each MID 0004, Reconnects
	//the links that came back after a loss, as the endpoint's `_Connection` counts them;  Identity, LinkLevel and Revisions are the
	//last connection's, kept through a loss;  Skew is the controller's clock less the gateway's at each connect, MID 0080's, with a
	//warning past maxSkew, Bad_WaitingForInitialData before the first and Bad_NotSupported where the controller gives none;
	//Results counts the results written, and LostResults the recoveries that ended short.  A controller whose MID 0002 names a
	//system whose results are not MID 0061, an MT Focus 6000 or a PowerMACS, is refused:  the session stops for good, with the
	//reason in LastError and the log (spec *Controllers*).
	struct OpenProtocolDriver final : IDriver{
		OpenProtocolDriver( const ServerCnnctn& row, SRCE )ε;//the row's own refusal, OpenProtocolRow's, for one written past the save's rules.
		Ω Make( const ServerCnnctn& row, SL sl )ε->up<IDriver>{ return mu<OpenProtocolDriver>( row, sl ); }
		//maxAsyncOperationQueueSize, the block's maxAsyncOperations, and an item's deepest queue, twice its maxBackfill:  a fill
		//is a burst inside one publishing interval, the walk's bound and as many held.
		α Configure( Opc::Server::UASettings& settings )ε->void override;
		α Build( UA_Server& server, UA_UInt16 ns )ε->void override;
		α Start()ι->void override;
		α Stop( absl::AnyInvocable<void()> stopped )ι->void override;//MID 0003 where the link is up, then the close.
	private:
		using Writes = vector<std::pair<string,Value>>;//a node's path, `Result.Torque`, and what it is to hold.
		struct Posts;
		//A result kept while a recovery runs, and what its time is read by (TighteningResult::ToSys):  a pushed one's receipt, a
		//found one's the time of the result the walk reached before it.
		template<class T> struct Kept{ T Result; uint16_t Revision; TimePoint Seen; };
		struct Recovery{
			vector<Kept<OpenProtocol::Mid0065>> Found;//the walk's, the station's own, newest first.
			vector<Kept<OpenProtocol::Mid0061>> Held;//pushed meanwhile, as they came.
			uint Requests{};//the MID 0064s sent.
			optional<uint32_t> Oldest;//the oldest id the walk reached, whichever station's.
			TimePoint By;//the time of the last result the walk reached, and before the first the controller's clock.
		};
		//These on the strand.
		α OnConnected( OpenProtocol::Session::Connection&& connection )ι->void;
		α OnLost( const OpenProtocol::Failure& why )ι->void;
		α OnTime( OpenProtocol::Session::Reply&& reply )ι->void;//MID 0081, or why there is none.
		α OnSubscribed( OpenProtocol::Session::Reply&& reply )ι->void;//MID 0060's answer.
		α OnResult( uint16_t revision, const OpenProtocol::Mid0061& result )ι->void;
		α Walk( uint32_t below )ι->void;//MID 0064 for the result before `below`, 0 for the latest, or the walk's end where none may be asked.
		α OnOld( OpenProtocol::Session::Reply&& reply )ι->void;//MID 0065, or why there is none.
		α EndWalk( sv shortBy={} )ι->void;//what was found and held is written;  `shortBy` is why the walk did not reach the last result written.
		//A result as one post, by the revision of its own message:  MID 0061's pushed, MID 0065's found.
		template<class T> α Record( const T& result, uint16_t revision, TimePoint seen )ι->void;
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
		uint _maxBackfill;//the MID 0064s one recovery may send.
		optional<OpenProtocol::Session> _session;
		UA_Server* _server{};//Build's:  posted to until Stop's `stopped`, which the loop outlives (UAServer::Post).
		UA_UInt16 _ns{};
		sp<Posts> _posts;//how long the posts held the server's thread:  said when the last has run.
		bool _connected{};
		bool _wasConnected{};//a connection has stood:  the next is a reconnect.
		UA_UInt32 _reconnects{};
		optional<uint32_t> _stationId;//the connection's, MID 0002 revision 6's:  a walk drops another station's results.
		uint16_t _resultRevision{};//MID 0061's as the last result came, or as the subscription was accepted below it:  0 before the first.
		uint16_t _shownRevision{};//MID 0061's as `_Op.Revisions` last showed it.
		vector<bool> _lacked;//TighteningResult::Variables' each:  it reads Bad_NotSupported for want of it in the revision, or in the last result.
		optional<TimePoint> _lastResult;//the last result's source timestamp, or the last NotSupported marks'.
		optional<uint32_t> _lastId;//the last result written:  where a recovery's walk ends.
		optional<Recovery> _recovery;//from a reconnect until its walk ends:  results pushed meanwhile are held.
		bool _clockBack{};//results are taking the last's time after a clock went back past maxSkew:  warned once.
		UA_UInt64 _results{}, _lost{}, _total{}, _ok{}, _nok{};
	};
}