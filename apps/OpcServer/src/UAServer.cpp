#include "UAServer.h"
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/process/thread.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include <NodesetLoader/backendOpen62541.h>
#include "UATrust.h"


#define let const auto
namespace Jde::Opc::Server {
	UAServer::UAServer()ε:
		ServerName{ Settings::FindString("/opcServer/name").value_or("OpcServer") },
		_ua{ UA_Server_newWithConfig(&_config) }
	{
		if( _ua )
			UATrust::Install( *_ua );//must target the server-owned config - UA_Server_newWithConfig memset _config. Main thread, before Run() spawns the loop thread.
	}
	UAServer::~UAServer(){
		INFOT( ELogTags::App, "Stopping OPC UA server..." );
		if( _thread.has_value() ){
			_running = false;
#ifdef _MSC_VER
			std::this_thread::sleep_for( 1s );
#endif
			_thread->request_stop();
			_thread->join();
			_thread.reset();
		}
		if( _ua ){
			UA_Server_delete( _ua );
			_ua = nullptr;
		}
	}

	α UAServer::Run()ε->void{
		if( _thread )
			return;
		UAε( UA_Server_run_startup(_ua) );
		_running = true;//set before spawning: otherwise a destructor racing an early Run() could clear it before the thread reads it, and join() would hang.
		_thread = std::jthread{ [this](std::stop_token st){
			Thread::SetName( "UAServer" );
			//UA_Server_run(...) demands a volatile UA_Boolean*; run the loop manually against a std::atomic instead. This is what UA_Server_run does internally, and it also drops the original's double run_shutdown.
			while( _running && !st.stop_requested() )
				UA_Server_run_iterate( _ua, true );
			UA_Server_run_shutdown( _ua );//the delete is the destructor's, after it joins this thread.
			INFOT( ELogTags::App, "OPC UA server stopped." );
		}};
	}
	α UAServer::Load( fs::path configFile, SL sl )ε->void{
		INFOT( ELogTags::App, "Loading configuration from: '{}'", configFile.string() );
		CHECK_PATH( configFile, sl );
		//xmlSetGenericErrorFunc( nullptr, myXmlError );
		auto success = NodesetLoader_loadFile( _ua, configFile.string().c_str(), nullptr );
		THROW_IFSL( !success, "Failed to load nodeset file: '{}'", configFile.string() );
		PublishDataTypes();//per file, not once at the end:  a later nodeset's variables carry <Value>s typed by an earlier one's enums, and those writes are type-checked as the node is added.
	}

	//The nodeset loader files every type it reads through UA_Server_addDataTypeFromDescription, which lands in the server's
	//own internal list, while PubSub's readers and writers decode and encode with `config.customDataTypes` *only*
	//(ua_pubsub_reader.c, ua_pubsub_writer.c, ua_pubsub_readergroup.c, 1.5.9).  Publish the internal lists through
	//config.customDataTypes so they see nodeset-defined types too - call it once the last nodeset is loaded, since this is
	//a snapshot.  Until 1.5.9 the attribute write path had the same gap:  adjustValueType() never widened a client's Int32
	//back to a nodeset-defined enum, so every write to e.g. DeviceHealth (ns=2;i=6244, DI) was BadTypeMismatch;  it now
	//looks through serverCustomTypes(), the internal lists included.
	//
	//Mirror nodes, not the internal head itself:  serverCustomTypes() hangs config.customDataTypes off the *end* of the
	//internal list every time it is asked, so handing it back its own head would tie the chain into a cycle and spin the
	//first lookup that misses.  The mirrors borrow open62541's `types` arrays, hence cleanup=false - UA_ServerConfig_clean
	//walks them and frees nothing.
	α UAServer::PublishDataTypes()ι->void{
		if( !_ua )
			return;
		auto config = UA_Server_getConfig( _ua );
		config->customDataTypes = nullptr;//so a second call enumerates the internal lists alone, not last time's mirrors too.
		vector<const UA_DataTypeArray*> internals;
		for( auto a = UA_Server_getDataTypes(_ua); a; a = a->next )
			internals.push_back( a );

		_customTypes.clear();
		_customTypes.reserve( internals.size() );//no growth, so the addresses taken below stay put.
		UA_DataTypeArray* next{};
		for( auto p = internals.rbegin(); p!=internals.rend(); ++p ){//backwards: `next` has to exist before the node pointing at it.
			_customTypes.push_back( UA_DataTypeArray{ .next=next, .typesSize=(*p)->typesSize, .types=(*p)->types, .cleanup=false } );
			next = &_customTypes.back();
		}
		config->customDataTypes = next;
		uint count{}; for( let& a : _customTypes ) count += a.typesSize;
		INFOT( ELogTags::App, "Published {} custom data types in {} lists.", count, _customTypes.size() );
	}

	α UAServer::Namespaces()ι->flat_map<uint,string>{
		flat_map<uint,string> y;
		UA_String ns;
		for( uint i=0; !UA_Server_getNamespaceByIndex(_ua, i, &ns); ++i ){
			y.emplace( i, Opc::ToString(ns) );
			UA_String_clear( &ns );
		}
		return y;
	}
}
