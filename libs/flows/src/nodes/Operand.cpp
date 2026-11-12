#include "Operand.h"
#include "Props.h"

#define let const auto

namespace Jde::Opc{
	α Flows::AsNumber( const jvalue& v )ι->optional<double>{
		boost::system::error_code ec;
		let y = v.is_number() ? v.to_number<double>( ec ) : 0;
		return v.is_number() && !ec ? optional{ y } : nullopt;
	}
	α Flows::Order( const jvalue& a, const jvalue& b )ι->optional<int>{
		let order = []( let& x, let& y ){ return x<y ? -1 : y<x ? 1 : 0; };
		if( a.is_number() && b.is_number() ){
			if( !a.is_double() && !b.is_double() )
				return Jde::operator<( a, b ) ? -1 : Jde::operator<( b, a ) ? 1 : 0;
			let x = AsNumber( a ); let y = AsNumber( b );
			return x && y && !std::isnan( *x ) && !std::isnan( *y ) ? optional{ order(*x, *y) } : nullopt;
		}
		if( a.is_string() && b.is_string() )
			return order( a.get_string(), b.get_string() );
		if( a.is_bool() && b.is_bool() )
			return order( a.get_bool(), b.get_bool() );
		return nullopt;
	}
	α Flows::Equal( const jvalue& a, const jvalue& b )ι->bool{
		return a.is_number() && b.is_number() ? Order( a, b )==0 : a==b;
	}

	α Flows::ToScope( const jobject& rule, sv typeKey, EScope dflt )ε->EScope{
		constexpr std::array<sv,3> names{ "msg", "flow", "global" };
		return Prop::Find( rule, typeKey ) ? (EScope)( Prop::Choice(rule, typeKey, names)+1 ) : dflt;
	}
}
namespace Jde::Opc::Flows{
	Operand::Operand( const jobject& rule, sv key, sv typeKey )ε:
		_scope{ ToScope(rule, typeKey, EScope::Literal) }{
		if( _scope==EScope::Literal ){
			let v = rule.if_contains( key );
			THROW_IF( !v, "the rule has no `{}`.", key );
			_literal = *v;
		}
		else if( _scope==EScope::Msg )
			_property.emplace( Prop::Text(rule, key) );
		else
			_key = Prop::Text( rule, key );
	}
	α Operand::Find( const Message& m, const Store& flow, const GlobalStore& global, jvalue& scratch )Ι->const jvalue*{
		switch( _scope ){
		case EScope::Literal: return &_literal;
		case EScope::Msg: return _property->Find( m.Json );
		case EScope::Flow: return flow.Get( _key );
		default:
			scratch = global.Get( _key );
			return scratch.is_null() ? nullptr : &scratch;
		}
	}
}