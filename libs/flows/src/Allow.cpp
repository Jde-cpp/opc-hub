#include <jde/flows/Allow.h>

#define let const auto

namespace Jde::Opc::Flows{
	//`pattern` over `text`, both lower case:  `*` any run, `?` any one.
	Ω globbed( sv pattern, sv text )ι->bool{
		uint p{}, t{};
		optional<uint> star; uint mark{};
		while( t<text.size() ){
			if( p<pattern.size() && (pattern[p]=='?' || pattern[p]==text[t]) ){
				++p; ++t;
			}
			else if( p<pattern.size() && pattern[p]=='*' ){
				star = p++;
				mark = t;
			}
			else if( star ){
				p = *star+1;
				t = ++mark;
			}
			else
				return false;
		}
		while( p<pattern.size() && pattern[p]=='*' )
			++p;
		return p==pattern.size();
	}
	//Lower case, without an IPv6 literal's brackets or the root's dot.
	Ω hostName( sv host )ι->string{
		if( host.size()>1 && host.front()=='[' && host.back()==']' )
			host = host.substr( 1, host.size()-2 );
		if( host.size()>1 && host.back()=='.' )
			host.remove_suffix( 1 );
		string y{ host };
		for( auto& ch : y )
			ch = ch>='A' && ch<='Z' ? ch-'A'+'a' : ch;
		return y;
	}
}
namespace Jde::Opc{
	α Flows::HostAllowed( sv host, std::span<const string> allow )ι->bool{
		if( host.find_first_of("@/\\?# \t")!=sv::npos )//a user, a path or a query in it:  no host.
			return false;
		let name = hostName( host );
		return !name.empty() && std::ranges::any_of( allow, [&]( str pattern ){ let p = hostName( pattern ); return !p.empty() && globbed( p, name ); } );
	}
}