#pragma once
//A connection row of a kind with a driver, as the driver suites make and read one (DriverRowTests, DriverHostTests, and the
//hub's DriverTests), and the driver server it gets.
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/ql.h>
#include <jde/tests/WaitUntil.h>
#include <jde/opc/openProtocol/Controller.h>
#include "../../src/drivers/DriverHost.h"
#include "../../src/ql/GatewayQL.h"
#include "../../src/UAClient.h"
#include "helpers.h"
#ifndef let
#define let const auto
#endif

namespace Jde::Opc::Gateway::Tests{
	Ξ ControllerPort()ι->PortType{ return Settings::FindNumber<PortType>( "/testing/openProtocolPort" ).value_or( 4645 ); }//the suite's test controller.
	Ξ ControllerUrl()ι->string{ return Ƒ( "open-protocol://127.0.0.1:{}", ControllerPort() ); }
	//A row of the caller's shape through the QL:  its id, or what the save refused it with.
	Ξ CreateRow( str slug, sv args, jobject variables={} )ι->std::expected<ServerCnnctnPK,string>{
		try{
			return Json::AsNumber<ServerCnnctnPK>( QL().QuerySync<jobject>(Ƒ("mutation createServerConnection( slug:\"{0}\", name:\"{0}\", isDefault:false, {1} ){{id}}", slug, args), move(variables), {UserPK::System}), "id" );
		}
		catch( const std::exception& e ){
			return std::unexpected{ string{e.what()} };
		}
	}
	//An Open Protocol row for the suite's controller, or the one at `url`, with `driver` where one is given.
	Ξ CreateDriverRow( str slug, sv driver={}, string url=ControllerUrl() )ι->std::expected<ServerCnnctnPK,string>{
		return driver.empty()
			? CreateRow( slug, "url:$url, kind:\"open-protocol\"", {{"url", move(url)}} )
			: CreateRow( slug, "url:$url, kind:\"open-protocol\", driver:$driver", {{"url", move(url)}, {"driver", driver}} );
	}
	Ξ UpdateRow( ServerCnnctnPK id, sv args, jobject variables={} )ι->optional<string>{//what the save refused it with.
		try{
			QL().QuerySync<jvalue>( Ƒ("mutation updateServerConnection( id:{}, {} )", id, args), move(variables), {UserPK::System} );
		}
		catch( const std::exception& e ){
			return e.what();
		}
		return nullopt;
	}
	Ξ RowOf( ServerCnnctnPK id )ε->ServerCnnctn{//the row ctor's, what a client and a host are built from.
		auto rows = BlockAwait<ServerCnnctnAwait,vector<ServerCnnctn>>( ServerCnnctnAwait{DB::Key{id}} );
		THROW_IF( rows.size()!=1, "{} rows for connection {}.", rows.size(), id );
		return move( rows.front() );
	}
	//The revisions the controller heard `mid` at, from frame `from` on.
	Ξ RevisionsOf( const OpenProtocol::Controller& station, uint16_t mid, uint from=0 )ι->vector<uint16_t>{
		vector<uint16_t> y;
		let frames = station.Frames();
		for( uint i=from; i<frames.size(); ++i ){
			if( frames[i].Head.Mid==mid )
				y.push_back( frames[i].Head.Revision );
		}
		return y;
	}
	//The slug's driver server, once its row's change has reached the hosts' thread:  null after `timeout`.
	Ξ WaitHost( str slug, Duration timeout=std::chrono::seconds{15} )ι->sp<DriverHost>{
		sp<DriverHost> host;
		WaitUntil( [&]{ host = DriverHost::Find( slug ); return host!=nullptr; }, timeout );
		return host;
	}
	Ξ WaitNoHost( str slug, Duration timeout=std::chrono::seconds{15} )ι->bool{ return WaitUntil( [&]{ return !DriverHost::Find( slug ); }, timeout ); }
	Ξ DriverNode( sv path )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(ServerCnnctn::DriverBrowseNs, string{path}.c_str()) }; }//a node of a row's server by its path, `Result.Torque`.
	Ξ OpNode( sv name )ι->NodeId{ return DriverNode( Ƒ("_Op.{}", name) ); }
	//`_Op`'s `name` as a client of the slug's reads it from the row's server, `cred`'s registered client or one connected for it.
	Ξ ReadOp( str slug, sv name, Credential cred )ε->Value{
		return ReadValue( BlockAwait(UAClient::GetClient(slug, move(cred))), OpNode(name) );
	}
	Ξ OpText( const Value& value )ι->string{ return value.hasValue && UA_Variant_hasScalarType( &value.value, &UA_TYPES[UA_TYPES_STRING] ) ? ToString( value.Get<UA_String>(0) ) : string{}; }
	Ξ OpFlag( const Value& value )ι->optional<bool>{ return value.hasValue && UA_Variant_hasScalarType( &value.value, &UA_TYPES[UA_TYPES_BOOLEAN] ) ? optional<bool>{ value.Get<UA_Boolean>(0) } : nullopt; }
	Ξ OpCount( const Value& value )ι->optional<uint>{
		if( value.hasValue && UA_Variant_hasScalarType(&value.value, &UA_TYPES[UA_TYPES_UINT32]) )
			return value.Get<UA_UInt32>( 0 );
		return value.hasValue && UA_Variant_hasScalarType( &value.value, &UA_TYPES[UA_TYPES_UINT64] ) ? optional<uint>{ value.Get<UA_UInt64>(0) } : nullopt;
	}

	//A row for the suite's controller, or the one at `url`, made while the gateway runs, with its server listening:  purged as it goes.
	struct DriverRow final{
		DriverRow( string slug, sv driver={}, string url=ControllerUrl() )ε:Slug{ move(slug) }{
			PurgeServerCnnctn( Slug );
			let created = CreateDriverRow( Slug, driver, move(url) );
			THROW_IF( !created, "{}", created.error() );
			Id = *created;
			if( !WaitHost(Slug) ){
				PurgeServerCnnctn( Id );
				THROW( "the server of '{}' never started.", Slug );
			}
		}
		DriverRow( const DriverRow& )=delete;
		~DriverRow(){
			PurgeServerCnnctn( Id );
			WaitNoHost( Slug );
		}
		string Slug;
		ServerCnnctnPK Id;
	};
	//A PF6000 station that answers every revision the codec has, and numbers its messages.
	Ξ Station()ι->OpenProtocol::Controller::Script{
		return { .Identity={ .CellId=1, .ChannelId=2, .ControllerName="Line 3 station 7", .SupplierCode="ACT", .OpenProtocolVersion="2.20.1", .ControllerSoftwareVersion="3.11.5", .ToolSoftwareVersion="1.4.2",
			.RbuType="Gold", .ControllerSerialNumber="C00123456", .SystemType=3, .SystemSubtype=1, .SequenceNumberSupport=1, .LinkingSupport=0, .StationId=7, .StationName="Station 7", .ClientId=1 } };
	}
	Ξ Op( const DriverRow& row, sv name )ε->Value{ return ReadOp( row.Slug, name, SessionCredential() ); }//as a web session's client reads it.
	//Until `_Op`'s `name` is as `is` has it;  a read the row's server is not there for is one that is not yet.
	Ξ WaitOp( const DriverRow& row, sv name, function<bool(const Value&)> is, Duration timeout=std::chrono::seconds{15} )ι->bool{
		return WaitUntil( [&]{
			try{
				return is( Op(row, name) );
			}
			catch( const std::exception& ){
				return false;
			}
		}, timeout );
	}
	Ξ WaitConnected( const DriverRow& row, bool connected=true )ι->bool{ return WaitOp( row, "Connected", [=]( let& v ){ return OpFlag( v )==connected; } ); }
	//The link down until Mend, as through a network's fault, while the controller's stations go on:  the controller drops its
	//client and refuses each MID 0001 after.
	Ξ Sever( OpenProtocol::Controller& station, const DriverRow& row )ι->bool{
		station.Edit( []( OpenProtocol::Controller::Script& script ){ script.Refuses[1] = 16; } );
		station.Drop();
		return WaitConnected( row, false );
	}
	Ξ Mend( OpenProtocol::Controller& station )ι->void{ station.Edit( []( OpenProtocol::Controller::Script& script ){ script.Refuses.erase( 1 ); } ); }
}