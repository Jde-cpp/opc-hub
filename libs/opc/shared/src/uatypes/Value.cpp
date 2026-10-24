#include <jde/opc/uatypes/Value.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/Variant.h>


#define let const auto

namespace Jde::Opc{
	//The inverse of Variant.cpp's toJson( double ).  nullopt means "not one of the three", and the caller falls through to the ordinary numeric read, so
	//any other string still fails there with its own message rather than becoming a silent NaN.
	Ω nonFinite( const jvalue& j )ι->optional<double>{
		optional<double> y;
		if( j.is_string() ){
			let& s = j.get_string();
			if( s=="NaN" )
				y = std::numeric_limits<double>::quiet_NaN();
			else if( s=="Infinity" )
				y = std::numeric_limits<double>::infinity();
			else if( s=="-Infinity" )
				y = -std::numeric_limits<double>::infinity();
		}
		return y;
	}

	α Value::ToJson()Ι->jvalue{
		//Severity, not "non-zero".  Only a Bad code means the reading is unusable; an Uncertain one (0x4xxxxxxx) still
		//carries a value per OPC-UA - UNCERTAINLASTUSABLEVALUE with a last reading of 42.7 used to serialize as {"sc":…}
		//and blank the tag in the UI for a node that was still reporting.
		if( UA_StatusCode_isBad(status) )
			return UAException::ToJson( status );
		if( IsEmpty() )
			return hasStatus ? UAException::ToJson( status ) : jvalue{};//nothing to show, so say why if we were told.
		let scalar = IsScalar();
		let& type = *value.type;
		jvalue j{ scalar ? jvalue{} : jarray{} };
		for( uint i=0; i<(scalar ? 1 : value.arrayLength); ++i ){
			//Element i, not the whole variant: this used to re-encode every element for each element, which gave an
			//array of N copies of the whole array.  The try is what keeps ToJson's Ι honest - ElementToJson is ε, and
			//the encoder throws on anything it cannot fit (see #11).  A failed element becomes a status object rather
			//than taking the process down: both callers are noexcept, one of them an open62541 C callback.
			jvalue element;
			try{
				element = Variant::ElementToJson( (const UA_Byte*)value.data + i*type.memSize, type );
			}
			catch( const runtime_error& e ){
				ERRT( ELogTags::Parsing, "Could not encode element {} of '{}' - {}.  Serializing it as a status instead.", i, type.typeName, e.what() );
				element = UAException::ToJson( UA_STATUSCODE_BADENCODINGERROR );
			}
			if( scalar )
				j = move( element );
			else
				j.get_array().push_back( move(element) );
		}
		//A non-zero code rides along with the reading - {"v":…,"sc":…} - rather than replacing it.  Uncertain is the
		//motivating case, but an informative Good sub-code (GoodClamped) is reported too: the rule is "never drop a status".
		return status ? jvalue{ jobject{ {"v", move(j)}, {"sc", status} } } : move(j);
	}

	α Value::Set( const jvalue& j, SL sl )ε->void{
		//Whatever ToJson emits, Set has to accept back.  Two of its shapes are objects that every arm below would reject,
		//since Json::AsNumber's try_to_number returns not_number for one and the type inference has no arm for one either:
		//a reading carrying a status is {v,sc} (review2 #15), and a 64-bit integer is the protobufjs Long {high,low,
		//unsigned} - what the SPA builds a Long from, and what a Long serializes back to, long.js having no toJSON.
		//Unwrapped here rather than per arm so the typeless path gets them too.  {seconds,nanos} is left alone: that is
		//DATETIME/DURATION's own spelling, and its arms read it.
		if( let* o = j.if_object(); o ){
			if( o->contains("v") )
				return Set( o->at("v"), sl );//the status is the server's to report, not the writer's to send back.
			if( o->contains("high") && o->contains("low") )
				return Set( FromLong(*o, sl), sl );
		}
		auto& type = value.type;
		if( !type ){
			if( j.is_null() )
				throw Exception{ sl, ELogLevel::Error, "Value has no type and json is null." };
			else if( j.is_bool() )
				type = &UA_TYPES[UA_TYPES_BOOLEAN];
			else if( j.is_int64() )
				type = &UA_TYPES[UA_TYPES_INT64];
			else if( j.is_uint64() )
				type = &UA_TYPES[UA_TYPES_UINT64];
			else if( j.is_double() )
				type = &UA_TYPES[UA_TYPES_DOUBLE];
			else if( j.is_string() )
				type = &UA_TYPES[UA_TYPES_STRING];
			else
				throw Exception{ sl, ELogLevel::Error, "Value has no type." };
		}
		let dt = type;
		UA_Variant_clear( &value );
		type = dt;
		if( j.is_array() )//ToJson emits one per element, so Set has to read one back; every element goes through Set itself.
			return SetArray( j.get_array(), sl );
		auto setDuration = [&]()ε->void {
			let& o = j.as_object();
			THROW_IF( !o.contains("seconds") || !o.contains("nanos"), "Expected duration object with 'seconds' and 'nanos' - '{}'.", serialize(j) );
			//Scale in double instead of adding chrono durations:  seconds+nanoseconds has int64 *nanoseconds* as its common
			//type, so a client-supplied `seconds` past ~292 years overflowed the conversion - UB, and a UBSan abort in debug.
			//The fields keep their integer reads, so an out-of-range or non-integral one still throws rather than truncating.
			//Same formula as the socket's toDuration, so both entry points into a UA_Duration agree.
			SetNumber<UA_Duration>( Json::AsNumber<int64_t>(o, "seconds", sl)*1000. + Json::AsNumber<int32_t>(o, "nanos", sl)/1'000'000., sl );
		};

		switch( type->typeKind ){
		case UA_DATATYPEKIND_BOOLEAN:{
			THROW_IF( !j.is_bool(), "Expected bool '{}'.", serialize(j) );
			UA_Boolean v = j.get_bool();
			SetScalar( &v, sl );
			break;}
		case UA_DATATYPEKIND_BYTE: SetNumber<UA_Byte>( j, sl ); break;
		case UA_DATATYPEKIND_SBYTE: SetNumber<UA_SByte>( j, sl ); break;//SByte (i=2) is what a PLC server gives a `Char`; the arm was simply missing.
		case UA_DATATYPEKIND_INT16: SetNumber<UA_Int16>( j, sl ); break;
		case UA_DATATYPEKIND_UINT16: SetNumber<UA_UInt16>( j, sl ); break;
		case UA_DATATYPEKIND_INT32: SetNumber<UA_Int32>( j, sl ); break;
		case UA_DATATYPEKIND_UINT32: SetNumber<UA_UInt32>( j, sl ); break;
		case UA_DATATYPEKIND_INT64: SetNumber<UA_Int64>( j, sl ); break;
		case UA_DATATYPEKIND_UINT64: SetNumber<UA_UInt64>( j, sl ); break;
		//One arm for both:  UA_Duration *is* UA_Double, a count of milliseconds - the same kind - and either can arrive in
		//either json spelling.  ToJson emits the bare number, while the socket sends a Duration as a google.protobuf.Duration,
		//which reaches a client as {seconds,nanos}.  DURATION used to accept only the object, so writing back a value the
		//client had just read over REST threw inside as_object().
		case UA_DATATYPEKIND_DOUBLE:
			if( let v = nonFinite(j); v )
				SetScalar( &*v, sl );
			else if( j.is_object() )
				setDuration();
			else
				SetNumber<UA_Double>( j, sl );//value.type is untouched, so a DURATION stays a DURATION.
			break;
		case UA_DATATYPEKIND_FLOAT:
			if( let v = nonFinite(j); v ){
				let f = (UA_Float)*v;//through float, so an Infinity stays one and a NaN stays one.
				SetScalar( &f, sl );
			}
			else
				SetNumber<UA_Float>( j, sl );
			break;
		case UA_DATATYPEKIND_STRING:
		case UA_DATATYPEKIND_LOCALIZEDTEXT:{
			THROW_IF( !j.is_string(), "Expected string '{}'.", serialize(j) );
			let str = ToUV( j.get_string() );
			if( type->typeKind==UA_DATATYPEKIND_STRING )
				SetScalar( &str, sl );
			else{
				UA_LocalizedText lt;
				lt.locale = UA_STRING_NULL;
				lt.text = str;
				SetScalar( &lt, sl );
			}
			break;}
		case UA_DATATYPEKIND_DATETIME:{
			let v = UADateTime{j}.UA();//SetScalar, not SetNumber: that takes a jvalue, so the int64 was boxed into json
			SetScalar( &v, sl );//and parsed straight back out.
			break;}
		default:
			THROW( "Setting type '{}' has not been implemented - {}", type->typeName, serialize(j) );
		}
	}

	//Each element goes through Set itself, so the array covers exactly the types the scalar arms do - the Long form and
	//the {v,sc} unwrap included - rather than a second type ladder that could drift from the first.  The element's box is
	//then transferred into the slot: memcpy moves the memSize bytes, and with them ownership of anything they point at;
	//UA_free releases the box without clearing its members; UA_Variant_init keeps the temporary's destructor off it.
	α Value::SetScalar( const void* p, SL sl )ε->void{
		if( let sc = UA_Variant_setScalarCopy(&value, p, value.type); sc )
			throw UAException{ sc, Ƒ("Could not set a '{}' value.", value.type->typeName), {}, sl };
	}

	α Value::SetArray( const jarray& a, SL sl )ε->void{
		let type = value.type;
		auto data = UA_Array_new( a.size(), type );
		if( !data )
			throw Exception{ sl, ELogLevel::Error, "Could not allocate {} '{}' elements.", a.size(), type->typeName };
		try{
			for( uint i=0; i<a.size(); ++i ){
				if( a[i].is_array() )
					throw Exception{ sl, ELogLevel::Error, "A UA array is flat - nested arrays are its arrayDimensions, not json - '{}'.", serialize(a[i]) };
				Value element{ a[i], type, sl };
				if( !element.value.data )
					throw Exception{ sl, ELogLevel::Error, "Element {} of '{}' produced no value.", i, type->typeName };
				::memcpy( (UA_Byte*)data+i*type->memSize, element.value.data, type->memSize );
				UA_free( element.value.data );
				UA_Variant_init( &element.value );
			}
		}
		catch( ... ){
			UA_Array_delete( data, a.size(), type );//UA_Array_new zeroes, so the slots not yet filled clear to no-ops.
			value.type = type;//UA_Array_delete does not touch `value`; keep the type for the caller's diagnostics.
			throw;
		}
		UA_Variant_setArray( &value, data, a.size(), type );
	}
}