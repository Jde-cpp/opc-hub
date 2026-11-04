#include "nodesets.h"
#include <jde/fwk/exceptions/IOException.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/server/UAServer.h>
#include <NodesetLoader/backendOpen62541.h>

#define let const auto
namespace Jde::Opc{
	α Server::LoadNodeset( UAServer& ua, fs::path configFile, SL sl )ε->void{
		INFOT( ELogTags::App, "Loading configuration from: '{}'", configFile.string() );
		CHECK_PATH( configFile, sl );
		auto success = NodesetLoader_loadFile( ua.Ptr(), configFile.string().c_str(), nullptr );
		THROW_IFSL( !success, "Failed to load nodeset file: '{}'", configFile.string() );
	}

	//pumpManual.toHertz (clients spec *Methods*, #318):  the test method the gateway's CallTests call - `rpm` Double in, `hertz`
	//Double and `text` String out.  The server has type-checked the inputs against InputArguments by here, so the shape check is
	//a guard, not the rule.
	Ω toHertz( UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void*, const UA_NodeId*, void*, size_t inputSize, const UA_Variant* input, size_t outputSize, UA_Variant* output )->UA_StatusCode{
		if( inputSize!=1 || outputSize!=2 || !UA_Variant_hasScalarType(&input[0], &UA_TYPES[UA_TYPES_DOUBLE]) )
			return UA_STATUSCODE_BADINVALIDARGUMENT;
		const UA_Double hertz = *(const UA_Double*)input[0].data/60;
		if( let sc = UA_Variant_setScalarCopy(&output[0], &hertz, &UA_TYPES[UA_TYPES_DOUBLE]); sc )
			return sc;
		let text = Ƒ( "{} Hz", hertz );
		const UA_String s = UA_STRING( (char*)text.c_str() );
		return UA_Variant_setScalarCopy( &output[1], &s, &UA_TYPES[UA_TYPES_STRING] );
	}
	α Server::BindPumpMethods( UAServer& ua )ι->void{
		size_t ns{};
		if( UA_Server_getNamespaceByName(ua.Ptr(), UA_STRING((char*)"urn:jde:pumps"), &ns) )
			return;//not loaded here.
		const NodeId method{ (NsIndex)ns, 6055 };//pumpManual.toHertz, as the nodeset numbers it.
		if( let sc = UA_Server_setMethodNodeCallback(ua.Ptr(), method, &toHertz); sc )
			WARNT( ELogTags::App, "Could not bind pumpManual.toHertz ({}):  {}", method.ToString(), UA_StatusCode_name(sc) );
	}

	//The nodeset loader files every type it reads through UA_Server_addDataTypeFromDescription, which lands in the server's
	//own internal list, while PubSub's readers and writers decode and encode with `config.customDataTypes` *only*
	//(ua_pubsub_reader.c, ua_pubsub_writer.c, ua_pubsub_readergroup.c, 1.5.9).  Publish the internal lists through
	//config.customDataTypes so they see nodeset-defined types too.  It is a snapshot, so startup takes it once, after the
	//last nodeset:  taken per LoadNodeset, every lookup that missed in a later one scanned each type twice - the internal lists,
	//then their mirrors (open62541-1.5.9 review #11).  Until 1.5.9 the attribute write path had the same gap:  adjustValueType()
	//never widened a client's Int32 back to a nodeset-defined enum, so every write to e.g. DeviceHealth was BadTypeMismatch;
	//it now looks through serverCustomTypes(), the internal lists included.  Today's PubSub contracts carry built-in types
	//only (PubSub::Config), so nothing reads the snapshot yet.
	//
	//Mirror nodes, not the internal head itself:  serverCustomTypes() hangs config.customDataTypes off the *end* of the
	//internal list every time it is asked, so handing it back its own head would tie the chain into a cycle and spin the
	//first lookup that misses.  The mirrors borrow open62541's `types` arrays, hence cleanup=false - UA_ServerConfig_clean
	//walks them and frees nothing, which is why they are kept on the server (UAServer::CustomTypes), not here.
	α Server::PublishDataTypes( UAServer& ua )ι->void{
		auto config = UA_Server_getConfig( ua.Ptr() );
		config->customDataTypes = nullptr;//so a second call enumerates the internal lists alone, not last time's mirrors too.
		vector<const UA_DataTypeArray*> internals;
		for( auto a = UA_Server_getDataTypes(ua.Ptr()); a; a = a->next )
			internals.push_back( a );

		auto& customTypes = ua.CustomTypes();
		customTypes.clear();
		customTypes.reserve( internals.size() );//no growth, so the addresses taken below stay put.
		UA_DataTypeArray* next{};
		for( auto p = internals.rbegin(); p!=internals.rend(); ++p ){//backwards: `next` has to exist before the node pointing at it.
			customTypes.push_back( UA_DataTypeArray{ .next=next, .typesSize=(*p)->typesSize, .types=(*p)->types, .cleanup=false } );
			next = &customTypes.back();
		}
		config->customDataTypes = next;
		uint count{}; for( let& a : customTypes ) count += a.typesSize;
		INFOT( ELogTags::App, "Published {} custom data types in {} lists.", count, customTypes.size() );
	}
}