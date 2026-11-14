#pragma once
#include <jde/opc/usings.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/openProtocol/Session.h>
#include "../DriverCalls.h"
#include "../IDriver.h"

namespace Jde::Opc::Gateway{
	//The Open Protocol driver (docs/open-protocol/spec.md *The driver server*, *Address space*, *Results*, *Backfill*,
	//*Commands*, *Events*):  a row's session with its controller on the row's strand of the executor, its settings the
	///gateway/drivers/openProtocol block's and the zone and the ceilings of the row's `driver`, and, in the row's namespace,
	//`Result`, `Counters`, `Selected`, `Alarm` and `_Op`, each variable a stored value the strand posts to the server's own thread
	//as the session reports, `Commands`, whose methods the strand sends the controller, and `TighteningResultEventType`, the
	//type of the event each result raises.
	//`Result` is a variable per field of MID 0061 (TighteningResult.h), subscribed to by MID 0060 at each connect.  Each reads
	//Bad_WaitingForInitialData until the first result, and Bad_NotSupported where the revision the results come at, or the
	//result last written, does not carry it, set when the subscription is accepted and by a result that lacks it.  A result is
	//one post:  a write per field, TighteningId last, then the counters that moved, then `_Op.Results`.  Every write of one
	//result has one source timestamp, the controller's time read in the row's zone, a millisecond past the last result's where
	//it is not after it, so the row's are distinct and increasing, and the gateway's receipt where the result has no time.
	//`Counters` are Total, Ok and Nok since the driver started:  every result below revision 3, and from 3 a result of type 1, a
	//tightening, or of no type, Ok and Nok by the tightening status.  Both keep their last value and status through a loss of the
	//link.
	//Each result written is an event too, raised in its post behind its writes:  a TighteningResultEventType, BaseEventType's
	//subtype with an optional property per variable of `Result`, from `Result`, whose EventNotifier says so, and so from the
	//Server object.  Its fields are the values the result wrote, under their variables' names in the row's namespace, a field
	//the result has no value for left empty;  its Time the result's source timestamp, its Severity 100 for OK and 500 for
	//anything else, and its Message `OK` or `NOK` with the parameter set.
	//A link that returns once a result has been written is filled:  the results pushed from the subscription on are held, and
	//after the clock's read MID 0064 walks back, from the oldest held id less one, or from id 0, the latest, where none is held,
	//then from each id it is answered less one.  The walk ends at the last id written, or at a lower id timed no later than the
	//last write, whichever station made it.  It ends short of that at a MID 0004, at maxBackfill requests, at id 1 and at a
	//reply with no id, which adds one to `_Op.LostResults` with a warning that names both ends.  A result of another station of
	//the controller, MID 0065 revision 7's station id against MID 0002 revision 6's, is dropped.  What was found is then written
	//oldest first and the held results after it, each id once, the pushed one where a result came both ways.  A found result is
	//a MID 0065, which carries no limits:  what it lacks of `Result` reads Bad_NotSupported from its time until the next pushed
	//result.  A link lost during the walk drops what was found and held, and the next connection walks again from the latest.
	//`Selected` is what the controller has selected, the parameter set of MID 0015, the job of MID 0035 and the VIN of MID 0052
	//with revision 2's identifier parts beside it, and `Alarm` its alarm:  MID 0071's code and time with Active true, MID 0076's
	//status as the controller gives it, and MID 0074's acknowledgement of the alarm held, Active false, another code's leaving the
	//held one active.  Their four subscriptions are asked at each connect behind the results' and the clock's read, and a report
	//is one post, sourced at its receipt.  A variable reads Bad_WaitingForInitialData until its first report, and again once a
	//subscription refused at an earlier connect is accepted;  Bad_NotSupported where a report has no value for it or the
	//controller refuses its subscription;  and, the link lost, Bad_NoCommunication over the value it held Good, until its next
	//report.
	//`Commands` holds five methods, each the MID of its purpose (§4.5):  SelectPset( pset ) MID 0018, SelectJob( job ) MID 0038,
	//DisableTool() and EnableTool() MIDs 0042 and 0043, every tool of the connection, and DownloadVin( vin ) MID 0050.  The Call
	//service has asked the host for Execute before a method's callback runs (DriverHost.h).  The callback, on the server's thread
	//under the service lock, refuses Bad_OutOfRange a value no revision the row may ask carries, a set past 999, a job past 9999
	//or a VIN past 25 characters, as the codec measures them;  else it holds the call with the host, posts the request to the
	//strand and answers Good_CompletesAsynchronously.  The strand sends it behind whatever is outstanding and answers on the
	//server's thread:  Good on MID 0005;  Bad_InvalidState on MID 0004, with the code and its name in `_Op.LastError` and the
	//call's line;  Bad_Timeout where the controller is silent for replyTimeout, which is the link's loss too;  Bad_NoCommunication
	//while the link is down, or where it goes first;  Bad_OutOfRange where the revision the controller settled does not carry
	//the value, a job past 99 at MID 0038's revision 1, with nothing sent at it;  Bad_Shutdown where the driver stops first.  A
	//call open62541 cancels before the strand has it is not sent;  one cancelled after is sent, its answer logged and not set.
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
		//maxAsyncOperationQueueSize, the block's maxAsyncOperations;  how long a call holds, its callTimeout, past replyTimeout
		//and the wait behind what is outstanding;  and an item's deepest queue, twice its maxBackfill:  a fill is a burst inside
		//one publishing interval, the walk's bound and as many held.
		α Configure( Opc::Server::UASettings& settings )ε->void override;
		α Build( UA_Server& server, UA_UInt16 ns, sp<DriverCalls> calls )ε->void override;
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
		//A method of `Commands` called, open62541's callback on the server's thread under the service lock, and the request it
		//makes, held with the host and posted to the strand through the server's thread, or refused there.
		Ω OnCall( UA_Server* server, const UA_NodeId* sessionId, void* sessionContext, const UA_NodeId* methodId, void* methodContext, const UA_NodeId* objectId, void* objectContext, size_t inputSize, const UA_Variant* input, size_t outputSize, UA_Variant* output )ι->UA_StatusCode;
		α Command( OpenProtocol::Body&& request, string&& text, UserPK user, const UA_Variant* output )ι->UA_StatusCode;
		//These on the strand.
		α Send( DriverCalls::Call&& call, OpenProtocol::Body&& request )ι->void;//a held call's request, where open62541 has not cancelled it.
		α OnCommanded( DriverCalls::Call&& call, uint16_t mid, OpenProtocol::Session::Reply&& reply )ι->void;//MID 0005, or why there is none:  the call's answer.
		α OnConnected( OpenProtocol::Session::Connection&& connection )ι->void;
		α OnLost( const OpenProtocol::Failure& why )ι->void;
		α OnTime( OpenProtocol::Session::Reply&& reply )ι->void;//MID 0081, or why there is none.
		α OnSubscribed( OpenProtocol::Session::Reply&& reply )ι->void;//MID 0060's answer.
		α OnPushed( OpenProtocol::Message&& pushed )ι->void;//a subscription's message, which the session has acknowledged.
		α OnResult( uint16_t revision, const OpenProtocol::Mid0061& result )ι->void;
		α OnStateSubscribed( uint16_t mid, OpenProtocol::Session::Reply&& reply )ι->void;//MID 0014's, 0034's, 0051's or 0070's answer.
		//A report to `Selected` or `Alarm` as one post:  each variable its value, or Bad_NotSupported where the report has none
		//for it.  `named` is the MID `_Op.Revisions` has the report's subscription by, 0 for MID 0074, whose revisions are its own.
		α Report( uint16_t named, uint16_t revision, vector<std::pair<uint8_t,optional<Value>>>&& values )ι->void;
		α Walk( uint32_t below )ι->void;//MID 0064 for the result before `below`, 0 for the latest, or the walk's end where none may be asked.
		α OnOld( OpenProtocol::Session::Reply&& reply )ι->void;//MID 0065, or why there is none.
		α EndWalk( sv shortBy={} )ι->void;//what was found and held is written;  `shortBy` is why the walk did not reach the last result written.
		//A result as one post, by the revision of its own message:  MID 0061's pushed, MID 0065's found.
		template<class T> α Record( const T& result, uint16_t revision, TimePoint seen )ι->void;
		//Bad_NotSupported for each of `Result`'s variables `revision` does not carry, where it does not read so already.
		α SetRevision( uint16_t revision, UA_DateTime at, Writes& writes )ι->void;
		//A result's event, raised once its post has written:  for each of TighteningResult::Variables, which of the post's
		//writes holds its value, the event's field of its name, and none where the result has no value for it.
		struct Event{ UA_DateTime Time; UA_UInt16 Severity; string Message; vector<optional<uint8_t>> Fields; };
		α Post( Writes&& writes, optional<Event>&& event={} )ι->void;//in order, then the event, in one call on the server's thread.
		α Write( sv name, const void* scalar, const UA_DataType& type, UA_StatusCode status=UA_STATUSCODE_GOOD )ι->void;//`_Op`'s `name`.
		α WriteText( sv name, sv text )ι->void;
		α WriteNoSkew( UA_StatusCode why )ι->void;
		α WriteConnected( bool connected )ι->void;//Connected, and Since now.
		α WriteRevisions()ι->void;
		α ShowRevision( uint16_t named, uint16_t revision )ι->void;//`_Op.Revisions` again, where it does not have `named` at `revision`.
		const ServerCnnctnNK _slug;
		const string _url;
		const std::chrono::time_zone* _zone{};//the row's, else the gateway's own.
		Duration _maxSkew;
		Duration _callTimeout;
		optional<uint> _maxAsyncOperations;
		uint _maxBackfill;//the MID 0064s one recovery may send.
		flat_map<uint16_t,uint16_t> _ceilings;//the row's, by MID:  a command's value is measured at the highest revision it may go at.
		const OpenProtocol::Strand _strand;//the session's.
		optional<OpenProtocol::Session> _session;
		UA_Server* _server{};//Build's:  posted to until Stop's `stopped`, which the loop outlives (UAServer::Post).
		UA_UInt16 _ns{};
		sp<DriverCalls> _calls;//Build's, the host's.
		std::atomic<bool> _stopping{};//Stop has been called:  a method answers Bad_Shutdown.
		//Set on the strand as the session's stop ends, ahead of `stopped`:  what the strand runs after it, a call posted from the
		//server's thread meanwhile, touches nothing of the driver's, which may have gone.
		const sp<bool> _stopped{ ms<bool>() };
		sp<Posts> _posts;//how long the posts held the server's thread:  said when the last has run.
		bool _connected{};
		bool _wasConnected{};//a connection has stood:  the next is a reconnect.
		UA_UInt32 _reconnects{};
		optional<uint32_t> _stationId;//the connection's, MID 0002 revision 6's:  a walk drops another station's results.
		uint16_t _resultRevision{};//MID 0061's as the last result came, or as the subscription was accepted below it:  0 before the first.
		flat_map<uint16_t,uint16_t> _shown;//what `_Op.Revisions` last showed, by the MID it names.
		vector<bool> _lacked;//TighteningResult::Variables' each:  it reads Bad_NotSupported for want of it in the revision, or in the last result.
		optional<TimePoint> _lastResult;//the last result's source timestamp, or the last NotSupported marks'.
		optional<uint32_t> _lastId;//the last result written:  where a recovery's walk ends.
		optional<Recovery> _recovery;//from a reconnect until its walk ends:  results pushed meanwhile are held.
		bool _clockBack{};//results are taking the last's time after a clock went back past maxSkew:  warned once.
		vector<Value> _state;//`Selected`'s and `Alarm`'s variables, each as it was last written:  one that reads Good goes Bad_NoCommunication with the link.
		flat_set<uint16_t> _refused;//the state subscriptions a refusal marked Bad_NotSupported:  accepted, their variables wait for a report again.
		UA_UInt64 _results{}, _lost{}, _total{}, _ok{}, _nok{};
	};
}