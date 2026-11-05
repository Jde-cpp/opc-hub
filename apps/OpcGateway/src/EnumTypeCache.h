#pragma once
#include "LazyCache.h"

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	struct EnumField{ _int Value; string Name; string Description; };
	enum class EEnumSource : uint8{ Definition, EnumValues, EnumStrings };
	//An enumeration as its DataType node defines it (EnumTypeCache).
	struct EnumType{
		NodeId Id;
		string Name;			//the DataType node's DisplayName text, e.g. DeviceHealthEnumeration.
		vector<EnumField> Fields;	//server order.
		EEnumSource Source{};
		α ToJson( const QL::TableQL& q )Ι->jobject;	//{name, enumValues:[{id,name,description}]} projected by the query's columns.
	};
	//The per-connection enumeration definitions behind `__type( opc, ns, i ){ name enumValues{id name description} }` (ql/GatewayQLAwait).
	//These used to be hand-copied from the companion specs into config/introspection/*.jsonnet keyed by namespace *index*, so they
	//fit exactly one server's namespace layout and the `opc` argument was ignored.  The server carries every enum on its DataType
	//node - the DataTypeDefinition attribute (1.04+), else the EnumValues/EnumStrings property - so read it there on first use
	//and keep it for the connection's life.  Owned by the UAClient, so a disconnect/TTL drop discards it with the client.
	struct EnumTypeCache final : LazyCache<EnumType>{
		α Refused( const NodeId& id )Ι->bool;	//a Failed entry:  the server's own answer that id is no enumeration, which Get rethrows as it rethrows a lost one.
		Ω SourceName( EEnumSource s )ι->sv;
	private:
		//Failed:  a permanent answer (not an enum, no definition anywhere) - re-thrown to every later caller without another round trip.
		α Fetch( sp<UAClient> client, NodeId id )ι->VoidTask override;

		Ω FromDefinition( const UA_EnumDefinition& def )ι->vector<EnumField>;
		Ω FromEnumValues( const UA_Variant& v )ε->vector<EnumField>;	//EnumValueType[] - open62541 unwraps the ExtensionObjects since the type is in UA_TYPES.
		Ω FromEnumStrings( const UA_Variant& v )ε->vector<EnumField>;	//LocalizedText[], the value is the index.
	};
}