#pragma once

//A node's `props`, read to their types.  What each throws is the graph's fault at the node, so it names the key.
namespace Jde::Opc::Flows::Prop{
	α Find( const jobject& props, sv key )ι->const jvalue*;//null for a key that is absent or null.
	α Text( const jobject& props, sv key )ε->string;
	α Text( const jobject& props, sv key, sv dflt )ε->string;
	α Flag( const jobject& props, sv key, bool dflt=false )ε->bool;
	α Number( const jobject& props, sv key )ε->double;
	α FindCount( const jobject& props, sv key )ε->optional<uint>;//a whole number above zero.
	α FindWait( const jobject& props, sv key )ε->optional<Duration>;//an ISO 8601 duration above zero.
	α Wait( const jobject& props, sv key )ε->Duration;
	α Choice( const jobject& props, sv key, std::span<const sv> names, optional<uint> dflt={} )ε->uint;//the index in `names` of the key's text.
	α Rules( const jobject& props )ε->const jarray&;//`rules`:  objects, one at least.
}