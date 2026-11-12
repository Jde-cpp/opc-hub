#include <jde/flows/Context.h>

#define let const auto

namespace Jde::Opc::Flows{
	Ω keys( const jobject& values )ι->vector<string>{
		vector<string> y;
		y.reserve( values.size() );
		for( let& member : values )
			y.emplace_back( member.key() );
		return y;
	}
	//The value before.
	Ω set( jobject& values, sv key, jvalue&& value, const Store::Changed& changed )ι->jvalue{
		jvalue before;
		if( value.is_null() ){
			let p = values.find( key );
			if( p==values.end() )
				return before;
			before = move( p->value() );
			values.erase( p );
			if( changed )
				changed( key, nullptr );
		}
		else{
			auto& held = values[key];
			before = std::exchange( held, move(value) );
			if( changed )
				changed( key, &held );
		}
		return before;
	}

	α Store::Get( sv key )Ι->const jvalue*{ return _values.if_contains( key ); }
	α Store::Set( sv key, jvalue value )ι->void{ set( _values, key, move(value), _changed ); }
	α Store::Keys()Ι->vector<string>{ return keys( _values ); }

	α GlobalStore::Get( sv key )Ι->jvalue{
		lg _{ _mutex };
		let v = _values.if_contains( key );
		return v ? *v : jvalue{};
	}
	α GlobalStore::Set( sv key, jvalue value )ι->jvalue{
		lg _{ _mutex };
		return set( _values, key, move(value), _changed );
	}
	α GlobalStore::Keys()Ι->vector<string>{
		lg _{ _mutex };
		return keys( _values );
	}
}