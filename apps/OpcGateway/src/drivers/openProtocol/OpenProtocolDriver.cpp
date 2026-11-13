#include "OpenProtocolDriver.h"
#include <jde/fwk/settings.h>
#include <jde/fwk/process/execution.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/UAString.h>
#include <jde/opc/server/UAServer.h>
#include <jde/opc/openProtocol/logTags.h>
#include "../../types/OpenProtocolRow.h"
#include "../../types/ServerCnnctn.h"
#include "TighteningResult.h"

#define let const auto
namespace Jde::Opc::Gateway{
	using namespace OpenProtocol;
	constexpr ELogTags _tags{ LogTag };
	constexpr sv _block{ "/gateway/drivers/openProtocol" };

	//A duration of the block, `dflt` where it is absent or would spin a timer.  The defaults are config/Opc.Gateway.jsonnet's.
	Ω blockDuration( sv name, Duration dflt )ι->Duration{
		let path = Ƒ( "{}/{}", _block, name );
		let value = Settings::FindDuration( path ).value_or( dflt );
		if( value>Duration::zero() )
			return value;
		WARNT( ELogTags::Settings, "'{}' of {} is not allowed:  it must be positive.  Using {}.", path, Chrono::ToString(value), Chrono::ToString(dflt) );
		return dflt;
	}
	//The block as a session's settings, with the row's ceilings.  callTimeout is the commands' (plan Phase 8), and maxBackfill
	//the driver's own.
	Ω sessionSettings( const OpenProtocolRow& row )ι->Session::Settings{
		return {
			.KeepAlive = blockDuration( "keepAlive", 10s ),
			.ReplyTimeout = blockDuration( "replyTimeout", 10s ),
			.ReconnectMin = blockDuration( "reconnectMin", 1s ),
			.ReconnectMax = blockDuration( "reconnectMax", 1min ),
			.Revisions = row.Revisions,
			.Log = Settings::FindBool( Ƒ("{}/log", _block) ).value_or( false )
		};
	}
	//MID 0002's system types whose results are not MID 0061 (spec *Controllers*), empty for one the driver takes.
	Ω unsupported( optional<uint16_t> systemType )ι->sv{
		return systemType==2 ? "PowerMACS"sv : systemType==4 ? "MT Focus 6000"sv : sv{};
	}
	//`_Op.Revisions` names a subscription by the MID it pushes, as a row's ceiling does:  0061 for MID 0060's.
	Ω revisionName( uint16_t asked )ι->uint16_t{
		switch( asked ){
			case Mid0014::Id: return Mid0015::Id;
			case Mid0034::Id: return Mid0035::Id;
			case Mid0051::Id: return Mid0052::Id;
			case Mid0060::Id: return Mid0061::Id;
			case Mid0070::Id: return Mid0071::Id;
			default: return asked;
		}
	}

	//How long the posts held the server's thread, counted there alone and said when the last has run, the driver gone:  a fill is
	//a post a result, a burst of them (plan *Risks*).
	struct OpenProtocolDriver::Posts final{
		~Posts(){
			if( Count )
				INFO( "({}) {} posts held its server's thread {} µs in all, the longest {} µs.", Slug, Count, duration_cast<microseconds>(Total).count(), duration_cast<microseconds>(Longest).count() );
		}
		const ServerCnnctnNK Slug;
		uint Count{};
		steady_clock::duration Total{}, Longest{};
	};

	OpenProtocolDriver::OpenProtocolDriver( const ServerCnnctn& row, SL sl )ε:
		_slug{ row.Slug },
		_url{ row.Url },
		_maxSkew{ blockDuration("maxSkew", 5min) },
		_maxAsyncOperations{ Settings::FindNumber<uint>(Ƒ("{}/maxAsyncOperations", _block)) },
		_maxBackfill{ Settings::FindNumber<uint>(Ƒ("{}/maxBackfill", _block)).value_or(2000) },
		_posts{ ms<Posts>(_slug) },
		_lacked( TighteningResult::Variables().size() )
	{
		const OpenProtocolRow controller{ row, sl };
		_zone = controller.TimeZone ? controller.TimeZone : std::chrono::current_zone();
		_session.emplace( boost::asio::make_strand(*Executor()), controller.Host, controller.Port, sessionSettings(controller), Session::Events{
			[this]( Session::Connection&& connection ){ OnConnected( move(connection) ); },
			[this]( const Failure& why ){ OnLost( why ); },
			[this]( Message&& pushed ){
				if( let result = get_if<Mid0061>(&pushed.Data) )
					OnResult( pushed.Head.Revision, *result );
			}
		} );
	}
	α OpenProtocolDriver::Configure( Opc::Server::UASettings& settings )ε->void{
		settings.Limit.MaxAsyncOperations = _maxAsyncOperations;
		settings.Limit.MaxQueueSize = 2*_maxBackfill;//the held count is not known here:  as many as the walk may find (spec *Backfill*).
	}

	//Each node's id is the string of its path, `_Op.Connected` (spec *Address space*).
	Ω nodeId( UA_UInt16 ns, sv path )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(ns, string{path}.c_str()) }; }
	//With its source timestamp and no server timestamp:  open62541 answers a read's and an item's sample's with its own clock
	//(addMissingTimestamps), which for an item at 0 is the write's.
	Ω stamped( Value&& value, UA_DateTime source )ι->Value{
		value.sourceTimestamp = source;
		value.hasSourceTimestamp = true;
		return move( value );
	}
	//The scalar as `_Op`'s value, with `status` and the time now, owned:  a posted write runs after the caller's scalar is gone.
	Ω opValue( const void* scalar, const UA_DataType& type, UA_StatusCode status=UA_STATUSCODE_GOOD )ι->Value{
		return stamped( TighteningResult::Scalar(scalar, type, status), UA_DateTime_now() );
	}
	Ω write( UA_Server& server, UA_UInt16 ns, sv slug, sv path, const UA_DataValue& value )ι->void{
		if( let sc = UA_Server_writeDataValue(&server, nodeId(ns, path), value); sc )
			WARN( "({}) could not write {}:  {}", slug, path, UAException::Message(sc) );
	}
	α OpenProtocolDriver::Build( UA_Server& server, UA_UInt16 ns )ε->void{
		_server = &server;
		_ns = ns;
		let addObject = [&]( sv name ){
			UA_ObjectAttributes object = UA_ObjectAttributes_default;
			object.displayName = UA_LocalizedText{ {}, ToUV(name) };
			UAε( UA_Server_addObjectNode(&server, nodeId(ns, name), NodeId::ObjectsFolder(), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QualifiedName{ns, ToUV(name)}, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), object, nullptr, nullptr) );
		};
		//AccessLevel CurrentRead, and the defaults:  WriteMask 0, MinimumSamplingInterval 0, so an item at 0 hears each write.
		let addVariable = [&]( sv object, sv name, const UA_DataType& type, const void* initial ){
			UA_VariableAttributes attributes = UA_VariableAttributes_default;
			attributes.displayName = UA_LocalizedText{ {}, ToUV(name) };
			attributes.dataType = type.typeId;
			attributes.valueRank = UA_VALUERANK_SCALAR;
			attributes.accessLevel = UA_ACCESSLEVELMASK_READ;
			UA_Variant_setScalar( &attributes.value, const_cast<void*>(initial), &type );
			UAε( UA_Server_addVariableNode(&server, nodeId(ns, Ƒ("{}.{}", object, name)), nodeId(ns, object), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), UA_QualifiedName{ns, ToUV(name)}, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
		};
		//These written here, before Run, with no lock to wait for:  a post would wait for a loop that may never run.
		let started = UA_DateTime_now();
		addObject( "Result" );
		for( let& variable : TighteningResult::Variables() ){
			let waiting = stamped( TighteningResult::Unset(*variable.Type, UA_STATUSCODE_BADWAITINGFORINITIALDATA), started );
			addVariable( "Result", variable.Name, *variable.Type, waiting.value.data );
			write( server, ns, _slug, Ƒ("Result.{}", variable.Name), waiting );
		}
		addObject( "Counters" );
		for( let name : {"Total"sv, "Ok"sv, "Nok"sv} ){
			const UA_UInt64 none{};
			addVariable( "Counters", name, UA_TYPES[UA_TYPES_UINT64], &none );
			write( server, ns, _slug, Ƒ("Counters.{}", name), stamped(opValue(&none, UA_TYPES[UA_TYPES_UINT64]), started) );
		}
		addObject( "_Op" );
		let add = [&]( sv name, const UA_DataType& type, const void* initial ){ addVariable( "_Op", name, type, initial ); };
		const UA_Boolean no{};
		const UA_String none{};
		const UA_UInt32 reconnects{};
		const UA_UInt64 count{};
		const UA_Double skew{};
		let since = UADateTime{ Clock::now() }.UA();
		let url = ToUV( _url );
		add( "Connected", UA_TYPES[UA_TYPES_BOOLEAN], &no );
		add( "Since", UA_TYPES[UA_TYPES_DATETIME], &since );
		add( "Url", UA_TYPES[UA_TYPES_STRING], &url );
		add( "LastError", UA_TYPES[UA_TYPES_STRING], &none );
		add( "Reconnects", UA_TYPES[UA_TYPES_UINT32], &reconnects );
		add( "Identity", UA_TYPES[UA_TYPES_STRING], &none );
		add( "Revisions", UA_TYPES[UA_TYPES_STRING], &none );
		add( "LinkLevel", UA_TYPES[UA_TYPES_BOOLEAN], &no );
		add( "Results", UA_TYPES[UA_TYPES_UINT64], &count );
		add( "LostResults", UA_TYPES[UA_TYPES_UINT64], &count );
		add( "Skew", UA_TYPES[UA_TYPES_DOUBLE], &skew );
		write( server, ns, _slug, "_Op.Skew", opValue(&skew, UA_TYPES[UA_TYPES_DOUBLE], UA_STATUSCODE_BADWAITINGFORINITIALDATA) );//no clock read yet.
	}
	α OpenProtocolDriver::Start()ι->void{ _session->Start(); }
	α OpenProtocolDriver::Stop( absl::AnyInvocable<void()> stopped )ι->void{ _session->Stop( std::move(stopped) ); }

	//On the server's own thread, not the strand's executor thread, which the service lock's wait would hold (UAServer::Post).
	α OpenProtocolDriver::Post( Writes&& writes )ι->void{
		if( writes.empty() )
			return;
		Opc::Server::UAServer::Post( *_server, [ns=_ns, posts=_posts, writes=move(writes)]( UA_Server& server ){
			let start = steady_clock::now();
			for( let& [path, value] : writes )
				write( server, ns, posts->Slug, path, value );
			let held = steady_clock::now()-start;
			++posts->Count;
			posts->Total += held;
			posts->Longest = std::max( posts->Longest, held );
		} );
	}
	α OpenProtocolDriver::Write( sv name, const void* scalar, const UA_DataType& type, UA_StatusCode status )ι->void{
		Writes writes;
		writes.emplace_back( Ƒ("_Op.{}", name), opValue(scalar, type, status) );
		Post( move(writes) );
	}
	α OpenProtocolDriver::WriteText( sv name, sv text )ι->void{
		let value = ToUV( text );
		Write( name, &value, UA_TYPES[UA_TYPES_STRING] );
	}
	//A typed variable holds a value whatever its status, so a skew there is none of is 0 under the status that says why.
	α OpenProtocolDriver::WriteNoSkew( UA_StatusCode why )ι->void{
		const UA_Double none{};
		Write( "Skew", &none, UA_TYPES[UA_TYPES_DOUBLE], why );
	}
	α OpenProtocolDriver::WriteConnected( bool connected )ι->void{
		const UA_Boolean value{ connected };
		Write( "Connected", &value, UA_TYPES[UA_TYPES_BOOLEAN] );
		let since = UADateTime{ Clock::now() }.UA();
		Write( "Since", &since, UA_TYPES[UA_TYPES_DATETIME] );
	}
	α OpenProtocolDriver::WriteRevisions()ι->void{
		flat_map<uint16_t,uint16_t> named;
		for( let& [asked, revision] : _session->Revisions() )
			named[revisionName(asked)] = revision;
		let results = named.find( Mid0061::Id );
		_shownRevision = results==named.end() ? 0 : results->second;
		string text;
		for( let& [mid, revision] : named )
			text += Ƒ( "{}{:04}:{}", text.empty() ? "" : " ", mid, revision );
		WriteText( "Revisions", text );
	}

	α OpenProtocolDriver::OnConnected( Session::Connection&& connection )ι->void{
		jobject identity;
		for( let& field : OpenProtocolRow::Identity(connection.Identity) ){
			if( !field.value().is_null() )
				identity.emplace( field.key(), field.value() );
		}
		WriteText( "Identity", serialize(identity) );
		if( let system = unsupported(connection.Identity.SystemType); system.size() ){
			let why = Ƒ( "The controller is a {}, system type {} of MID 0002, whose results are not MID 0061:  the driver does not take it.", system, *connection.Identity.SystemType );
			WARN( "({}) {}", _slug, why );
			WriteText( "LastError", why );
			_session->Stop();
			return;
		}
		_connected = true;
		if( _wasConnected )
			++_reconnects;
		_wasConnected = true;
		_stationId = connection.Identity.StationId;
		INFO( "({}) connected to '{}':  MID 0002 at revision {}, acknowledged at {} level.", _slug, _url, connection.Revision, connection.LinkLevel ? "link" : "application" );
		const UA_Boolean linkLevel{ connection.LinkLevel };
		Write( "LinkLevel", &linkLevel, UA_TYPES[UA_TYPES_BOOLEAN] );
		Write( "Reconnects", &_reconnects, UA_TYPES[UA_TYPES_UINT32] );
		WriteRevisions();
		WriteConnected( true );
		//A result written before the link went:  what its loss cost is fetched, and what the subscription pushes meanwhile is held
		//(spec *Backfill*).  A row's first results have nothing before them to be fetched from.
		if( _lastId )
			_recovery.emplace();
		_session->Request( Mid0060{}, [this]( Session::Reply&& reply ){ OnSubscribed( move(reply) ); } );//first:  a result made before it is accepted is never pushed.
		_session->Request( Mid0080{}, [this]( Session::Reply&& reply ){ OnTime( move(reply) ); } );
	}
	α OpenProtocolDriver::OnLost( const Failure& why )ι->void{
		if( _connected ){
			_connected = false;
			INFO( "({}) lost '{}':  {}", _slug, _url, why.What );
			WriteConnected( false );
		}
		WriteText( "LastError", why.What );
		//What a recovery had found and held is the controller's still:  the next connection's walk finds it again, from the latest.
		if( let dropped = std::exchange(_recovery, nullopt); dropped && dropped->Found.size()+dropped->Held.size() )
			DBG( "({}) the link went during a recovery:  {} result(s) found and {} held are left to the next.", _slug, dropped->Found.size(), dropped->Held.size() );
	}
	//The controller's clock is the plant's to set:  read, shown and warned of, never corrected (spec *Open questions* #5).  Its
	//time is whole seconds local to the row's zone, read as a result's is (TighteningResult::ToSys).  A reply the link's loss
	//answered is OnLost's.  A recovery's walk starts here, once the subscription has had this long to push:  from the oldest
	//result held, the PF6000 appendix's start for a known id (§2.4), or from the latest.
	α OpenProtocolDriver::OnTime( Session::Reply&& reply )ι->void{
		if( !reply && reply.error().Kind!=EFailure::Refused )
			return;
		let time = reply ? get_if<Mid0081>( &reply->Data ) : nullptr;
		let now = Clock::now();
		TimePoint controller{ now };
		if( time && time->Time ){
			controller = TighteningResult::ToSys( *time->Time, *_zone, now );
			const UA_Double skew{ std::chrono::duration<double>(controller-now).count() };
			Write( "Skew", &skew, UA_TYPES[UA_TYPES_DOUBLE] );
			if( std::chrono::duration<double>{std::abs(skew)}>_maxSkew )
				WARN( "({}) the controller's clock is {:.0f} s {} the gateway's, past {}/maxSkew, {}:  each result carries the controller's time.", _slug, std::abs(skew), skew>0 ? "ahead of" : "behind", _block, Chrono::ToString(_maxSkew) );
			WriteRevisions();
		}
		else{
			if( !reply )
				WriteText( "LastError", reply.error().What );
			WriteNoSkew( UA_STATUSCODE_BADNOTSUPPORTED );
		}
		if( _recovery ){
			_recovery->By = controller+1s;//its clock's second has this long to run:  no result it holds is later.
			let& held = _recovery->Held;
			Walk( held.empty() ? 0 : held.front().Result.TighteningId.value_or(0) );
		}
	}

	//Accepted at the revision the results come at, until one's own header says another (spec *The protocol*):  what it lacks is
	//known before a result comes.  One above the last results' says nothing new, since a controller may take a revision and push
	//at its own (§1.4);  the first result does.  A link that went first is OnLost's, and the next connection subscribes again.
	α OpenProtocolDriver::OnSubscribed( Session::Reply&& reply )ι->void{
		if( !reply ){
			if( reply.error().Kind==EFailure::Refused ){
				WARN( "({}) the controller refused the results' subscription, so none come:  {}", _slug, reply.error().What );
				WriteText( "LastError", reply.error().What );
			}
			return;
		}
		if( let revision = _session->Revision(Mid0060::Id); !_resultRevision || revision<_resultRevision ){
			//After the last result by the controller's clock, as a result is, so the row's source timestamps stay increasing.
			let at = _lastResult ? *_lastResult+1ms : Clock::now();
			Writes writes;
			SetRevision( revision, UADateTime{at}.UA(), writes );
			if( _lastResult && writes.size() )
				_lastResult = at;
			Post( move(writes) );
		}
		WriteRevisions();
	}
	α OpenProtocolDriver::SetRevision( uint16_t revision, UA_DateTime at, Writes& writes )ι->void{
		let& variables = TighteningResult::Variables();
		for( uint i=0; i<variables.size(); ++i ){
			if( variables[i].Since>revision && !_lacked[i] ){
				writes.emplace_back( Ƒ("Result.{}", variables[i].Name), stamped(TighteningResult::Unset(*variables[i].Type, UA_STATUSCODE_BADNOTSUPPORTED), at) );
				_lacked[i] = true;
			}
		}
		_resultRevision = revision;
	}
	//MID 0061, which the session has acknowledged (spec *Results*), or MID 0065, which a recovery found.  Its time is whole
	//seconds local to the row's zone, so two rundowns in a second, or a clock set back, would share a time or reverse:  a result
	//not after the last takes the last's time and a millisecond, with a warning where the clock went back past maxSkew.  One
	//with no time takes `seen`.  A variable its message does not carry turns Bad_NotSupported at its time, where it is not
	//so already.
	template<class T> α OpenProtocolDriver::Record( const T& result, uint16_t revision, TimePoint seen )ι->void{
		constexpr bool stored{ std::same_as<T,Mid0065> };
		TimePoint at{ result.Time ? TighteningResult::ToSys(*result.Time, *_zone, seen, stored) : seen };
		if( _lastResult && at<=*_lastResult ){
			if( *_lastResult-at>_maxSkew && !std::exchange(_clockBack, true) )
				WARN( "({}) result {} is timed {}, {} before the last result's {}:  the controller's clock went back, so each result takes the last's time and a millisecond until the clock passes it.", _slug, result.TighteningId.value_or(0), ToIsoString(at), Chrono::ToString(*_lastResult-at), ToIsoString(*_lastResult) );
			at = *_lastResult+1ms;
		}
		else
			_clockBack = false;
		_lastResult = at;
		let source = UADateTime{ at }.UA();
		if( !stored ){
			_resultRevision = revision;
			if( revision!=_shownRevision )
				WriteRevisions();
		}
		Writes writes;
		auto values = TighteningResult::Values( result, revision, *_zone, seen );
		let& variables = TighteningResult::Variables();
		for( uint i=0; i<variables.size(); ++i ){
			let lacked = !values[i];
			if( !lacked )
				writes.emplace_back( Ƒ("Result.{}", variables[i].Name), stamped(move(*values[i]), source) );
			else if( !_lacked[i] )
				writes.emplace_back( Ƒ("Result.{}", variables[i].Name), stamped(TighteningResult::Unset(*variables[i].Type, UA_STATUSCODE_BADNOTSUPPORTED), source) );
			_lacked[i] = lacked;
		}
		let count = [&]( sv name, UA_UInt64& counter ){
			++counter;
			writes.emplace_back( Ƒ("Counters.{}", name), stamped(opValue(&counter, UA_TYPES[UA_TYPES_UINT64]), source) );
		};
		if( revision<3 || result.ResultType.value_or(1)==1 ){//a tightening:  below revision 3 a result names no type.
			count( "Total", _total );
			if( result.Status )
				count( *result.Status==1 ? "Ok"sv : "Nok"sv, *result.Status==1 ? _ok : _nok );
		}
		++_results;
		writes.emplace_back( "_Op.Results", opValue(&_results, UA_TYPES[UA_TYPES_UINT64]) );
		if( result.TighteningId )
			_lastId = result.TighteningId;
		TRACE( "({}) result {} at revision {} of MID {:04}, sourced {}.", _slug, result.TighteningId.value_or(0), revision, T::Id, ToIsoString(at) );
		Post( move(writes) );
	}
	α OpenProtocolDriver::OnResult( uint16_t revision, const Mid0061& result )ι->void{
		if( _recovery )
			_recovery->Held.push_back( {result, revision, Clock::now()} );
		else
			Record( result, revision, Clock::now() );
	}

	//MID 0064 for the result before `below`, each reply asking for the next:  one request out at a time.  A controller answers
	//an id no result has with the result before it, which is how the walk passes the ids of its other stations and the gaps
	//between them.  0 asks for the latest (Table 121).
	α OpenProtocolDriver::Walk( uint32_t below )ι->void{
		if( below==1 )
			EndWalk( "The controller's results begin at id 1." );
		else if( _recovery->Requests>=_maxBackfill )
			EndWalk( Ƒ("{}/maxBackfill, {} request(s), was reached.", _block, _maxBackfill) );
		else{
			++_recovery->Requests;
			_session->Request( Mid0064{.TighteningId=below ? below-1 : 0}, [this]( Session::Reply&& reply ){ OnOld( move(reply) ); } );
		}
	}
	//A link that went first is OnLost's.
	α OpenProtocolDriver::OnOld( Session::Reply&& reply )ι->void{
		if( !reply ){
			if( let& why = reply.error(); why.Kind==EFailure::Refused || why.Kind==EFailure::Value ){
				WriteText( "LastError", why.What );
				EndWalk( why.What );
			}
			return;
		}
		let found = get_if<Mid0065>( &reply->Data );
		let id = found ? found->TighteningId.value_or( 0 ) : 0;
		if( !id ){
			EndWalk( "A result came with no tightening id." );
			return;
		}
		auto& walk = *_recovery;
		walk.Oldest = id;
		let time = found->Time ? optional<TimePoint>{ TighteningResult::ToSys(*found->Time, *_zone, walk.By, true) } : nullopt;
		//The last result written, or one made before it, whichever station's:  the ids run up, a controller's stations sharing
		//them, so a higher one is later whatever its time, a second rundown in the last's second or a clock set back.  A lower
		//one timed after the last write is of a run of ids that began again.
		if( id==*_lastId || (id<*_lastId && (!time || *time<=*_lastResult)) ){
			EndWalk();
			return;
		}
		if( !found->StationId || !_stationId || *found->StationId==*_stationId )
			walk.Found.push_back( {move(*found), reply->Head.Revision, walk.By} );
		if( time )
			walk.By = *time;
		Walk( id );
	}
	//Oldest first, then what was pushed meanwhile, each id once:  a result that came both ways is written as it was pushed,
	//which carries more of it.
	α OpenProtocolDriver::EndWalk( sv shortBy )ι->void{
		let walk = move( *_recovery );
		_recovery.reset();
		if( shortBy.size() ){
			++_lost;
			Write( "LostResults", &_lost, UA_TYPES[UA_TYPES_UINT64] );
			let reached = walk.Oldest ? Ƒ( "the walk reached result {}, and what was made between the two is lost", *walk.Oldest ) : "the walk reached no result, and what was made since is lost"s;
			WARN( "({}) the recovery of results ended short of result {}, the last written:  {}.  _Op.LostResults is {}.  {}", _slug, *_lastId, reached, _lost, shortBy );
		}
		WriteRevisions();//MID 0064's.
		flat_set<uint32_t> written;
		for( let& pushed : walk.Held ){
			if( pushed.Result.TighteningId )
				written.emplace( *pushed.Result.TighteningId );
		}
		uint filled{};
		for( let& found : walk.Found | std::views::reverse ){
			if( !written.emplace(*found.Result.TighteningId).second )
				continue;
			Record( found.Result, found.Revision, found.Seen );
			++filled;
		}
		for( let& pushed : walk.Held )
			Record( pushed.Result, pushed.Revision, pushed.Seen );
		if( filled )
			INFO( "({}) wrote {} result(s) made while the link was down, found by {} MID 0064(s), ahead of {} pushed since.", _slug, filled, walk.Requests, walk.Held.size() );
	}
}