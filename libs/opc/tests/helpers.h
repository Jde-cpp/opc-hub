#pragma once
//Builders shared by the suites:  variants and data values holding a copy of a raw vendor value, scalar or array.
#include <jde/opc/uatypes/Value.h>

namespace Jde::Opc::Tests{
	Ξ scalarVariant( const void* value, const UA_DataType& type )ι->UA_Variant{
		UA_Variant v{};
		UA_Variant_setScalarCopy( &v, value, &type );
		return v;
	}
	Ξ arrayVariant( const void* values, uint count, const UA_DataType& type )ι->UA_Variant{
		UA_Variant v{};
		UA_Variant_setArrayCopy( &v, values, count, &type );
		return v;
	}
	Ξ dataValue( const void* value, const UA_DataType& type )ι->Value{ return Value{ scalarVariant(value, type) }; }
	Ξ arrayDataValue( const void* values, uint count, const UA_DataType& type )ι->Value{ return Value{ arrayVariant(values, count, type) }; }
}