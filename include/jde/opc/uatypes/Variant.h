#pragma once
#include <jde/opc/usings.h>
#include "Owned.h"
#include "Types.h"

namespace Jde::Opc{
	struct Variant : Owned<UA_Variant, UA_TYPES_VARIANT>{
		//dataType null infers the UA type from the json kind; otherwise the json is decoded *toward* the declared type.
		//A `sv` name used to be taken here and only "double" was honoured - resolving the name is the caller's job (see
		//OpcServer's DT()), since the vocabulary is its config schema, not a UA concept.
		Variant( const jvalue& v, const UA_DataType* dataType )ε;
		Variant( const UA_Variant& v )ι:Owned{ v }{}
		Variant( UA_Variant&& v )ι:Owned{ move(v) }{}

		α IsNull()Ι->bool{ return UA_Variant_isEmpty(this); }
		α IsScalar()Ι->bool{ return UA_Variant_isScalar(this); }

		α ToJson( const ITypes* types=nullptr )ε->jvalue;
		//The one per-element serializer:  Value::ToJson is its status rules around a loop over this.  A structure - open62541's own, or one
		//`types` holds (clients spec *Structures*, #421) - goes out as an object of its members by name, each in the shape its kind takes
		//here, nested where the members are, an array member as an array, a union as its one selected member alone;  an ExtensionObject
		//decoded, or encoded with a type one of the two explains, the same, and one no type explains stays opaque, {typeId, body}, its
		//encoding id noted with `types` (ITypes::Missing).
		Ω ElementToJson( const void* element, const UA_DataType& type, const ITypes* types=nullptr )ε->jvalue;
		//`v` with every structure of a type outside UA_TYPES - a connection's registry's, which a row edit drops (Gateway::TypeRegistry) -
		//re-encoded as the ExtensionObject it travelled as, an array of them as an array, at any depth:  a Variant element's, an
		//ExtensionObject's, a DataValue's, a namespace-0 structure's member (KeyValuePair's Value).  What keeps a value past the request,
		//the historian and the endpoint's mirror, then holds no type a registry owns;  one that won't encode is kept as an empty
		//ExtensionObject, never decoded.  nullopt when `v` holds none, so the caller keeps `v` as it is.
		Ω WithBuiltInTypes( const UA_Variant& v )ι->optional<Variant>;
	};
}