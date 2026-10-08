#include <jde/opc/uatypes/Value.h>
#include <charconv>
#include <cmath>
#include <limits>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/BrowseName.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Variant.h>


#define let const auto

namespace Jde::Opc{
	α Value::operator=( Value&& x )ι->Value&{
		if( this==&x )
			return *this;
		UA_DataValue_clear(this);
		static_cast<UA_DataValue&>(*this) = static_cast<UA_DataValue&>(x);
		UA_DataValue_init(&x);
		return *this;
	}

	//OPC-UA Part 6 spells the non-finite doubles as the strings "NaN"/"Infinity"/"-Infinity", which is what UA_encodeJson
	//writes - so Variant::ToJson, which goes through the encoder, has always emitted them and Value did not.  Boost.JSON's
	//default options write a NaN as `null`, the same text a value-less reading serializes as, so a failed sensor - NaN is
	//the common one - reached the SPA as a blank tag.  Its allow_infinity_and_nan writes the bare tokens NaN/Infinity
	//instead, which are not json at all: JSON.parse throws on the whole response.  The string spelling is valid json, is
	//the vendor's, and is distinguishable from "no value".  (±Inf survived by accident - Boost writes 1e99999, which
	//JSON.parse reads as Infinity - but it goes the same way, so the three stay one shape.)
	Ω toJson( double v )ι->jvalue{
		return std::isnan( v ) ? jvalue{"NaN"} : std::isinf( v ) ? jvalue{ v<0 ? "-Infinity" : "Infinity" } : jvalue{v};
	}
	//A Float at the fewest digits that are still that float.  Widened as it is, 21.3f writes 21.299999237060547, which every screen
	//showed;  the double parsed from the float's shortest text writes 21.3, and narrows back to the same float.
	Ω toJson( float v )ι->jvalue{
		if( !std::isfinite(v) )
			return toJson( (double)v );
		char text[32];
		const char* end = std::to_chars( text, text+sizeof(text), v ).ptr;
		double y{};
		std::from_chars( text, end, y );
		return jvalue{ y };
	}
	//The inverse.  nullopt means "not one of the three", and the caller falls through to the ordinary numeric read, so
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

#define IS(ua) IsKind( type, ua )//the kind, not the descriptor address - see IsKind in opcHelpers.h.
	α Value::ToJson()Ι->jvalue{
		//Severity, not "non-zero".  Only a Bad code means the reading is unusable; an Uncertain one (0x4xxxxxxx) still
		//carries a value per OPC-UA - UNCERTAINLASTUSABLEVALUE with a last reading of 42.7 used to serialize as {"sc":…}
		//and blank the tag in the UI for a node that was still reporting.
		if( UA_StatusCode_isBad(status) )
			return UAException::ToJson( status );
		if( IsEmpty() )
			return hasStatus ? UAException::ToJson( status ) : jvalue{};//nothing to show, so say why if we were told.
		let scaler = IsScalar();
		let type = value.type;
		jvalue j{ scaler ? jvalue{} : jarray{} };
		auto add = [scaler, &j]( let& v )ι{
			if( scaler )
				j = v;
			else
				j.get_array().push_back( v );
		};
		auto addExplicit = [scaler, &j]( jvalue&& x ){ if( scaler ) j = move(x); else j.get_array().push_back( move(x) ); };
		for( uint i=0; i<(scaler ? 1 : value.arrayLength); ++i ){
			if( IS(UA_TYPES_BOOLEAN) )
				add( Get<UA_Boolean>(i) );
			else if( IS(UA_TYPES_BYTE) )
				addExplicit( (unsigned char)((UA_Byte*)value.data)[i] );
			else if( IS(UA_TYPES_BYTESTRING) ) [[unlikely]]
				addExplicit( ByteStringToJson(((UA_ByteString*)value.data)[i]) );
			else if( IS(UA_TYPES_DATETIME) )
				addExplicit( UADateTime{((UA_DateTime*)value.data)[i]}.ToJson() );
			else if( IS(UA_TYPES_DOUBLE) )
				addExplicit( toJson(Get<UA_Double>(i)) );//and Duration, which is a Double by kind - it had its own arm when this dispatched on the address.
			else if( IS(UA_TYPES_EXPANDEDNODEID) ) [[unlikely]]
				addExplicit( Opc::ToJson( ((UA_ExpandedNodeId*)value.data)[i] ) );
			else if( IS(UA_TYPES_FLOAT) )
				addExplicit( toJson(Get<UA_Float>(i)) );
			else if( IS(UA_TYPES_GUID) ) [[unlikely]]
				addExplicit( Opc::ToJson(((UA_Guid*)value.data)[i]) );
			else if( IS(UA_TYPES_INT16) ) [[likely]]
				add( Get<UA_Int16>(i) );
			else if( IS(UA_TYPES_INT32) ) [[likely]]
				add( Get<UA_Int32>(i) );
			else if( IS(UA_TYPES_INT64) )
				addExplicit( Opc::ToJson(((UA_Int64*)value.data)[i]) );
			else if( IS(UA_TYPES_NODEID) )
				addExplicit( Opc::ToJson((((UA_NodeId*)value.data)[i])) );
			else if( IS(UA_TYPES_SBYTE) )
				addExplicit( (char)((UA_SByte*)value.data)[i] );
			else if( IS(UA_TYPES_STATUSCODE) )
				add( Get<StatusCode>(i) );
			else if( IS(UA_TYPES_STRING) ) [[likely]]
				addExplicit( jstring{ToSV(((UA_String*)value.data)[i])} );
			else if( IS(UA_TYPES_UINT16) )
				add( Get<UA_UInt16>(i) );
			else if( IS(UA_TYPES_UINT32) ) [[likely]]
				add( Get<UA_UInt32>(i) );
			else if( IS(UA_TYPES_UINT64) )
				addExplicit( Opc::ToJson(((UA_UInt64*)value.data)[i]) );
			else if( IS(UA_TYPES_XMLELEMENT) ) [[unlikely]]
				addExplicit( jstring{ToSV(((UA_XmlElement*)value.data)[i])} );
			else if( IS(UA_TYPES_LOCALIZEDTEXT) ) [[unlikely]]
				addExplicit( jstring{ToSV(((UA_LocalizedText*)value.data)[i].text)} ); //the text alone, as the Variant fallback's trimNames did.
			else if( IS(UA_TYPES_QUALIFIEDNAME) ) [[unlikely]]
				addExplicit( BrowseName::ToJson(((UA_QualifiedName*)value.data)[i]) );
			else{
				//Element i, not the whole variant: this used to re-encode every element for each element, which gave an
				//array of N copies of the whole array.  The try is what keeps ToJson's Ι honest - ElementToJson is ε, and
				//the encoder throws on anything it cannot fit (see #11).  A failed element becomes a status object rather
				//than taking the process down: both callers are noexcept, one of them an open62541 C callback.
				try{
					addExplicit( Variant::ElementToJson((const UA_Byte*)value.data + i*type->memSize, *type, true) );
				}
				catch( const runtime_error& e ){
					ERRT( ELogTags::Parsing, "Could not encode element {} of '{}' - {}.  Serializing it as a status instead.", i, type->typeName, e.what() );
					addExplicit( UAException::ToJson(UA_STATUSCODE_BADENCODINGERROR) );
				}
			}
		}
		//A non-zero code rides along with the reading - {"v":…,"sc":…} - rather than replacing it.  Uncertain is the
		//motivating case, but an informative Good sub-code (GoodClamped) is reported too: the rule is "never drop a status".
		return status ? jvalue{ jobject{ {"v", move(j)}, {"sc", status} } } : move(j);
	}

	//Reassembles the protobufjs Long shape.  Both halves are read as int64 and narrowed to 32 bits: Opc::ToJson writes
	//them as unsigned 0..2^32-1 while protobufjs writes them as *signed* int32s, and the cast reads either.  Assembled
	//unsigned so a value past INT64_MAX does not overflow the shift - the "unsigned" flag beside them says how the 64
	//bits are meant to be read, and the caller applies it.
	Ω longBits( const jobject& o, SL sl )ε->uint64_t{
		let half = [&]( sv key )ε->uint32_t{
			let v = Json::AsNumber<int64_t>( o, key, sl );
			if( v>0xFFFFFFFFLL || v<-0x80000000LL )
				throw Exception{ sl, ELogLevel::Error, "Long '{}' is {}, not a 32-bit half - '{}'.", key, v, serialize(o) };
			return (uint32_t)v;
		};
		return (uint64_t)half("high")<<32 | half("low");
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
			if( o->contains("high") && o->contains("low") ){
				let bits = longBits( *o, sl );
				return Json::FindBool( *o, "unsigned" ).value_or( false ) ? Set( jvalue{bits}, sl ) : Set( jvalue{(int64_t)bits}, sl );
			}
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

		if( IS(UA_TYPES_BOOLEAN) ){
			THROW_IF( !j.is_bool(), "Expected bool '{}'.", serialize(j) );
			UA_Boolean v = j.get_bool();
			SetScalar( &v, sl );
		}
		else if( IS(UA_TYPES_BYTE) )
			SetNumber<UA_Byte>( j, sl );
		else if( IS(UA_TYPES_SBYTE) )//SByte (i=2) is what a PLC server gives a `Char`; the arm was simply missing.
			SetNumber<UA_SByte>( j, sl );
		//One branch for both:  UA_Duration *is* UA_Double, a count of milliseconds - the same kind, so one test now covers
		//both - and either can arrive in either json spelling.  ToJson emits the bare number, while the socket sends a
		//Duration as a google.protobuf.Duration, which reaches a client as {seconds,nanos}.  DURATION used to accept only
		//the object, so writing back a value the client had just read over REST threw inside as_object().
		else if( IS(UA_TYPES_DOUBLE) ){
			if( let v = nonFinite(j); v )
				SetScalar( &*v, sl );
			else if( j.is_object() )
				setDuration();
			else
				SetNumber<UA_Double>( j, sl );//value.type is untouched, so a DURATION stays a DURATION.
		}
		else if( IS(UA_TYPES_FLOAT) ){
			if( let v = nonFinite(j); v ){
				let f = (UA_Float)*v;//through float, so an Infinity stays one and a NaN stays one.
				SetScalar( &f, sl );
			}
			else
				SetNumber<UA_Float>( j, sl );
		}
		else if( IS(UA_TYPES_INT16) )
			SetNumber<UA_Int16>( j, sl );
		else if( IS(UA_TYPES_INT32) )
			SetNumber<UA_Int32>( j, sl );
		else if( IS(UA_TYPES_INT64) )
			SetNumber<UA_Int64>( j, sl );
		else if( IS(UA_TYPES_STRING) || IS(UA_TYPES_LOCALIZEDTEXT) ){
			THROW_IF( !j.is_string(), "Expected string '{}'.", serialize(j) );
			let str = ToUV( j.get_string() );
			if( IS(UA_TYPES_STRING) )
				SetScalar( &str, sl );
			else{
				UA_LocalizedText lt;
				lt.locale = UA_STRING_NULL;
				lt.text = str;
				SetScalar( &lt, sl );
			}
		}
		else if( IS(UA_TYPES_UINT16) )
			SetNumber<UA_UInt16>( j, sl );
		else if( IS(UA_TYPES_UINT32) )
			SetNumber<UA_UInt32>( j, sl );
		else if( IS(UA_TYPES_UINT64) )
			SetNumber<UA_UInt64>( j, sl );
		else if( IS(UA_TYPES_DATETIME) ){
			let v = UADateTime{j}.UA();//SetScalar, not SetNumber: that takes a jvalue, so the int64 was boxed into json
			SetScalar( &v, sl );//and parsed straight back out.
		}
		else
			THROW( "Setting type '{}' has not been implemented - {}", type->typeName, serialize(j) );
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