#pragma once
#include <jde/opc/usings.h>
#include "Owned.h"

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

		α ToJson()ε->jvalue;
		//The one per-element serializer:  Value::ToJson is its status rules around a loop over this.
		Ω ElementToJson( const void* element, const UA_DataType& type )ε->jvalue;
	};
}