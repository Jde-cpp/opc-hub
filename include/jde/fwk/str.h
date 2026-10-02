#pragma once
#include "jde/fwk/usings.h"
DISABLE_WARNINGS
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#include <charconv>
#include <span>
#include <boost/algorithm/string/trim.hpp>
#include <absl/strings/ascii.h>
#include <absl/strings/escaping.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_join.h>
#include "exceptions/Exception.h"
ENABLE_WARNINGS

#define Φ Γ auto
#define let const auto
namespace Jde{
	Ŧ To( sv value )ι->T{ T v{}; std::from_chars(value.data(), value.data()+value.size(), v); return v; }
	template<> Φ To( sv x )ι->double;
	Ŧ hex( T number )ι->string{ return Ƒ("{:x}", number); }
	Φ ToUuid( sv s, SRCE )ε->uuid;
	Φ ToString( const boost::uuids::uuid& u )ι->string;
}
namespace Jde::Str{
	Φ Empty()ι->str;
	Ξ Reserve( uint size )ι->string{ string s; s.reserve(size); return s; }

	template<class T=string> α Decode64( sv s, bool fileSafe=false, SRCE )ε->T;//padded or not; fileSafe is base64url.
	Φ DecodeUri( sv str )ι->string;
	Ŧ Encode64( const T& val, bool fileSafe=false )ι->string requires requires{ val.data(); val.size(); }{//fileSafe is base64url, unpadded (RFC 7515 §2).
		const sv bytes{ (const char*)val.data(), val.size()*sizeof(*val.data()) };
		return fileSafe ? absl::WebSafeBase64Escape( bytes ) : absl::Base64Escape( bytes );
	}

	Φ Format( sv format, vector<string> args )ε->string;
	Φ TryFormat( sv format, vector<string> args )ι->string;
	Ŧ Join( const T& collection, sv separator=",", bool quote=false )ι->string;
	Φ Replace( sv source, sv find, sv replace )ι->string;
	Φ Replace( sv source, char find, char replace )ι->string;
	Φ Split( sv s, char delim=',' )ι->vector<sv>;
	Ξ StartsWith( sv value, sv starting )ι{ return starting.size() > value.size() ? false : std::equal(starting.begin(), starting.end(), value.begin()); }
	Φ StartsWithInsensitive( sv value, sv starting )ι->bool;
	Φ LTrim( sv s )->sv;
	Φ RTrim( sv s )->sv;
	Φ LTrim( string&& s )->string;
	Φ RTrim( string&& s )->string;
	Φ ToHex( std::span<const byte> bytes )ι->string;
	Ŧ ToHex( const T& x )ι->string requires requires{ x.data(); x.size(); }{ return ToHex( std::as_bytes(std::span{x.data(), x.size()}) ); }//any contiguous container or span of any byte-sized element.
	Φ ToLower( sv source )ι->string;
	Φ ToUpper( sv source )ι->string;
	template<class T=uint, int Base=10> α TryTo( sv s )ι->optional<T>;//the whole of s, in range for T; Base 10 or 16.

	Ξ Trim( sv s )->sv{ return RTrim(LTrim(s)); }
	Ξ Trim( string&& s )->string{ return RTrim(LTrim(move(s))); }
	Φ TrimFirstLast( string&& s, char first, char last )ι->string;

	template<class Y=sv, class X> α ToView( const X& x )ι->Y{ return Y{x.data(),x.size()}; }
	template<class T> using bsv = std::basic_string_view<char,T>;
	template<class T=sv, class D=sv> α Split( bsv<typename T::traits_type> s, bsv<typename D::traits_type> delim )ι->vector<bsv<typename T::traits_type>>;

	struct ci_traits : public std::char_traits<char>{
		Ω eq( char c1, char c2 )ι{ return absl::ascii_toupper(c1) == absl::ascii_toupper(c2); }
		Ω ne( char c1, char c2 )ι{ return absl::ascii_toupper(c1) != absl::ascii_toupper(c2); }
		Ω lt( char c1, char c2 )ι{ return (unsigned char)absl::ascii_toupper(c1) < (unsigned char)absl::ascii_toupper(c2); }//unsigned, as char_traits<char>::lt orders.
		Ω compare( const char* s1, const char* s2, uint n )ι->int{
			int y{};
			for( ; !y && n-- != 0; ++s1, ++s2 ){
				if( lt(*s1, *s2) ) y=-1;
				if( lt(*s2, *s1) ) y= 1;
			}
			return y;
		}
		Ω find( const char* s, uint n, char a )ι->const char*{
			while( n-- > 0 && absl::ascii_toupper(*s) != absl::ascii_toupper(a) )
				++s;
			return n==string::npos ? nullptr : s;
		}
	};
	using iv = bsv<Str::ci_traits>;
}
namespace Jde{
	constexpr Str::iv operator ""_iv( const char* x, uint len )ι{ return Str::iv(x, len); }

	Ξ ToSV( Str::iv x )ι->sv{ return Str::ToView<sv,Str::iv>(x); }
	Ξ ToIV( sv x )ι->Str::iv{ return Str::ToView<Str::iv,sv>(x); }

	Ŧ Str::Decode64( sv s, bool fileSafe, SL sl )ε->T{
		string y;
		if( !(fileSafe ? absl::WebSafeBase64Unescape(s, &y) : absl::Base64Unescape(s, &y)) )
			throw Exception{ sl, {}, "Error decoding base64" };
		if constexpr( std::same_as<T,string> )
			return y;
		else
			return T( y.begin(), y.end() );
	}

	Ŧ Str::Join( const T& collection, sv separator, bool quote )ι->string{
		return absl::StrJoin( collection, separator, [quote]( string* out, const auto& item ){//fmt, not absl's AlphaNum:  ids and enums format through their fmt formatters.
			if( quote )
				out->push_back( '"' );
			fmt::format_to( std::back_inserter(*out), "{}", item );
			if( quote )
				out->push_back( '"' );
		});
	}

	template<class T, class D> α Str::Split( bsv<typename T::traits_type> s_, bsv<typename D::traits_type> delim )ι->vector<bsv<typename T::traits_type>>{
		vector<bsv<typename T::traits_type>> tokens;
		if( s_.empty() )
			return tokens;
		uint i=0;
		bsv<typename D::traits_type> s{ s_.data(), s_.size() };
		for( uint next = s.find(delim); next!=string::npos; i=next+delim.size(), next = s.find(delim, i) )
			tokens.push_back( ToView(s.substr(i, next-i)) );
		if( i<s.size() )
			tokens.push_back( ToView(s.substr(i)) );
		return tokens;
	}

	template<class T, int Base> α Str::TryTo( sv s )ι->optional<T>{
		static_assert( Base==10 || Base==16, "Str::TryTo parses base 10 or 16 only." );
		T y;
		return ( Base==16 ? absl::SimpleHexAtoi(s, &y) : absl::SimpleAtoi(s, &y) ) ? optional<T>{ y } : nullopt;
	}
}
#undef Φ