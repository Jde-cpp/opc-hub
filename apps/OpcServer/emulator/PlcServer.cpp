#include "PlcServer.h"
#include <cmath>
#include <NodesetLoader/backendOpen62541.h>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/settings.h>
#include <jde/app/IApp.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/BrowsePath.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <jde/opc/server/UAAccess.h>

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ ELogTags::App };//the emulator's own lines, as Emulator.cpp:  the uaServer tag is open62541's, which the configs quiet.

	α PlcServer::ReadSettings( const Crypto::CryptoSettings& deviceSsl, sv root )ε->Server::UASettings{
		let key = [&]( sv name ){ return Ƒ( "{}/{}", root, name ); };
		let ssl = Settings::FindObject( key("ssl") );
		Server::UASettings y{ .Ssl{ ssl ? Crypto::CryptoSettings{*ssl} : deviceSsl } };
		y.ApplicationName = Ƒ( "Jde-Cpp PlcEmulator [{}]", App::InstanceName() );
		y.Port = Settings::FindNumber<PortType>( key("port") ).value_or( 4842 );
		y.Address = Settings::FindString( key("address") ).value_or( "127.0.0.1" );//a dev tool's endpoint:  every interface asks Windows' firewall on each run.
		for( let& dir : Settings::FindStringArray(key("trustedCertDirs")) )
			y.TrustedCertDirs.emplace_back( dir );
		let own = Process::AppDataFolder()/"ssl";
		if( y.TrustedCertDirs.empty() )
			y.TrustedCertDirs.push_back( own/"clients" );
		y.RejectedCertDir = Settings::FindPath( key("rejectedCertDir") ).value_or( own/"rejected" );
		y.None = Settings::FindBool( key("none") ).value_or( y.None );
		y.ReadTokens( root );
		y.ReadLimits( root );
		return y;
	}

	PlcServer::PlcServer( const Server::UASettings& settings, sp<App::Client::IAppClient> appClient, const fs::path& nodeset, optional<PubSub::Config>&& contract, SL sl )ε:
		_port{ settings.Port },
		_contract{ move(contract) },
		_server{ mu<Server::UAServer>(settings, move(appClient), ms<PlcNodeAccess>(), nullptr) }{
		CHECK_PATH( nodeset, sl );
		UA_Server& ua = *_server;
		THROW_IFSL( !NodesetLoader_loadFile(&ua, nodeset.string().c_str(), nullptr), "Could not load nodeset '{}'.", nodeset.string() );
		if( _contract )
			_contract->Resolve( ua, sl );
		INFO( "PLC server on {}:{} with '{}'{}.", settings.Address.empty() ? "every interface" : settings.Address, _port, nodeset.filename().string(), _contract ? Ƒ(";  publishing {}", _contract->ToString()) : string{} );
		if( settings.Address.empty() && settings.Tokens.Anonymous )
			WARN( "The PLC server takes anonymous logins on every interface - anything on the network can read and write its tags.  Set /emulator/plc/address, or turn tokenTypes.anonymous off." );
	}
	PlcServer::~PlcServer(){
		_writer.reset();//a PubSub component of the server, which goes after it.
		_server.reset();
	}
	α PlcServer::Run()ε->void{
		_server->Run();
		//The writer once the server runs:  it enables the server's PubSub components, and UA_Server_delete asserts every component
		//stopped, which only a run server's shutdown does.  A server never run - the emulator's when its startup fails after the
		//server is built - or whose startup failed - the port taken, which leaves a PubSub channel open until the event loop runs
		//again - has none to strand.  On the running server the writer starts at once.
		if( _contract && !_writer )
			_writer = mu<PubSub::Writer>( *_server, *_contract );
	}
	α PlcServer::Namespace( sv uri, SL sl )ε->NsIndex{ return NamespaceIndex( *_server, uri, sl ); }
	α PlcServer::Resolve( sv path, NsIndex defaultNs, SL sl )ε->NodeId{ return BrowsePath::Resolve( *_server, path, defaultNs, {}, sl ); }

	α PlcServer::FindField( sv name )Ι->optional<uint>{
		for( uint i=0; _contract && i<_contract->Fields.size(); ++i ){
			if( _contract->Fields[i].Name==name )
				return i;
		}
		return {};
	}
	α PlcServer::Write( const NodeId& node, const UA_Variant& value, UA_StatusCode status, SL )ε->void{
		UA_WriteValue write;
		UA_WriteValue_init( &write );
		write.nodeId = node;//borrowed, like the value:  nothing here is cleared.
		write.attributeId = UA_ATTRIBUTEID_VALUE;
		write.value.value = value;
		//The whole DataValue, not UA_Server_writeValue's variant:  the node holds the status with the value, which the writer
		//samples and publishes (PubSub::Writer's field content mask) and a session reads.  The value is always there, Bad or not -
		//the OpcServer's reader skips a field without one (ua_pubsub_reader.c `if(!field->hasValue) continue`), so a valueless Bad
		//would never arrive.  hasStatus always:  the node stores it as written, which is how a reading returns to Good.
		write.value.hasValue = true;
		write.value.status = status;
		write.value.hasStatus = true;
		write.value.sourceTimestamp = UA_DateTime_now();//when the PLC took the reading:  without one open62541 stamps each read with its own time.
		write.value.hasSourceTimestamp = true;
		UAε( UA_Server_write(_server->Ptr(), &write) );//UA_THREADSAFE:  the emulator's loop thread, beside the server's own.
	}

	//`value` as `T`:  a Boolean is nonzero, an integer rounded and held to its range, NaN 0.
	Ṫ to( double value )ι->T{
		if constexpr( std::is_same_v<T,UA_Boolean> )
			return value!=0;
		else if constexpr( std::is_floating_point_v<T> )
			return (T)value;
		else{
			using L = std::numeric_limits<T>;
			let r = std::round( value );
			return std::isnan(r) ? T{} : r<=(double)L::lowest() ? L::lowest() : r>=(double)L::max() ? L::max() : (T)r;
		}
	}
	union Scalar{ UA_Boolean B; UA_SByte I8; UA_Byte U8; UA_Int16 I16; UA_UInt16 U16; UA_Int32 I32; UA_UInt32 U32; UA_Int64 I64; UA_UInt64 U64; UA_Float F; UA_Double D; };
	//`value` as a scalar of `type` in `v`, over `s`;  false for a type the emulator doesn't write.
	Ω setScalar( UA_Variant& v, const UA_DataType& type, double value, Scalar& s )ι->bool{
		switch( type.typeKind ){
		case UA_DATATYPEKIND_BOOLEAN: s.B = to<UA_Boolean>( value ); break;
		case UA_DATATYPEKIND_SBYTE: s.I8 = to<UA_SByte>( value ); break;
		case UA_DATATYPEKIND_BYTE: s.U8 = to<UA_Byte>( value ); break;
		case UA_DATATYPEKIND_INT16: s.I16 = to<UA_Int16>( value ); break;
		case UA_DATATYPEKIND_UINT16: s.U16 = to<UA_UInt16>( value ); break;
		case UA_DATATYPEKIND_INT32: case UA_DATATYPEKIND_ENUM: s.I32 = to<UA_Int32>( value ); break;//an enumeration is an Int32.
		case UA_DATATYPEKIND_UINT32: s.U32 = to<UA_UInt32>( value ); break;
		case UA_DATATYPEKIND_INT64: s.I64 = to<UA_Int64>( value ); break;
		case UA_DATATYPEKIND_UINT64: s.U64 = to<UA_UInt64>( value ); break;
		case UA_DATATYPEKIND_FLOAT: s.F = to<UA_Float>( value ); break;
		case UA_DATATYPEKIND_DOUBLE: s.D = value; break;
		default: return false;
		}
		UA_Variant_setScalar( &v, &s, &type );//every member at the union's address.
		return true;
	}
	α PlcServer::Write( const NodeId& node, const UA_DataType& type, double value, UA_StatusCode status, SL sl )ε->void{
		Scalar s;
		UA_Variant v;
		UA_Variant_init( &v );
		THROW_IFSL( !setScalar(v, type, value, s), "{} is {} - the emulator writes Boolean, an integer, an enumeration, Float or Double.", node.ToString(), type.typeName );
		Write( node, v, status, sl );
	}
	α PlcServer::Write( uint field, double value, UA_StatusCode status, SL sl )ε->void{
		THROW_IFSL( !_contract, "No contract:  the server transport writes by node." );
		let& f = _contract->Fields.at( field );
		Write( f.Node, *f.Type, value, status, sl );
	}
	α PlcServer::ValueType( const NodeId& node, SL sl )ε->const UA_DataType&{
		UA_NodeId id;
		if( let sc = UA_Server_readDataType(_server->Ptr(), node, &id); sc )
			throw UAException{ sc, Ƒ("{}'s DataType", node.ToString()), {}, sl };
		let type = UA_findDataTypeWithCustom( &id, UA_Server_getConfig(_server->Ptr())->customDataTypes );
		UA_NodeId_clear( &id );
		if( !type )
			return UA_TYPES[UA_TYPES_DOUBLE];//abstract:  Number, BaseDataType.
		UA_Variant v;
		Scalar s;
		THROW_IFSL( !setScalar(v, *type, 0, s), "{} is {} - the emulator writes Boolean, an integer, an enumeration, Float or Double.", node.ToString(), type->typeName );
		return *type;
	}

	α PlcServer::Watch( const NodeId& node, uint key, SL sl )ε->optional<bool>{
		{
			lg _{ _mutex };
			THROW_IFSL( !_watched.emplace(node, key).second, "{} is watched already - a command node has one tag.", node.ToString() );
		}
		let v = ReadValue( *_server->Ptr(), node );
		UA_ValueSourceNotifications notifications{ .onRead=nullptr, .onWrite=Written };
		UAε( UA_Server_setNodeContext(_server->Ptr(), node, this) );
		UAε( UA_Server_setVariableNode_internalValueSource(_server->Ptr(), node, nullptr, &notifications) );//the node keeps its value;  each write of it notifies.
		return v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_BOOLEAN]) ? optional{ v.Get<UA_Boolean>(0)!=0 } : nullopt;
	}
	α PlcServer::Written( UA_Server*, const UA_NodeId*, void* sessionContext, const UA_NodeId* nodeId, void* nodeContext, const UA_NumericRange*, const UA_DataValue* data )ι->void{
		auto self = static_cast<PlcServer*>( nodeContext );
		if( !self || !nodeId || !data || !data->hasValue || !UA_Variant_hasScalarType(&data->value, &UA_TYPES[UA_TYPES_BOOLEAN]) )
			return;
		let user = sessionContext ? static_cast<const Server::UAAccess::SessionContext*>( sessionContext )->UserPK : UserPK{};
		lg _{ self->_mutex };
		if( let p = self->_watched.find(NodeId{*nodeId}); p!=self->_watched.end() )
			self->_commands.push_back( {p->second, *(UA_Boolean*)data->value.data!=0, user} );
	}
	α PlcServer::Drain()ι->vector<Command>{
		lg _{ _mutex };
		return std::exchange( _commands, {} );
	}
}