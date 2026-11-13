#include "OpenProtocolDriver.h"
#include <boost/asio/post.hpp>
#include <jde/fwk/settings.h>
#include <jde/fwk/process/execution.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/UAString.h>
#include <jde/opc/server/UAAccess.h>
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
	//The block as a session's settings, with the row's ceilings.  callTimeout, maxBackfill and maxSkew are the driver's own.
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
	//`Selected`'s and `Alarm`'s variables (spec *Address space*), each a field of what the subscription `Subscribes` pushes.
	struct StateVariable{ sv Object; sv Name; uint16_t Subscribes; uint16_t Type; };
	enum : uint8_t{ Pset, Job, Vin, Part2, Part3, Part4, AlarmCode, AlarmActive, AlarmTime };//_stateVariables' indexes.
	constexpr array<StateVariable,9> _stateVariables{{
		{"Selected", "Pset", Mid0014::Id, UA_TYPES_UINT16}, {"Selected", "Job", Mid0034::Id, UA_TYPES_UINT16}, {"Selected", "Vin", Mid0051::Id, UA_TYPES_STRING},
		{"Selected", "IdentifierPart2", Mid0051::Id, UA_TYPES_STRING}, {"Selected", "IdentifierPart3", Mid0051::Id, UA_TYPES_STRING}, {"Selected", "IdentifierPart4", Mid0051::Id, UA_TYPES_STRING},
		{"Alarm", "Code", Mid0070::Id, UA_TYPES_STRING}, {"Alarm", "Active", Mid0070::Id, UA_TYPES_BOOLEAN}, {"Alarm", "Time", Mid0070::Id, UA_TYPES_DATETIME}
	}};
	Ω statePath( uint i )ι->string{ return Ƒ( "{}.{}", _stateVariables[i].Object, _stateVariables[i].Name ); }
	//A report's field as its variable's value, none where the report sent none (§2.3, Table 3);  a name's empty text is a value.
	Ω text( str value )ι->optional<Value>{
		let ua = ToUV( value );
		return TighteningResult::Scalar( &ua, UA_TYPES[UA_TYPES_STRING] );
	}
	Ω number( optional<uint16_t> value )ι->optional<Value>{
		const UA_UInt16 ua{ value.value_or(0) };
		return value ? optional<Value>{ TighteningResult::Scalar(&ua, UA_TYPES[UA_TYPES_UINT16]) } : nullopt;
	}
	Ω flag( bool value )ι->optional<Value>{
		const UA_Boolean ua{ value };
		return TighteningResult::Scalar( &ua, UA_TYPES[UA_TYPES_BOOLEAN] );
	}
	//`Commands`' methods (spec *Commands*), each the MID of its purpose (§4.5) and the one argument it takes, none for the tool's two.
	struct CommandMethod{ sv Name; uint16_t Mid; sv Argument; uint16_t Type; };
	constexpr array<CommandMethod,5> _commands{{
		{"SelectPset", Mid0018::Id, "pset", UA_TYPES_UINT16}, {"SelectJob", Mid0038::Id, "job", UA_TYPES_UINT16}, {"DisableTool", Mid0042::Id, {}, 0},
		{"EnableTool", Mid0043::Id, {}, 0}, {"DownloadVin", Mid0050::Id, "vin", UA_TYPES_STRING}
	}};
	constexpr sv _commandsObject{ "Commands" };
	//A method's request and its argument as the call's line has it, from the one input open62541 has typed.
	Ω commandRequest( uint16_t mid, const UA_Variant* input )ι->std::pair<Body,string>{
		switch( mid ){
			case Mid0018::Id:{ let pset = *(const UA_UInt16*)input->data; return { Mid0018{.Pset=pset}, std::to_string(pset) }; }
			case Mid0038::Id:{ let job = *(const UA_UInt16*)input->data; return { Mid0038{.JobId=job}, std::to_string(job) }; }
			case Mid0050::Id:{ auto vin = ToString( *(const UA_String*)input->data ); auto text = Ƒ( "\"{}\"", vin ); return { Mid0050{.Vin=move(vin)}, move(text) }; }
			case Mid0042::Id: return { Mid0042{}, {} };
			default: return { Mid0043{}, {} };
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
		_callTimeout{ blockDuration("callTimeout", 30s) },
		_maxAsyncOperations{ Settings::FindNumber<uint>(Ƒ("{}/maxAsyncOperations", _block)) },
		_maxBackfill{ Settings::FindNumber<uint>(Ƒ("{}/maxBackfill", _block)).value_or(2000) },
		_strand{ boost::asio::make_strand(*Executor()) },
		_posts{ ms<Posts>(_slug) },
		_lacked( TighteningResult::Variables().size() )
	{
		const OpenProtocolRow controller{ row, sl };
		_zone = controller.TimeZone ? controller.TimeZone : std::chrono::current_zone();
		_ceilings = controller.Revisions;
		_session.emplace( _strand, controller.Host, controller.Port, sessionSettings(controller), Session::Events{
			[this]( Session::Connection&& connection ){ OnConnected( move(connection) ); },
			[this]( const Failure& why ){ OnLost( why ); },
			[this]( Message&& pushed ){ OnPushed( move(pushed) ); }
		} );
		_state.reserve( _stateVariables.size() );
		for( let& variable : _stateVariables )
			_state.push_back( TighteningResult::Unset(UA_TYPES[variable.Type], UA_STATUSCODE_BADWAITINGFORINITIALDATA) );
	}
	α OpenProtocolDriver::Configure( Opc::Server::UASettings& settings )ε->void{
		settings.Limit.MaxAsyncOperations = _maxAsyncOperations;
		settings.AsyncTimeout = _callTimeout;
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
	α OpenProtocolDriver::Build( UA_Server& server, UA_UInt16 ns, sp<DriverCalls> calls )ε->void{
		_server = &server;
		_ns = ns;
		_calls = move( calls );
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
		for( let object : {"Selected"sv, "Alarm"sv} ){
			addObject( object );
			for( uint i=0; i<_stateVariables.size(); ++i ){
				let& variable = _stateVariables[i];
				if( variable.Object!=object )
					continue;
				let waiting = stamped( Value{_state[i]}, started );
				addVariable( object, variable.Name, UA_TYPES[variable.Type], waiting.value.data );
				write( server, ns, _slug, statePath(i), waiting );
			}
		}
		//Each method's context is this driver, which its host keeps until the server has gone.  Its InputArguments property is
		//added here, under its path as every node is:  open62541 adds one by a string id and leaks its copy of the id
		//(UA_Server_addMethodNodeEx_finish, v1.5.10), and gives one it names itself a number in namespace 1.
		addObject( _commandsObject );
		for( let& command : _commands ){
			UA_MethodAttributes attributes = UA_MethodAttributes_default;
			attributes.displayName = UA_LocalizedText{ {}, ToUV(command.Name) };
			attributes.executable = attributes.userExecutable = true;
			let path = Ƒ( "{}.{}", _commandsObject, command.Name );
			UAε( UA_Server_addMethodNode(&server, nodeId(ns, path), nodeId(ns, _commandsObject), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), UA_QualifiedName{ns, ToUV(command.Name)}, attributes, &OnCall, 0, nullptr, 0, nullptr, this, nullptr) );
			if( command.Argument.empty() )
				continue;
			UA_Argument argument; UA_Argument_init( &argument );
			argument.name = ToUV( command.Argument );
			argument.dataType = UA_TYPES[command.Type].typeId;
			argument.valueRank = UA_VALUERANK_SCALAR;
			UA_UInt32 count{ 1 };
			UA_VariableAttributes inputs = UA_VariableAttributes_default;
			inputs.displayName = UA_LocalizedText{ {}, ToUV("InputArguments") };
			inputs.dataType = UA_TYPES[UA_TYPES_ARGUMENT].typeId;
			inputs.valueRank = UA_VALUERANK_ONE_DIMENSION;
			inputs.arrayDimensions = &count;
			inputs.arrayDimensionsSize = 1;
			inputs.accessLevel = UA_ACCESSLEVELMASK_READ;
			UA_Variant_setArray( &inputs.value, &argument, 1, &UA_TYPES[UA_TYPES_ARGUMENT] );
			UAε( UA_Server_addVariableNode(&server, nodeId(ns, Ƒ("{}.InputArguments", path)), nodeId(ns, path), UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY), UA_QualifiedName{0, ToUV("InputArguments")}, UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE), inputs, nullptr, nullptr) );
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
	α OpenProtocolDriver::Stop( absl::AnyInvocable<void()> stopped )ι->void{
		_stopping = true;
		_session->Stop( [gone=_stopped, stopped=std::move(stopped)]()mutable{ *gone = true; stopped(); } );
	}

	//The Call service has typed the input by the method's InputArguments and asked the host for Execute (spec *Commands*).  A
	//call with no session's context is the server's own, UA_Server_call, with no user to answer for it.
	α OpenProtocolDriver::OnCall( UA_Server*, const UA_NodeId*, void* sessionContext, const UA_NodeId* methodId, void* methodContext, const UA_NodeId*, void*, size_t inputSize, const UA_Variant* input, size_t, UA_Variant* output )ι->UA_StatusCode{
		let driver = static_cast<OpenProtocolDriver*>( methodContext );
		if( !driver || !methodId || !output )
			return UA_STATUSCODE_BADINTERNALERROR;
		let session = static_cast<const Opc::Server::UAAccess::SessionContext*>( sessionContext );
		if( !session )
			return UA_STATUSCODE_BADUSERACCESSDENIED;
		let id = methodId->identifierType==UA_NODEIDTYPE_STRING ? ToSV( methodId->identifier.string ) : sv{};
		let command = std::ranges::find_if( _commands, [&]( let& c ){ return id==Ƒ( "{}.{}", _commandsObject, c.Name ); } );
		if( command==_commands.end() )
			return UA_STATUSCODE_BADMETHODINVALID;
		if( command->Argument.size() && (inputSize!=1 || !UA_Variant_hasScalarType(input, &UA_TYPES[command->Type])) )
			return UA_STATUSCODE_BADINVALIDARGUMENT;
		auto [request, argument] = commandRequest( command->Mid, input );
		return driver->Command( move(request), Ƒ("{}({})", command->Name, argument.empty() ? string{} : Ƒ(" {} ", argument)), session->UserPK, output );
	}
	//What no revision the row may ask carries is refused here, with nothing sent;  what the revision the controller settles does
	//not carry, by the session as it encodes the request (EFailure::Value).  The strand has the call from here:  the driver may
	//have stopped, and gone, by the time it runs.
	α OpenProtocolDriver::Command( Body&& request, string&& text, UserPK user, const UA_Variant* output )ι->UA_StatusCode{
		if( _stopping ){
			_calls->Refused( user, text, UA_STATUSCODE_BADSHUTDOWN, "The driver is stopping." );
			return UA_STATUSCODE_BADSHUTDOWN;
		}
		let mid = MidOf( request );
		let ceiling = _ceilings.find( mid );
		let revision = ceiling==_ceilings.end() || !ceiling->second ? MaxRevision( mid ) : std::min( MaxRevision(mid), ceiling->second );
		if( let frame = Encode({.Revision=revision}, request); !frame ){
			_calls->Refused( user, text, UA_STATUSCODE_BADOUTOFRANGE, frame.error().What );
			return UA_STATUSCODE_BADOUTOFRANGE;
		}
		boost::asio::post( _strand, [this, stopped=_stopped, calls=_calls, call=_calls->Hold(output, user, text), request=move(request)]()mutable{
			if( *stopped )
				calls->Dropped( call, "The driver has stopped." );
			else
				Send( move(call), move(request) );
		} );
		return UA_STATUSCODE_GOODCOMPLETESASYNCHRONOUSLY;
	}
	//Behind whatever is outstanding (§3.2.1).  A call open62541 cancelled before this - the queue's limit as its callback
	//returned, a client's Cancel - has its client answered, and is not sent.  A request the session takes after its stop has
	//ended is answered at once, unsent, on a strand the driver may have left.
	α OpenProtocolDriver::Send( DriverCalls::Call&& call, Body&& request )ι->void{
		if( !_calls->Held(call) ){
			_calls->Dropped( call, "open62541 cancelled it first - a client's Cancel, the queue's limit or a timeout." );
			return;
		}
		let mid = MidOf( request );
		_session->Request( move(request), [this, mid, stopped=_stopped, calls=_calls, call=move(call)]( Session::Reply&& reply )mutable{
			if( *stopped )
				calls->Dropped( call, "The driver stopped first." );
			else
				OnCommanded( move(call), mid, move(reply) );
		} );
	}
	//MID 0005 is Good.  MID 0004 is the controller's refusal, a Viewer's 92 and a second Actor's 35 among its codes;  a
	//controller silent for replyTimeout has lost its link by it;  a link that is down, or goes while the request waits, sent
	//nothing or has no answer to give.  A driver whose session stopped for a controller it does not take has no link either.
	α OpenProtocolDriver::OnCommanded( DriverCalls::Call&& call, uint16_t mid, Session::Reply&& reply )ι->void{
		UA_StatusCode result{ UA_STATUSCODE_GOOD };
		string detail;
		if( reply )
			ShowRevision( mid, _session->Revision(mid) );
		else{
			let& why = reply.error();
			detail = why.What;
			switch( why.Kind ){
				case EFailure::Refused:
					result = UA_STATUSCODE_BADINVALIDSTATE;
					WriteText( "LastError", why.What );
					break;
				case EFailure::Timeout: result = UA_STATUSCODE_BADTIMEOUT; break;
				case EFailure::Value: result = UA_STATUSCODE_BADOUTOFRANGE; break;
				case EFailure::Stopped: result = _stopping ? UA_STATUSCODE_BADSHUTDOWN : UA_STATUSCODE_BADNOCOMMUNICATION; break;
				default: result = UA_STATUSCODE_BADNOCOMMUNICATION;
			}
		}
		_calls->Answer( *_server, move(call), result, move(detail) );
	}

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
		string text;
		for( let& [mid, revision] : named )
			text += Ƒ( "{}{:04}:{}", text.empty() ? "" : " ", mid, revision );
		WriteText( "Revisions", text );
		_shown = move( named );
	}
	//A push is read by its own header's revision, which may be below the one its subscription was accepted at (§1.4).
	α OpenProtocolDriver::ShowRevision( uint16_t named, uint16_t revision )ι->void{
		if( let shown = _shown.find(named); shown==_shown.end() || shown->second!=revision )
			WriteRevisions();
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
		//Behind the two, and ahead of a recovery's walk, whose first request the clock's reply makes.
		let subscribe = [this]<class T>( T&& request ){ _session->Request( request, [this]( Session::Reply&& reply ){ OnStateSubscribed( T::Id, move(reply) ); } ); };
		subscribe( Mid0014{} );
		subscribe( Mid0034{} );
		subscribe( Mid0051{} );
		subscribe( Mid0070{} );
	}
	α OpenProtocolDriver::OnLost( const Failure& why )ι->void{
		if( _connected ){
			_connected = false;
			INFO( "({}) lost '{}':  {}", _slug, _url, why.What );
			WriteConnected( false );
			//`Selected` and `Alarm` follow the link (spec *Address space*):  what read Good is stale, its value kept, until its
			//next report.  One with no report yet, or none to come, says so still.
			Writes stale;
			let at = UA_DateTime_now();
			for( uint i=0; i<_state.size(); ++i ){
				auto& held = _state[i];
				if( held.status )
					continue;
				held.status = UA_STATUSCODE_BADNOCOMMUNICATION;
				held.hasStatus = true;
				stale.emplace_back( statePath(i), stamped(Value{held}, at) );
			}
			Post( move(stale) );
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
			ShowRevision( Mid0061::Id, revision );
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
		if( result.TighteningId.value_or(0) )//0 is no id, as MID 0064 has it:  a walk could never reach it.
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

	//A result, or a report to `Selected` or `Alarm` (spec *Address space*).  MID 0052 carries the identifier parts from revision
	//2.  An alarm is active from MID 0071 until MID 0074 says it was acknowledged on the controller, which carries its code and
	//no time, and clears only the alarm held under that code;  MID 0076 is the alarm's status as the controller has it, sent as
	//the subscription is accepted, one of no alarm and no time leaving the time.  An alarm's time is whole seconds local to the
	//row's zone, read as a pushed result's is (TighteningResult::ToSys).
	α OpenProtocolDriver::OnPushed( Message&& pushed )ι->void{
		let revision = pushed.Head.Revision;
		let time = [this]( optional<LocalTime> local )ι->optional<Value>{
			if( !local )
				return nullopt;
			let ua = UADateTime{ TighteningResult::ToSys(*local, *_zone, Clock::now()) }.UA();
			return TighteningResult::Scalar( &ua, UA_TYPES[UA_TYPES_DATETIME] );
		};
		if( let result = get_if<Mid0061>(&pushed.Data) )
			OnResult( revision, *result );
		else if( let pset = get_if<Mid0015>(&pushed.Data) )
			Report( Mid0015::Id, revision, {{Pset, number(pset->Pset)}} );
		else if( let job = get_if<Mid0035>(&pushed.Data) )
			Report( Mid0035::Id, revision, {{Job, number(job->JobId)}} );
		else if( let vin = get_if<Mid0052>(&pushed.Data) ){
			let part = [=]( str value ){ return revision>=2 ? text( value ) : nullopt; };
			Report( Mid0052::Id, revision, {{Vin, text(vin->Vin)}, {Part2, part(vin->IdentifierPart2)}, {Part3, part(vin->IdentifierPart3)}, {Part4, part(vin->IdentifierPart4)}} );
		}
		else if( let alarm = get_if<Mid0071>(&pushed.Data) )
			Report( Mid0071::Id, revision, {{AlarmCode, text(alarm->ErrorCode)}, {AlarmActive, flag(true)}, {AlarmTime, time(alarm->Time)}} );
		else if( let status = get_if<Mid0076>(&pushed.Data) ){
			vector<std::pair<uint8_t,optional<Value>>> values{ {AlarmCode, text(status->ErrorCode)}, {AlarmActive, status->AlarmStatus ? flag(*status->AlarmStatus==1) : nullopt} };
			if( status->Time || status->AlarmStatus!=0 )//no alarm and no time is not an alarm with no time:  the time stays as the alarm left it.
				values.emplace_back( AlarmTime, time(status->Time) );
			Report( Mid0071::Id, revision, move(values) );
		}
		else if( let acknowledged = get_if<Mid0074>(&pushed.Data) ){
			//`Alarm` is one alarm:  an older one acknowledged leaves the one it holds active, and over a lost link not known to be.
			let& held = _state[AlarmCode];
			let code = ToSV( *(const UA_String*)held.value.data );
			if( (held.status!=UA_STATUSCODE_GOOD && held.status!=UA_STATUSCODE_BADNOCOMMUNICATION) || code==acknowledged->ErrorCode )
				Report( 0, 0, {{AlarmCode, text(acknowledged->ErrorCode)}, {AlarmActive, flag(false)}} );
			else
				DBG( "({}) MID 0074 acknowledged alarm '{}', not '{}', which Alarm holds:  left as it is.", _slug, acknowledged->ErrorCode, code );
		}
	}
	//Sourced at its receipt, as the loss of the link is, so a variable's source timestamps increase whatever the controller's
	//clock.  A variable that reads Bad_NotSupported already is left as it is.
	α OpenProtocolDriver::Report( uint16_t named, uint16_t revision, vector<std::pair<uint8_t,optional<Value>>>&& values )ι->void{
		Writes writes;
		let at = UA_DateTime_now();
		for( auto& [i, value] : values ){
			auto& held = _state[i];
			if( !value && held.status==UA_STATUSCODE_BADNOTSUPPORTED )
				continue;
			held = value ? move( *value ) : TighteningResult::Unset( UA_TYPES[_stateVariables[i].Type], UA_STATUSCODE_BADNOTSUPPORTED );
			writes.emplace_back( statePath(i), stamped(Value{held}, at) );
		}
		Post( move(writes) );
		if( named )
			ShowRevision( named, revision );
	}
	//A controller with no such subscription, MID 0004 past the step-down, is no fault of the link's:  its variables read
	//Bad_NotSupported, said once, and `_Op.LastError` has the refusal.  Accepted at a later connect, as a controller given the
	//feature since would, they wait for its report again;  a Bad_NotSupported a report left under one never refused stays.  A
	//link that went first is OnLost's.
	α OpenProtocolDriver::OnStateSubscribed( uint16_t mid, Session::Reply&& reply )ι->void{
		if( !reply && reply.error().Kind!=EFailure::Refused )
			return;
		//The subscription's variables whose status `from` picks, each written `to` now:  their paths, for the log.
		let restate = [&, at=UA_DateTime_now()]( auto from, UA_StatusCode to ){
			Writes writes;
			string names;
			for( uint i=0; i<_state.size(); ++i ){
				let& variable = _stateVariables[i];
				if( variable.Subscribes!=mid || !from(_state[i].status) )
					continue;
				_state[i] = TighteningResult::Unset( UA_TYPES[variable.Type], to );
				writes.emplace_back( statePath(i), stamped(Value{_state[i]}, at) );
				names += Ƒ( "{}{}", names.empty() ? "" : ", ", writes.back().first );
			}
			Post( move(writes) );
			return names;
		};
		if( reply ){
			if( let names = _refused.erase(mid) ? restate([]( UA_StatusCode s ){ return s==UA_STATUSCODE_BADNOTSUPPORTED; }, UA_STATUSCODE_BADWAITINGFORINITIALDATA) : string{}; names.size() )
				INFO( "({}) the controller accepted MID {:04}, refused before, so {} read Bad_WaitingForInitialData until its report.", _slug, mid, names );
			WriteRevisions();
			return;
		}
		WriteText( "LastError", reply.error().What );
		_refused.emplace( mid );
		if( let names = restate([]( UA_StatusCode s ){ return s!=UA_STATUSCODE_BADNOTSUPPORTED; }, UA_STATUSCODE_BADNOTSUPPORTED); names.size() )
			INFO( "({}) the controller refused MID {:04}, so {} read Bad_NotSupported:  {}", _slug, mid, names, reply.error().What );
	}

	//MID 0064 for the result before `below`, each reply asking for the next:  one request out at a time.  A controller answers
	//an id no result has with the result before it, which is how the walk passes the ids of its other stations and the gaps
	//between them.  0 asks for the latest (Table 121).  Nothing is asked where the id before `below` is the last written:  the
	//walk is whole, though its last request has gone.
	α OpenProtocolDriver::Walk( uint32_t below )ι->void{
		if( below==1 )
			EndWalk( "The controller's results begin at id 1." );
		else if( below && below-1==*_lastId )
			EndWalk();
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