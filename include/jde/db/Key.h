#pragma once
#include <jde/fwk/io/json.h>

namespace Jde::DB{
	struct Key{
		Key( uint id )ι:_key{id}{}
		Key( string slug )ι:_key{slug}{}
		α IsPK()Ι{ return _key.index()==0; }
		α PK()Ι->uint{ return get<uint>(_key); }
		α NK()Ι->str{ return get<string>(_key); }
		α NK()ι->string&{ return const_cast<string&>( std::as_const(*this).NK() ); }
		α QLVariables()Ι->jobject{ return IsPK() ? jobject{ {"id", PK()} } : jobject{ {"slug", NK()} }; }
		α QLInput()Ι->sv{ return IsPK() ? "id: $id" : "slug: $slug"; }
		private:
			variant<uint,string> _key;
	};
	Ξ AsKey( const jobject& o, SRCE )ε->Key{
		if( auto p = o.if_contains("id"); p )
			return Key{ Json::AsNumber<uint>(*p, sl) };
		if( auto p = o.if_contains("slug"); p )
			return Key{ string{Json::AsSV(*p, sl)} };
		THROWSL( "Could not find 'id' or 'slug' in {}.", serialize(o) );
	}
}