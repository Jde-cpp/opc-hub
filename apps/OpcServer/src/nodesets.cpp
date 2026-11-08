#include "nodesets.h"
#include <charconv>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
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

	//pumpManual.toHertz (clients spec *Methods*, #318):  the pumps demo's method, which an install has to try the Call dialog on and
	//the gateway's CallTests call - `rpm` Double in, `hertz` Double and `text` String out.  It has no side effects.  The server has type-checked the inputs against InputArguments by here, so the shape check is
	//a guard, not the rule.  Nothing in it throws - an exception would unwind through the server's C frames - so the text is
	//spelled in place, as std::format would spell it, with no allocation but the copy open62541 makes.
	Ω toHertz( UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void*, const UA_NodeId*, void*, uint inputSize, const UA_Variant* input, uint outputSize, UA_Variant* output )ι->UA_StatusCode{
		if( inputSize!=1 || outputSize!=2 || !UA_Variant_hasScalarType(&input[0], &UA_TYPES[UA_TYPES_DOUBLE]) )
			return UA_STATUSCODE_BADINVALIDARGUMENT;
		const UA_Double hertz = *(const UA_Double*)input[0].data/60;
		if( let sc = UA_Variant_setScalarCopy(&output[0], &hertz, &UA_TYPES[UA_TYPES_DOUBLE]); sc )
			return sc;
		constexpr sv unit{ " Hz" };
		char text[32];//the longest shortest-double is 24 characters
		let [end, ec] = std::to_chars( text, text+sizeof(text)-unit.size(), hertz );
		if( ec!=std::errc{} )
			return UA_STATUSCODE_BADINTERNALERROR;
		std::copy( unit.begin(), unit.end(), end );
		const UA_String s{ (uint)(end-text)+unit.size(), (UA_Byte*)text };
		return UA_Variant_setScalarCopy( &output[1], &s, &UA_TYPES[UA_TYPES_STRING] );
	}
	//pumpManual.raiseEvent (clients spec *Events*, #424):  the pumps demo's event source, which an install tries the Events tab on and the
	//gateway's SubscribeTests raise - `message` String and `severity` UInt16 in, the event's EventId out.  A BaseEventType event from the
	//pump, the one the method is called on, so pumpManual's own events subscription sees it and the Server object's does (Part 3 7.17),
	//with the pump's motorRpm as a field of its own, `Rpm`:  a subscription selecting "Rpm" gets it, the server resolving a field by
	//the key its clause prints, "/Rpm" (ua_subscription_event.c resolveSAO).  Borrowed, so nothing here frees the field;  the output
	//is a copy of the id the server made.  Nothing in it throws, as toHertz doesn't.
	Ω raiseEvent( UA_Server* ua, const UA_NodeId*, void*, const UA_NodeId*, void*, const UA_NodeId* objectId, void*, uint inputSize, const UA_Variant* input, uint outputSize, UA_Variant* output )ι->UA_StatusCode{
		if( inputSize!=2 || outputSize!=1 || !objectId || !UA_Variant_hasScalarType(&input[0], &UA_TYPES[UA_TYPES_STRING]) || !UA_Variant_hasScalarType(&input[1], &UA_TYPES[UA_TYPES_UINT16]) )
			return UA_STATUSCODE_BADINVALIDARGUMENT;
		const UA_LocalizedText message{ UA_STRING_NULL, *(const UA_String*)input[0].data };
		const UA_UInt16 severity = *(const UA_UInt16*)input[1].data;
		const Value rpm = ReadValue( *ua, NodeId{objectId->namespaceIndex, 6054} );//pumpManual.motorRpm;  empty when the read fails, which the field then is.
		UA_KeyValuePair field{ .key={0, UA_STRING((char*)"/Rpm")}, .value=rpm.value };
		const UA_KeyValueMap fields{ 1, &field };
		UA_ByteString eventId; UA_ByteString_init( &eventId );
		if( let sc = UA_Server_createEvent(ua, *objectId, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEEVENTTYPE), severity, message, &fields, nullptr, &eventId); sc )
			return sc;
		let sc = UA_Variant_setScalarCopy( &output[0], &eventId, &UA_TYPES[UA_TYPES_BYTESTRING] );
		UA_ByteString_clear( &eventId );
		return sc;
	}
	//pumpManual.overspeed.Raise (clients spec *Alarms and conditions*, #425):  the demo condition driven by hand - `active` Boolean, `severity`
	//UInt16 and `message` String in, the transition's EventId out, the one Acknowledge and Confirm then take - so the suites, which embed the
	//OpcServer and no emulator, drive it themselves;  the demo's input is motorRpm over HighLimit (Condition::WatchHigh).  The condition is the
	//object called on, found through the server's Conditions.  Nothing in it throws, as raiseEvent doesn't.
	Ω raiseCondition( UA_Server* ua, const UA_NodeId*, void*, const UA_NodeId*, void*, const UA_NodeId* objectId, void*, uint inputSize, const UA_Variant* input, uint outputSize, UA_Variant* output )ι->UA_StatusCode{
		if( inputSize!=3 || outputSize!=1 || !objectId || !UA_Variant_hasScalarType(&input[0], &UA_TYPES[UA_TYPES_BOOLEAN]) || !UA_Variant_hasScalarType(&input[1], &UA_TYPES[UA_TYPES_UINT16]) || !UA_Variant_hasScalarType(&input[2], &UA_TYPES[UA_TYPES_STRING]) )
			return UA_STATUSCODE_BADINVALIDARGUMENT;
		auto conditions = Server::Conditions::Of( *ua );
		auto condition = conditions ? conditions->Find( *objectId ) : nullptr;
		if( !condition )
			return UA_STATUSCODE_BADNODEIDUNKNOWN;
		if( let sc = condition->Raise( *(const UA_Boolean*)input[0].data, *(const UA_UInt16*)input[1].data, ToSV(*(const UA_String*)input[2].data) ); sc )
			return sc;
		const UA_ByteString eventId{ condition->LastEventId().size(), (UA_Byte*)condition->LastEventId().data() };
		return UA_Variant_setScalarCopy( &output[0], &eventId, &UA_TYPES[UA_TYPES_BYTESTRING] );
	}
	//The pumps demo after its nodeset loads.  pumpManual.toHertz's callback - a method without one answers BadInternalError (clients spec
	//*Methods*, #318) - and raiseEvent's (clients spec *Events*, #424), whose EventNotifier bit the nodeset declares and the loader
	//applies.  And a WriteMask of DisplayName and Description on pumpManual:  the nodeset declares none and the loader reads none
	//(open62541-nodeset-loader Nodeset.c), so without it no write to an attribute could pass whatever the user holds - this is the node an
	//install can try the Attributes tab's edit on, and the one the gateway's WriteTests write (clients spec *Attributes*, #417);  the rights
	//mask then gates the write on Update (UAAccess::GetUserRightsMask).
	α Server::ConfigurePumps( UAServer& ua )ι->void{
		uint ns{};
		if( UA_Server_getNamespaceByName(ua.Ptr(), UA_STRING((char*)"urn:jde:pumps"), &ns) )
			return;//not loaded here.
		const NodeId method{ (NsIndex)ns, 6055 };//pumpManual.toHertz, as the nodeset numbers it.
		if( let sc = UA_Server_setMethodNodeCallback(ua.Ptr(), method, &toHertz); sc )
			WARNT( ELogTags::App, "Could not bind pumpManual.toHertz ({}):  {}", method.ToString(), UA_StatusCode_name(sc) );
		const NodeId raise{ (NsIndex)ns, 6058 };//pumpManual.raiseEvent.
		if( let sc = UA_Server_setMethodNodeCallback(ua.Ptr(), raise, &raiseEvent); sc )
			WARNT( ELogTags::App, "Could not bind pumpManual.raiseEvent ({}):  {}", raise.ToString(), UA_StatusCode_name(sc) );
		const NodeId pumpManual{ (NsIndex)ns, 5005 };
		if( let sc = UA_Server_writeWriteMask(ua.Ptr(), pumpManual, UA_WRITEMASK_DISPLAYNAME | UA_WRITEMASK_DESCRIPTION); sc )
			WARNT( ELogTags::App, "Could not set pumpManual's WriteMask ({}):  {}", pumpManual.ToString(), UA_StatusCode_name(sc) );
		//pumpManual.overspeed (clients spec *Alarms and conditions*, #425):  the demo's alarm condition, declared in the nodeset and taken over by
		//the server library (Conditions::Add), raised from pumpManual, driven by motorRpm over its HighLimit property and by its Raise method.
		const NodeId overspeed{ (NsIndex)ns, 5020 }, highLimit{ (NsIndex)ns, 6106 }, raiseCondition_{ (NsIndex)ns, 6110 };
		try{
			auto& condition = ua.Conditions().Add( *ua.Ptr(), overspeed, pumpManual );
			condition.WatchHigh( NodeId{(NsIndex)ns, 6054}, highLimit, 700 );
			UAε( UA_Server_setMethodNodeCallback(ua.Ptr(), raiseCondition_, &raiseCondition) );
		}
		catch( const runtime_error& e ){
			WARNT( ELogTags::App, "Could not add pumpManual.overspeed ({}):  {}", overspeed.ToString(), e.what() );
		}
		//pumpManual._Test.sample's value, a PumpSampleType (clients spec *Structures*, #421):  the type as the loader filed it, its members
		//from json as a gateway write would set them (Value::Set).  The nodeset carries no value - the loader's xml decode stops short of a
		//structure of the nodeset's own.
		const NodeId sampleType{ (NsIndex)ns, 3001 }, sample{ (NsIndex)ns, 6061 };
		let type = UA_Server_findDataType( ua.Ptr(), &sampleType );
		if( !type )
			WARNT( ELogTags::App, "The pumps' PumpSampleType ({}) is not among the server's types - _Test.sample stays empty.", sampleType.ToString() );
		if( type ){//not `else`:  the log macros expand to their own `if`.
			try{
				const Value value{ parse(R"({"Rpm":1480.5,"Running":true,"Label":"sample","Limits":{"Low":0,"High":3000},"History":[1470,1475,1480.5]})"), type };
				if( let sc = UA_Server_writeValue(ua.Ptr(), sample, value.value); sc )
					WARNT( ELogTags::App, "Could not write _Test.sample ({}):  {}", sample.ToString(), UA_StatusCode_name(sc) );
			}
			catch( const std::exception& e ){
				WARNT( ELogTags::App, "Could not build _Test.sample's value:  {}", e.what() );
			}
		}
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