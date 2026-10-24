#include "PlcServer.h"
#include <NodesetLoader/backendOpen62541.h>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/process/process.h>
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
		y.Address = Settings::FindString( key("address") ).value_or( "" );
		for( let& dir : Settings::FindStringArray(key("trustedCertDirs")) )
			y.TrustedCertDirs.emplace_back( dir );
		let own = Process::AppDataFolder()/"ssl";
		if( y.TrustedCertDirs.empty() )
			y.TrustedCertDirs.push_back( own/"clients" );
		y.RejectedCertDir = Settings::FindPath( key("rejectedCertDir") ).value_or( own/"rejected" );
		y.None = Settings::FindBool( key("none") ).value_or( y.None );
		y.ReadTokens( root );
		y.Limit.MaxSessions = Settings::FindNumber<uint16>( key("limits/maxSessions") );
		y.Limit.MaxSecureChannels = Settings::FindNumber<uint16>( key("limits/maxSecureChannels") );
		y.Limit.MaxMonitoredItems = Settings::FindNumber<uint32>( key("limits/maxMonitoredItems") );
		y.Limit.SessionTimeout = Settings::FindDuration( key("limits/sessionTimeout") );
		y.Limit.MinSamplingInterval = Settings::FindDuration( key("limits/minSamplingInterval") );
		y.Limit.MaxAsyncOperations = Settings::FindNumber<uint>( key("limits/maxAsyncOperations") );
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
	}
	PlcServer::~PlcServer(){
		_writer.reset();//a PubSub component of the server, which goes after it.
		_server.reset();
	}
	α PlcServer::Run()ε->void{
		//The writer here, not in the constructor:  it starts the server's PubSub component, and UA_Server_delete asserts every
		//component stopped, which only a run server's shutdown does - a server built and never run, the emulator's when its
		//startup fails after the server is built, has to delete clean.
		if( _contract && !_writer )
			_writer = mu<PubSub::Writer>( *_server, *_contract );
		_server->Run();
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
		UAε( UA_Server_write(_server->Ptr(), &write) );//UA_THREADSAFE:  the emulator's loop thread, beside the server's own.
	}
	α PlcServer::Write( uint field, double value, UA_StatusCode status, SL sl )ε->void{
		THROW_IFSL( !_contract, "No contract:  the server transport writes by node." );
		let& f = _contract->Fields.at( field );
		UA_Boolean b = value!=0;
		UA_Float x = (UA_Float)value;
		UA_Variant v;
		UA_Variant_init( &v );
		if( f.Type==&UA_TYPES[UA_TYPES_BOOLEAN] )
			UA_Variant_setScalar( &v, &b, f.Type );
		else if( f.Type==&UA_TYPES[UA_TYPES_FLOAT] )
			UA_Variant_setScalar( &v, &x, f.Type );
		else{
			THROW_IFSL( f.Type!=&UA_TYPES[UA_TYPES_DOUBLE], "Field '{}' is {} - the emulator writes Boolean, Float or Double.", f.Name, f.Type->typeName );
			UA_Variant_setScalar( &v, &value, f.Type );
		}
		Write( f.Node, v, status, sl );
	}

	α PlcServer::Watch( const NodeId& node, uint key, SL )ε->void{
		{
			lg _{ _mutex };
			_watched.emplace( node, key );
		}
		UA_ValueSourceNotifications notifications{ .onRead=nullptr, .onWrite=Written };
		UAε( UA_Server_setNodeContext(_server->Ptr(), node, this) );
		UAε( UA_Server_setVariableNode_internalValueSource(_server->Ptr(), node, nullptr, &notifications) );//the node keeps its value;  each write of it notifies.
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