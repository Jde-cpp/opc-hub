#pragma once
#include "LazyCache.h"

namespace Jde::QL{ struct TableQL; }
namespace Jde::Opc::Gateway{
	//One of a method's InputArguments or OutputArguments:  its name, data type, value rank and description, as the property has it,
	//and the namespace-0 type its data type derives from.
	struct Argument final : Owned<UA_Argument, UA_TYPES_ARGUMENT>{
		using Owned::Owned;
		α ToJson( const QL::TableQL& q )Ι->jobject;//{name, dataType, baseDataType, valueRank, arrayDimensions, description} projected by the query's columns.
		//The built-in type a json argument converts toward, from Base (ValueTypes::BuiltIn):  Int32 for an enumeration, ExtensionObject -
		//not Settable - for a structure or a union;  null for an abstract type, or one whose base is unknown, which leaves the json to
		//imply the type (Value::Set).
		α BuiltIn()Ι->const UA_DataType*;
		NodeId Base;//the data type itself for one of namespace 0, else its first supertype there (BaseTypeCache);  null where the server names none.
	};
	//A method's arguments, read from its InputArguments and OutputArguments properties - a method without one takes, or answers,
	//none - and its DisplayName:  what `call` types the arguments from, and `method( opc, id )` answers the web's dialog with.
	struct MethodArguments final{
		NodeId Id;
		string Name;
		vector<Argument> Inputs, Outputs;
		α ToJson( const QL::TableQL& q )Ι->jobject;//{name, inputArguments:[…], outputArguments:[…]} by the query's columns and sub-tables.
	};
	//The per-connection method arguments behind `call` and `method` (ql/CallQLAwait, ql/GatewayQLAwait), read on first use and kept for
	//the connection's life, as the enumeration definitions are (EnumTypeCache):  the property is the server's declaration, and a
	//call needs it typed before it goes.  Owned by the UAClient, so a disconnect/TTL drop discards it with the client.
	struct MethodArgumentCache final : LazyCache<MethodArguments>{
	private:
		//Failed:  the server's own answer that the id is no method, re-thrown to every later caller;  a lost session or an unknown id empties the slot instead.
		α Fetch( sp<UAClient> client, NodeId id )ι->VoidTask override;
	};
}