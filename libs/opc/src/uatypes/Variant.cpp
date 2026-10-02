#include <jde/opc/uatypes/Variant.h>
#include <jde/db/Value.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/UAString.h>
#include <jde/opc/uatypes/Value.h>

#define let const auto
namespace Jde::Opc{
	//Value::Set already decodes a jvalue toward a declared UA type, and infers one when there is none - this delegates
	//rather than growing a second conversion chain that can drift from it.  Previously only the literal "double" was
	//honoured and everything else fell through to json inference, so a config's {dataType:"int", value:5} produced an
	//Int64 against an Int32 attribute and UA_Server_addVariableNode rejected the node with BadTypeMismatch.
	Variant::Variant( const jvalue& v, const UA_DataType* dataType )ε:
		UA_Variant{},
		VariantPK{0}{
		Value converted{ v, dataType };
		static_cast<UA_Variant&>(*this) = converted.value;
		UA_Variant_init( &converted.value );//we own it now.
	}

	//A null data pointer is ToUAValues saying "this one could not be decoded".  Stamping the type and the caller's
	//arrayDimensions onto it anyway produced a *typed* variant with data==NULL: UA_Variant_isEmpty looks at `type` only,
	//so IsNull() was false, ToJson() gave [], and with stored dims the binary encoder rejects it outright
	//(totalRequiredSize != arrayLength -> BadEncodingError, and the server closes the channel on every Read that touches
	//the node).  Build the genuinely empty variant instead - the shape a node that never had a value has, which
	//UA_Server_writeValue accepts as a value-less write rather than type-checking.
	Variant::Variant( uint32 pk, tuple<uint,void*> data, tuple<UA_UInt32*, uint> dims, const UA_DataType& dataType )ι:
		UA_Variant{},
		VariantPK{pk}{
		let [length, values] = data;
		let [dimensions, dimensionsSize] = dims;
		if( !values ){
			if( dimensions )//the caller allocated them for us; nothing else will free them.
				UA_Array_delete( dimensions, dimensionsSize, &UA_TYPES[UA_TYPES_UINT32] );
			return;
		}
		type = &dataType;
		storageType = UA_VARIANT_DATA;
		arrayLength = length;
		this->data = values;
		arrayDimensions = dimensions;
		arrayDimensionsSize = dimensionsSize;
	}

	Variant::Variant( Variant&& v )ι:
		UA_Variant{ move(v) },
		VariantPK{ move(v.VariantPK) }{
		UA_Variant_init( &v );
	}
	Variant::Variant( const Variant& v )ι:
		VariantPK{ v.VariantPK }{
		UA_Variant_copy( &v, this );
	}

	Variant::Variant( const UA_Variant& v )ι{
		UA_Variant_copy( &v, this );
	}
	Variant::Variant( UA_Variant&& v )ι:
		UA_Variant{ v }{
		UA_Variant_init( &v );
	}
	Variant::Variant( StatusCode sc )ι:
		UA_Variant{}{
		UA_Variant_setScalarCopy( this, &sc, &UA_TYPES[UA_TYPES_STATUSCODE] );
	}

	α Variant::operator=( const Variant& v )ι->Variant&{
		if( this==&v )
			return *this;
		UA_Variant_clear( this );
		UA_Variant_copy( &v, this );
		VariantPK = v.VariantPK;
		return *this;
	}
	α Variant::operator=( Variant&& v )ι->Variant&{
		if( this==&v )
			return *this;
		UA_Variant_clear( this );
		static_cast<UA_Variant&>(*this) = v;
		UA_Variant_init( &v );
		VariantPK = v.VariantPK;
		return *this;
	}

	α Variant::Move()ι->UA_Variant{
		UA_Variant y = *this;
		UA_Variant_init( this );
		return y;
	}

	α Variant::GetUAValue( const UA_DataType& type, UA_ByteString j )ε->void*{
		void* p = UA_new( &type );
		if( !p )
			throw UAException{ UA_STATUSCODE_BADOUTOFMEMORY };
		if( let sc = UA_decodeJson(&j, p, &type, nullptr); sc ){
			UA_delete( p, &type );
			throw UAException{ sc };
		}
		return p;
	}

	α Variant::ArrayDimString()ι->string{
		if( !arrayDimensions || arrayDimensionsSize==0 )
			return {};
		string csv;
		for( uint i=0; i<arrayDimensionsSize; ++i )
			csv += std::to_string( arrayDimensions[i] )+',';
		csv.pop_back();
		return csv;
	}

	//The column has to answer "scalar or array?" for all three shapes, since the element count alone cannot: declared
	//dimensions persist as their csv; an array without any - what UA_Variant_setArrayCopy leaves - persists its own
	//length, which is its one-dimensional shape; only a scalar persists NULL.  Binding ArrayDimString() made a scalar a
	//non-null '' and every stored scalar reloaded as a one-element array (review3 #3); binding NULL for both of the
	//first two would put review2 #10's residual back, a one-element array reloading as a scalar.
	α Variant::ArrayDimValue()ι->DB::Value{
		if( arrayDimensionsSize )
			return DB::Value{ ArrayDimString() };
		return arrayLength ? DB::Value{ std::to_string(arrayLength) } : DB::Value{};
	}

	Ω uaEncode( const void* v, const UA_DataType& type, UAString& out )ε->void{
		UA_EncodeJsonOptions options{};
		if( let sc=UA_encodeJson(v, &type, &out, &options); sc )
			throw UAException{ sc };
	}
	Ω uaJsonString( const void* v, const UA_DataType& type )ε->string{
		UAString j;
		uaEncode( v, type, j );
		return j.ToString();
	}
	α Variant::ToUAJson()ε->vector<string>{
		vector<string> y;
		if( IsNull() )
			return y;
		if( IsScalar() ){
			y.emplace_back( uaJsonString(data, *this->type) );
		}else{
			for( uint i=0; i<arrayLength; ++i )
				y.emplace_back( uaJsonString((UA_Byte*)data+i*type->memSize, *type) );
		}
		return y;
	}
	α Variant::ElementToJson( const void* element, const UA_DataType& type, bool trimNames )ε->jvalue{
		if( IsKind(&type, UA_TYPES_LOCALIZEDTEXT) && trimNames )
			return jstring{ ToString( ((const UA_LocalizedText*)element)->text ) };
		else if( IsKind(&type, UA_TYPES_DATETIME) )
			return UADateTime{ *(const UA_DateTime*)element }.ToJson();
		else if( IsKind(&type, UA_TYPES_NODEID) )
			return Opc::ToJson( *(const UA_NodeId*)element );
		else if( IsKind(&type, UA_TYPES_QUALIFIEDNAME) )
			return BrowseName::ToJson( *(const UA_QualifiedName*)element );
		else{
			UAString ua;
			uaEncode( element, type, ua );
			try{
				return parse( ToSV(ua) );//off the encoder's buffer: the std::string was only ever an intermediate here.
			}
			catch( runtime_error& e ){
				let uaJson = ua.ToString();//only the failure path needs it as a string.
				ERRT( ELogTags::Parsing, "Error parsing {} - {}", uaJson, e.what() );
				return {uaJson};
			}
		}
	}
	α Variant::ToJson( bool trimNames )ε->jvalue{
		jvalue y;
		if( IsNull() )
			return y;
		if( IsScalar() )
			y = ElementToJson( data, *type, trimNames );//the scalar arms moved into ElementToJson, where the array uses them too.
		else{
			jarray arr;
			for( uint i=0; i<arrayLength; ++i )
				arr.emplace_back( ElementToJson((UA_Byte*)data+i*type->memSize, *type, trimNames) );
			y = move(arr);
		}
		return y;
	}
	α Variant::ToUAValues( const UA_DataType& type, flat_map<uint, string>&& values, bool isArray )ι->tuple<uint,void*>{
		void* data = nullptr;
		let size = values.size();
		try{
			if( size==1 )
				data = GetUAValue( type, ToUV(values.begin()->second) );
			else if( size>1 ){
				data = UA_Array_new( size, &type );
				if( !data )
					throw UAException{ UA_STATUSCODE_BADOUTOFMEMORY };
				uint i=0;
				for( auto&& [_, j] : values ){
					let bytes = ToUV( j );
					if( let sc = UA_decodeJson( &bytes, (UA_Byte*)data+i++*type.memSize, &type, nullptr ); sc )
						throw UAException{ sc };
				}
			}
			values.clear();
		}
		catch( const runtime_error& e ){
			if( data )
				UA_Array_delete( data, size, &type );
			//Loudly:  a variant that cannot be decoded loads as null, which is indistinguishable from a node that never
			//had a value.  The stored text has to be the raw UA-json token ToUAJson emits - json-quoting a copy of it
			//(serialize(array[i])) turns an Int32 5 into "5", which decodes for no non-string type.
			ERRT( ELogTags::Parsing, "Could not decode {} '{}' element{} loaded from the db - {}.  First element: '{}'.  The variant loads as null.",
				size, type.typeName, size==1 ? "" : "s", e.what(), size ? values.begin()->second : string{} );
			return {0, nullptr};
		}
		//The persisted shape decides, not the element count.  A one-element array used to collapse to a scalar here while
		//the caller still applied its arrayDimensions, and open62541 answers BadTypeMismatch to scalar data carrying
		//arrayDimensionsSize 1 - so one-element arrays did not survive a restart.  UA_new and UA_Array_new(1) allocate
		//the same single element, so only the length reported here has to change.
		return { isArray || size>1 ? size : 0, data };
	}

	α Variant::ToArrayDims( str csv )ι->tuple<UA_UInt32*, uint>{
		if( csv.empty() )
			return {nullptr, 0};
		auto dims = Str::Split( csv );
		let size = dims.size();
		auto arrayDims = (UA_UInt32*)UA_Array_new( size, &UA_TYPES[UA_TYPES_UINT32] );
		for( uint i=0; i<size; ++i )
			arrayDims[i] = Str::TryTo<UA_UInt32>( dims[i] ).value_or(0);
		return {arrayDims, size};
	}
}