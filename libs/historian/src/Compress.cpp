#include "Compress.h"

#define let const auto

namespace Jde::Opc{
	namespace{
		using Number = variant<int64_t,uint64_t,double>;
		//By kind, so an alias counts as what it is, a Duration as a Double and a Counter as a UInt32.  A Boolean, a
		//StatusCode or a DateTime is a state, not a quantity.
		Ω number( const UA_Variant& v )ι->optional<Number>{
			if( !UA_Variant_isScalar(&v) )
				return nullopt;
			switch( v.type->typeKind ){
			case UA_DATATYPEKIND_SBYTE: return Number{ (int64_t)*(const UA_SByte*)v.data };
			case UA_DATATYPEKIND_INT16: return Number{ (int64_t)*(const UA_Int16*)v.data };
			case UA_DATATYPEKIND_INT32: return Number{ (int64_t)*(const UA_Int32*)v.data };
			case UA_DATATYPEKIND_INT64: return Number{ (int64_t)*(const UA_Int64*)v.data };
			case UA_DATATYPEKIND_BYTE: return Number{ (uint64_t)*(const UA_Byte*)v.data };
			case UA_DATATYPEKIND_UINT16: return Number{ (uint64_t)*(const UA_UInt16*)v.data };
			case UA_DATATYPEKIND_UINT32: return Number{ (uint64_t)*(const UA_UInt32*)v.data };
			case UA_DATATYPEKIND_UINT64: return Number{ (uint64_t)*(const UA_UInt64*)v.data };
			case UA_DATATYPEKIND_FLOAT: return Number{ (double)*(const UA_Float*)v.data };
			case UA_DATATYPEKIND_DOUBLE: return Number{ (double)*(const UA_Double*)v.data };
			default: return nullopt;
			}
		}
		//Exact for the integers, whose difference a double of each would round away past 2^53.
		Ω distance( const Number& a, const Number& b )ι->double{
			if( let x = get_if<int64_t>(&a) ){
				let y = get<int64_t>( b );
				return (double)( *x>=y ? (uint64_t)*x-(uint64_t)y : (uint64_t)y-(uint64_t)*x );
			}
			if( let x = get_if<uint64_t>(&a) ){
				let y = get<uint64_t>( b );
				return (double)( *x>=y ? *x-y : y-*x );
			}
			return std::abs( get<double>(a)-get<double>(b) );
		}
		Ω magnitude( const Number& n )ι->double{
			return std::visit( []( auto v ){ return std::abs((double)v); }, n );
		}
	}

	α Hist::Passes( const Thresholds& config, const UA_DataValue& stored, const UA_DataValue& change )ι->bool{
		if( (stored.hasStatus ? stored.status : UA_STATUSCODE_GOOD)!=(change.hasStatus ? change.status : UA_STATUSCODE_GOOD) )
			return true;
		if( !config.ExceptionDeviation || !stored.hasValue || !change.hasValue || stored.value.type!=change.value.type )
			return true;
		let from = number( stored.value ), to = number( change.value );
		if( !from || !to )
			return true;
		auto band = *config.ExceptionDeviation;
		switch( config.DeviationFormat ){
		case UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFVALUE:
			band = band*magnitude( *from )/100;
			break;
		case UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE:
		case UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE:
			if( !config.Range )//the group's validate left no deviation without one.
				return true;
			band = band*( config.Range->High-config.Range->Low )/100;
			break;
		default:
			break;
		}
		return !( distance(*from, *to)<band );//a NaN is at no distance, so it passes.
	}
}
