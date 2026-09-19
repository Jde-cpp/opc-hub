#include "PlcServer.h"
#include <NodesetLoader/backendOpen62541.h>
#include <jde/opc/uatypes/Logger.h>
#include <jde/fwk/exceptions/IOException.h>

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::PubSub };
	static Opc::Logger _logger{};

	PlcServer::PlcServer( UA_UInt16 port, sv bind, const fs::path& nodeset, PubSub::Config&& contract, SL sl )ε:
		_port{ port },
		_contract{ move(contract) }{
		UA_ServerConfig config{};
		config.logging = &_logger;
		let sc = UA_ServerConfig_setMinimal( &config, port, nullptr ); THROW_IFX( sc, UAException(sc, "UA_ServerConfig_setMinimal", {}, sl) );
		//setMinimal leaves "opc.tcp://:<port>" - an empty hostname, i.e. "Listen on all interfaces (also external)"
		//(ua_config_default.c) - alongside UA_AccessControl_default( allowAnonymous=true ), over a nodeset whose
		//motorRpm/status nodes carry AccessLevel 3.  That is a second, unauthenticated door onto the same tags the
		//OpcServer serves, on a server nothing is meant to connect to at all, so bind loopback.  It must happen AFTER
		//setMinimal: setMinimal deletes whatever url it finds and logs "ServerUrls already set. Overriding.".  The
		//anonymous access control stays - its startup warnings then describe a loopback-only server.
		let url = bind.empty() ? Ƒ("opc.tcp://:{}", port) : Ƒ("opc.tcp://{}:{}", bind, port);
		UA_Array_delete( config.serverUrls, config.serverUrlsSize, &UA_TYPES[UA_TYPES_STRING] );
		config.serverUrls = nullptr;
		config.serverUrlsSize = 0;
		UA_String urls[1]{ UA_String{url.size(), (UA_Byte*)url.data()} };//a view: UA_Array_copy deep-copies.
		let urlSc = UA_Array_copy( urls, 1, (void**)&config.serverUrls, &UA_TYPES[UA_TYPES_STRING] ); THROW_IFX( urlSc, UAException(urlSc, Ƒ("serverUrls '{}'", url), {}, sl) );
		config.serverUrlsSize = 1;
		_server = UA_Server_newWithConfig( &config );
		THROW_IFSL( !_server, "UA_Server_newWithConfig failed." );
		try{
			CHECK_PATH( nodeset, sl );
			THROW_IFSL( !NodesetLoader_loadFile(_server, nodeset.string().c_str(), nullptr), "Could not load nodeset '{}'.", nodeset.string() );
			_contract.Resolve( *_server, sl );
			_writer = mu<PubSub::Writer>( *_server, _contract, sl );
			let startup = UA_Server_run_startup( _server ); THROW_IFX( startup, UAException(startup, "UA_Server_run_startup", {}, sl) );
		}
		catch( ... ){
			UA_Server_delete( _server );
			_server = nullptr;
			throw;
		}
		INFO( "PLC server up on {} with '{}'; publishing {}.", url, nodeset.filename().string(), _contract.ToString() );
		if( bind.empty() )
			WARN( "The PLC's own UA server is bound to every interface with anonymous access control over a writable nodeset - anything on the network can write its tags.  Clear /emulator/plc/bind only for a deliberately LAN-visible demo." );
	}
	PlcServer::~PlcServer(){
		if( _server ){
			UA_Server_run_shutdown( _server );
			UA_Server_delete( _server );
		}
	}
	α PlcServer::Iterate()ι->void{
		UA_Server_run_iterate( _server, false );
	}
	α PlcServer::FindField( sv name )Ι->optional<uint>{
		for( uint i=0; i<_contract.Fields.size(); ++i ){
			if( _contract.Fields[i].Name==name )
				return i;
		}
		return {};
	}
	α PlcServer::Write( uint field, double value, UA_StatusCode status )ε->void{
		let& f = _contract.Fields.at( field );
		UA_Boolean b = value!=0;
		UA_Float x = (UA_Float)value;
		UA_WriteValue write;
		UA_WriteValue_init( &write );
		write.nodeId = f.Node;//borrowed, like the scalars: nothing here is cleared.
		write.attributeId = UA_ATTRIBUTEID_VALUE;
		if( f.Type==&UA_TYPES[UA_TYPES_BOOLEAN] )
			UA_Variant_setScalar( &write.value.value, &b, f.Type );
		else if( f.Type==&UA_TYPES[UA_TYPES_FLOAT] )
			UA_Variant_setScalar( &write.value.value, &x, f.Type );
		else{
			THROW_IF( f.Type!=&UA_TYPES[UA_TYPES_DOUBLE], "Field '{}' is {} - the emulator writes Boolean, Float or Double.", f.Name, f.Type->typeName );
			UA_Variant_setScalar( &write.value.value, &value, f.Type );
		}
		//The whole DataValue, not UA_Server_writeValue's variant: the writer samples the node's DataValue and publishes its
		//status with it (PubSub::Writer's field content mask).  The value is always there, Bad or not - the OpcServer's
		//reader skips a field without one (ua_pubsub_reader.c `if(!field->hasValue) continue`), so a valueless Bad would
		//never arrive.  hasStatus always: the node stores it as written, which is how a reading returns to Good.
		write.value.hasValue = true;
		write.value.status = status;
		write.value.hasStatus = true;
		UAε( UA_Server_write(_server, &write) );
	}
}
