#include "OpenProtocolDriver.h"
#include <jde/fwk/settings.h>
#include <jde/fwk/process/execution.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/UAString.h>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/server/UAServer.h>
#include <jde/opc/openProtocol/logTags.h>
#include "../../types/OpenProtocolRow.h"
#include "../../types/ServerCnnctn.h"

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
	//The block as a session's settings, with the row's ceilings.  callTimeout and maxBackfill are the commands' and the
	//recovery's (plan Phases 8 and 6).
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

	OpenProtocolDriver::OpenProtocolDriver( const ServerCnnctn& row, SL sl )ε:
		_slug{ row.Slug },
		_url{ row.Url },
		_maxSkew{ blockDuration("maxSkew", 5min) },
		_maxAsyncOperations{ Settings::FindNumber<uint>(Ƒ("{}/maxAsyncOperations", _block)) }
	{
		const OpenProtocolRow controller{ row, sl };
		_zone = controller.TimeZone ? controller.TimeZone : std::chrono::current_zone();
		_session.emplace( boost::asio::make_strand(*Executor()), controller.Host, controller.Port, sessionSettings(controller), Session::Events{
			[this]( Session::Connection&& connection ){ OnConnected( move(connection) ); },
			[this]( const Failure& why ){ OnLost( why ); },
			{}
		} );
	}
	α OpenProtocolDriver::Configure( Opc::Server::UASettings& settings )ε->void{
		settings.Limit.MaxAsyncOperations = _maxAsyncOperations;
	}

	//Each node's id is the string of its path, `_Op.Connected` (spec *Address space*).
	Ω nodeId( UA_UInt16 ns, sv path )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(ns, string{path}.c_str()) }; }
	//The scalar as `_Op`'s value, with `status` and the time now, owned:  a posted write runs after the caller's scalar is gone.
	Ω opValue( const void* scalar, const UA_DataType& type, UA_StatusCode status )ι->Value{
		UA_DataValue view; UA_DataValue_init( &view );
		UA_Variant_setScalar( &view.value, const_cast<void*>(scalar), &type );
		view.hasValue = true;
		view.status = status;
		view.hasStatus = status!=UA_STATUSCODE_GOOD;
		view.sourceTimestamp = UA_DateTime_now();
		view.hasSourceTimestamp = true;
		return Value{ view };
	}
	Ω writeOp( UA_Server& server, UA_UInt16 ns, sv slug, sv name, const UA_DataValue& value )ι->void{
		if( let sc = UA_Server_writeDataValue(&server, nodeId(ns, Ƒ("_Op.{}", name)), value); sc )
			WARN( "({}) could not write _Op.{}:  {}", slug, name, UAException::Message(sc) );
	}
	α OpenProtocolDriver::Build( UA_Server& server, UA_UInt16 ns )ε->void{
		_server = &server;
		_ns = ns;
		UA_ObjectAttributes object = UA_ObjectAttributes_default;
		object.displayName = UA_LocalizedText{ {}, "_Op"_uv };
		UAε( UA_Server_addObjectNode(&server, nodeId(ns, "_Op"), NodeId::ObjectsFolder(), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QualifiedName{ns, "_Op"_uv}, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), object, nullptr, nullptr) );
		//AccessLevel CurrentRead, and the defaults:  WriteMask 0, MinimumSamplingInterval 0, so an item at 0 hears each write.
		let add = [&]( sv name, const UA_DataType& type, const void* initial ){
			UA_VariableAttributes attributes = UA_VariableAttributes_default;
			attributes.displayName = UA_LocalizedText{ {}, ToUV(name) };
			attributes.dataType = type.typeId;
			attributes.valueRank = UA_VALUERANK_SCALAR;
			attributes.accessLevel = UA_ACCESSLEVELMASK_READ;
			UA_Variant_setScalar( &attributes.value, const_cast<void*>(initial), &type );
			UAε( UA_Server_addVariableNode(&server, nodeId(ns, Ƒ("_Op.{}", name)), nodeId(ns, "_Op"), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), UA_QualifiedName{ns, ToUV(name)}, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
		};
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
		//No clock read yet.  Written here, before Run, with no lock to wait for:  a post would wait for a loop that may never run.
		writeOp( server, ns, _slug, "Skew", opValue(&skew, UA_TYPES[UA_TYPES_DOUBLE], UA_STATUSCODE_BADWAITINGFORINITIALDATA) );
	}
	α OpenProtocolDriver::Start()ι->void{ _session->Start(); }
	α OpenProtocolDriver::Stop( absl::AnyInvocable<void()> stopped )ι->void{ _session->Stop( std::move(stopped) ); }

	//On the server's own thread, not the strand's executor thread, which the service lock's wait would hold (UAServer::Post).
	α OpenProtocolDriver::Write( sv name, const void* scalar, const UA_DataType& type, UA_StatusCode status )ι->void{
		Opc::Server::UAServer::Post( *_server, [ns=_ns, slug=_slug, name=string{name}, value=opValue(scalar, type, status)]( UA_Server& server ){
			writeOp( server, ns, slug, name, value );
		} );
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
		INFO( "({}) connected to '{}':  MID 0002 at revision {}, acknowledged at {} level.", _slug, _url, connection.Revision, connection.LinkLevel ? "link" : "application" );
		const UA_Boolean linkLevel{ connection.LinkLevel };
		Write( "LinkLevel", &linkLevel, UA_TYPES[UA_TYPES_BOOLEAN] );
		Write( "Reconnects", &_reconnects, UA_TYPES[UA_TYPES_UINT32] );
		WriteRevisions();
		WriteConnected( true );
		_session->Request( Mid0080{}, [this]( Session::Reply&& reply ){ OnTime( move(reply) ); } );
	}
	α OpenProtocolDriver::OnLost( const Failure& why )ι->void{
		if( _connected ){
			_connected = false;
			INFO( "({}) lost '{}':  {}", _slug, _url, why.What );
			WriteConnected( false );
		}
		WriteText( "LastError", why.What );
	}
	//The controller's clock is the plant's to set:  read, shown and warned of, never corrected (spec *Open questions* #5).  Its
	//time is whole seconds local to the row's zone, an hour that comes twice read as its first.  A reply the link's loss answered
	//is OnLost's.
	α OpenProtocolDriver::OnTime( Session::Reply&& reply )ι->void{
		let time = reply ? get_if<Mid0081>( &reply->Data ) : nullptr;
		if( time && time->Time ){
			let controller = _zone->to_sys( *time->Time, std::chrono::choose::earliest );
			const UA_Double skew{ std::chrono::duration<double>(controller-Clock::now()).count() };
			Write( "Skew", &skew, UA_TYPES[UA_TYPES_DOUBLE] );
			if( std::chrono::duration<double>{std::abs(skew)}>_maxSkew )
				WARN( "({}) the controller's clock is {:.0f} s {} the gateway's, past {}/maxSkew, {}:  each result carries the controller's time.", _slug, std::abs(skew), skew>0 ? "ahead of" : "behind", _block, Chrono::ToString(_maxSkew) );
			WriteRevisions();
		}
		else if( reply || reply.error().Kind==EFailure::Refused ){
			if( !reply )
				WriteText( "LastError", reply.error().What );
			WriteNoSkew( UA_STATUSCODE_BADNOTSUPPORTED );
		}
	}
}