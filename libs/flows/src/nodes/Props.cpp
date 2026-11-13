#include "Props.h"
#include <jde/fwk/str.h>

#define let const auto

namespace Jde::Opc::Flows{
	α Prop::Find( const jobject& props, sv key )ι->const jvalue*{
		let v = props.if_contains( key );
		return v && !v->is_null() ? v : nullptr;
	}
	α Prop::Text( const jobject& props, sv key )ε->string{
		let v = Find( props, key );
		THROW_IF( !v || !v->is_string() || v->get_string().empty(), "`{}` must be text.", key );
		return string{ v->get_string() };
	}
	α Prop::Text( const jobject& props, sv key, sv dflt )ε->string{
		return Find( props, key ) ? Text( props, key ) : string{ dflt };
	}
	α Prop::Flag( const jobject& props, sv key, bool dflt )ε->bool{
		let v = Find( props, key );
		THROW_IF( v && !v->is_bool(), "`{}` must be true or false.", key );
		return v ? v->get_bool() : dflt;
	}
	α Prop::Number( const jobject& props, sv key )ε->double{
		let v = Find( props, key );
		THROW_IF( !v || !v->is_number(), "`{}` must be a number.", key );
		return v->to_number<double>();
	}
	α Prop::FindCount( const jobject& props, sv key )ε->optional<uint>{
		let v = Find( props, key );
		if( !v )
			return nullopt;
		boost::system::error_code ec;
		let y = v->to_number<uint>( ec );
		THROW_IF( ec || !y, "`{}` must be a whole number above zero.", key );
		return y;
	}
	α Prop::FindWait( const jobject& props, sv key )ε->optional<Duration>{
		let v = Find( props, key );
		if( !v )
			return nullopt;
		optional<Duration> y;
		if( let text = v->if_string() ){
			if( let duration = Chrono::TryToDuration(sv{*text}) )
				y = *duration;
		}
		THROW_IF( !y || *y<=Duration::zero(), "`{}` must be an ISO 8601 duration above zero, as PT30S is.", key );
		return y;
	}
	α Prop::Wait( const jobject& props, sv key )ε->Duration{
		let y = FindWait( props, key );
		THROW_IF( !y, "`{}` must be an ISO 8601 duration above zero, as PT30S is.", key );
		return *y;
	}
	α Prop::Choice( const jobject& props, sv key, std::span<const sv> names, optional<uint> dflt )ε->uint{
		let v = Find( props, key );
		if( !v && dflt )
			return *dflt;
		let p = v && v->is_string() ? std::ranges::find( names, sv{v->get_string()} ) : names.end();
		THROW_IF( p==names.end(), "`{}` must be one of {}.", key, Str::Join(names, ", ") );
		return p-names.begin();
	}
	α Prop::Rules( const jobject& props )ε->const jarray&{
		let v = Find( props, "rules" );
		let a = v ? v->if_array() : nullptr;
		THROW_IF( !a || a->empty() || !std::ranges::all_of(*a, [](let& rule){ return rule.is_object(); }), "`rules` must be a list of rules, one at least." );
		return *a;
	}
}