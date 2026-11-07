#pragma once
#include <jde/opc/usings.h>

namespace Jde::Opc{
	//The structure types a value is decoded by (clients spec *Structures*, #421):  a connection's registry of the types its server defines
	//(Gateway::TypeRegistry), chained as open62541 chains custom types, which the serializers resolve an encoded body against - the
	//attribute reads, updateVariable's answer and the history rows.  Namespace 0's own, UA_TYPES, come before the chain.  A serializer
	//given none, `nullptr`, decodes open62541's own structures and leaves every other body opaque.
	struct ITypes{
		virtual ~ITypes()=default;
		//The arrays:  each lives as long as this does and is never changed, only joined by a newer one ahead of it, so a reader walks it
		//off any thread.
		β Chain()Ι->const UA_DataTypeArray* =0;
		//A value named an encoding id no type here explains - the serializer's note, which a registry answers with a fetch of its own,
		//posted, never on the caller's thread.
		β Missing( const UA_NodeId& /*encodingId*/ )Ι->void{}
	};
	//The type an encoding id names:  UA_TYPES' by its DefaultBinary id, else `types`' chain.  Null for one neither holds.
	α FindEncoding( const UA_NodeId& encodingId, const ITypes* types )ι->const UA_DataType*;
	//The type a DataType id names, the same way.
	α FindDataType( const UA_NodeId& typeId, const ITypes* types )ι->const UA_DataType*;
	//One of open62541's own, which outlives every registry.
	Ξ IsBuiltIn( const UA_DataType* type )ι->bool{ return type>=UA_TYPES && type<UA_TYPES+UA_TYPES_COUNT; }
	//A kind with members:  a structure, one with optional members, or a union.
	Ξ IsStructure( const UA_DataType& type )ι->bool{ return type.typeKind==UA_DATATYPEKIND_STRUCTURE || type.typeKind==UA_DATATYPEKIND_OPTSTRUCT || type.typeKind==UA_DATATYPEKIND_UNION; }
	//An encoded body decoded by its encoding id against UA_TYPES and `types`' chain (FindEncoding):  the value, freed with this, and its
	//type.  None when no type explains the id - which is noted with `types` for its fetch (Missing), unless namespace 0's, which no fetch
	//explains - or when the body won't decode, which is logged.
	struct DecodedBody final{
		DecodedBody()ι=default;
		DecodedBody( void* data, const UA_DataType* type )ι:Data{ data }, Type{ type }{}
		DecodedBody( DecodedBody&& x )ι:Data{ std::exchange(x.Data, nullptr) }, Type{ x.Type }{}
		DecodedBody( const DecodedBody& )=delete;
		~DecodedBody(){ if( Data ) UA_delete( Data, Type ); }
		explicit operator bool()Ι{ return Data!=nullptr; }
		void* Data{};
		const UA_DataType* Type{};
	};
	α DecodeBody( const UA_ExtensionObject& eo, const ITypes* types )ι->DecodedBody;
	//Every encoded ExtensionObject in `v`, an element or the value itself, whose encoding id `types` lacks, noted with it (ITypes::Missing):
	//the data-change callback's, so a type added to a server while its row lives is fetched when its first value arrives.
	α NoteMissingTypes( const UA_Variant& v, const ITypes& types )ι->void;
}