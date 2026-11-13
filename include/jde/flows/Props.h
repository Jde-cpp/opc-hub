#pragma once

//A node's `props`, read to their types, the library's nodes' and a host's.  What each throws is the graph's fault at the
//node, so it names the key.
namespace Jde::Opc::Flows::Prop{
	α Find( const jobject& props, sv key )ι->const jvalue*;//null for a key that is absent or null.
	α Text( const jobject& props, sv key )ε->string;
	α Text( const jobject& props, sv key, sv dflt )ε->string;
	α FindText( const jobject& props, sv key )ε->optional<string>;//text that is not empty, where the key is present.
	α Flag( const jobject& props, sv key, bool dflt=false )ε->bool;
	α Number( const jobject& props, sv key )ε->double;
	α FindNumber( const jobject& props, sv key )ε->optional<double>;
	α FindCount( const jobject& props, sv key )ε->optional<uint>;//a whole number above zero.
	α FindWait( const jobject& props, sv key, bool zero=false )ε->optional<Duration>;//an ISO 8601 duration above zero, or at it with `zero`.
	α Wait( const jobject& props, sv key )ε->Duration;
	α Choice( const jobject& props, sv key, std::span<const sv> names, optional<uint> dflt={} )ε->uint;//the index in `names` of the key's text.
	α Rules( const jobject& props )ε->const jarray&;//`rules`:  objects, one at least.
}