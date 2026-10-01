#include <jde/fwk/str.h>
#include <algorithm>
#include <boost/algorithm/hex.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fmt/args.h>

#ifdef _MSC_VER
	#include <jde/fwk/process/os/windows/WindowsUtilities.h>
#endif
#define let const auto

template<> α Jde::To<double>( sv x )ι->double{
	double y{}; //use 0.0 to keep consistent with the other To<T>.
	try{
		y = stod(string{x});
	}catch( const std::exception& e ){//not runtime_error: stod throws invalid_argument/out_of_range, both logic_error - narrowing this escapes the noexcept and terminates.
		DBGT( ELogTags::Parsing, "stod failed on '{}': {}", x, e.what() );
	}
	return y;
}

boost::uuids::string_generator _gen;
α Jde::ToUuid( sv s, SL sl )ε->uuid{
	try{
		return _gen( s.data(), s.data()+s.size() );
	}
	catch( const std::runtime_error& e ){
		throw Exception( sl, {ELogTags::Parsing}, "Invalid UUID string '{}': {}", s, e.what() );
	}
}
α Jde::ToString( const boost::uuids::uuid& u )ι->string{
	return boost::uuids::to_string( u );
}

namespace Jde{
	const string _empty;
	α Str::Empty()ι->str{ return _empty; };

	α Str::DecodeUri( sv x )ι->string{
		auto fromHex = []( char ch )->int {
			if( ch>='0' && ch<='9' )
				return ch-'0';
			ch = (char)::tolower( (unsigned char)ch );
			return ch>='a' && ch<='f' ? ch-'a'+10 : -1;
		};
		string y; y.reserve( x.size() );
		for( uint i=0; i<x.size(); ++i ){
			char ch = x[i];
			if( ch=='%' && i+2<x.size() ){//sv isn't null terminated - a trailing '%' must not read past the end.
				if( let hi = fromHex(x[i+1]), lo = fromHex(x[i+2]); hi>=0 && lo>=0 ){//invalid escapes pass through literally.
					ch = (char)( hi<<4 | lo );
					i += 2;
				}
			}
			else if( ch=='+' )
				ch = ' ';
			y += ch;
		}
		return y;
	}
	α Str::Format( sv format, vector<string> args )ε->string{
    fmt::dynamic_format_arg_store<fmt::format_context> store;
		for( auto&& arg : args )
			store.push_back( move(arg) );
    return fmt::vformat( format, store );
	}
	α Str::TryFormat( sv format, vector<string> args )ι->string{
		try{
			return Str::Format( format, move(args) );
		}
		catch( const std::exception& e ){
			string msg = Ƒ( "{}", format ); //args is moved
			DBGT( ELogTags::Parsing, "Format error: {}, error: {}", msg, e.what() );
			return msg;
		}
	}
	α Str::Replace( sv source, char find_, char replace )ι->string{
		string result{ source };
		std::ranges::replace( result, find_, replace );
		return result;
	}
	α Str::Replace( sv source, sv find, sv replace )ι->string{
		ASSERT( find.size() );
		if( find.empty() )
			return string{ source };//find("",i) returns i, so i+find.length() never advances
		string y; y.reserve( source.size() ); uint iLast{ 0 };
		for( uint i{}; (i = source.find(find, i))!=string::npos; iLast = (i=i+find.length()) ){
			y += source.substr( iLast, i-iLast );
			y += replace;
		}
		if( iLast<source.size() )
			y += source.substr( iLast, source.size()-iLast );

		return y;
	}
	α Str::Split( sv s, char delim )ι->vector<sv>{
		vector<sv> y;
		for( uint fieldStart=0, fieldEnd;fieldStart<s.size();fieldStart = fieldEnd+1 ){
			fieldEnd = std::min( s.find_first_of(delim, fieldStart), s.size() );
			sv v{ s.data()+fieldStart, fieldEnd-fieldStart };
			if( v.size() )
				y.push_back( v );
		}
		return y;
	}
	α Str::ToHex( std::span<const byte> bytes )ι->string{
		string hex;
		hex.reserve( bytes.size()*2 );
		boost::algorithm::hex_lower( (const char*)bytes.data(), (const char*)bytes.data()+bytes.size(), std::back_inserter(hex) );
		return hex;
	}

	Ω transform( sv source, int(*f)(int) )ι->string{
		string result{ source };
		std::ranges::transform( result, result.begin(), [f](char ch){ return (char)f((unsigned char)ch); } );//unsigned cast: tolower/toupper are ub for negative chars.
		return result;
	}
	α Str::ToLower( sv source )ι->string{ return transform(source, ::tolower); }
	α Str::ToUpper( sv source )ι->string{ return transform(source, ::toupper); }


	//Reads the code point that starts at x[i] into ch and returns the index just past it.  Bytes are read unsigned - a
	//signed char sign-extends into a negative code point, ub in the space check.  Only ASCII and the UTF-8 3-byte block
	//led by 0xE2 (U+2000..U+2FFF, general punctuation - where the Unicode spaces live) are decoded; any other byte is
	//returned raw, 0..255, and never matches a space.
	Ω isContinuation( unsigned char b )ι->bool{ return (b & 0xC0)==0x80; }
	Ω decodeAt( sv x, uint i, char32_t& ch )ι->uint{
		let b = (unsigned char)x[i];
		if( b==0xE2 && i+2<x.size() && isContinuation((unsigned char)x[i+1]) && isContinuation((unsigned char)x[i+2]) ){
			ch = 0x2000u | (((unsigned char)x[i+1] & 0x3Fu) << 6) | ((unsigned char)x[i+2] & 0x3Fu);
			return i+3;
		}
		ch = b;
		return i+1;
	}
	//The same reading backwards: the code point that ends at x[i-1], returning where it starts.
	Ω decodeBefore( sv x, uint i, char32_t& ch )ι->uint{
		if( i>=3 && (unsigned char)x[i-3]==0xE2 && isContinuation((unsigned char)x[i-2]) && isContinuation((unsigned char)x[i-1]) ){
			ch = 0x2000u | (((unsigned char)x[i-2] & 0x3Fu) << 6) | ((unsigned char)x[i-1] & 0x3Fu);
			return i-3;
		}
		ch = (unsigned char)x[i-1];
		return i-1;
	}

	Ω isSpace( char32_t ch )->bool{
		if( ch<0x80 )
			return std::isspace( (int)ch )!=0;
		return (ch>=0x2000 && ch<=0x200A) || ch==0x2028 || ch==0x2029 || ch==0x202F || ch==0x205F;//Unicode spaces in the general-punctuation block.
	}

	//Both ends decode code points, so a U+2003 trims from the right exactly as it does from the left - it used to be
	//byte-wise on the right and in the string&& overloads.  The predicate is a template parameter, not a std::function.
	Ṫ ltrim( sv s, T&& isTrimmed )ι->sv{
		uint i=0;
		for( char32_t ch; i<s.size(); ){
			let next = decodeAt( s, i, ch );
			if( !isTrimmed(ch) )
				break;
			i = next;
		}
		return s.substr( i );
	}
	Ṫ rtrim( sv s, T&& isTrimmed )ι->sv{
		uint i=s.size();
		for( char32_t ch; i>0; ){
			let start = decodeBefore( s, i, ch );
			if( !isTrimmed(ch) )
				break;
			i = start;
		}
		return s.substr( 0, i );
	}

	α Str::LTrim( sv s )->sv{ return ltrim( s, isSpace ); }
	α Str::RTrim( sv s )->sv{ return rtrim( s, isSpace ); }
	α Str::LTrim( string&& s )->string{ let t = LTrim( sv{s} ); return t.size()==s.size() ? move(s) : string{t}; }//untrimmed: hand the caller's string back, no copy.
	α Str::RTrim( string&& s )->string{ let t = RTrim( sv{s} ); return t.size()==s.size() ? move(s) : string{t}; }
	α Str::TrimFirstLast( string&& s, char first, char last )ι->string{
		bool found{};
		auto f = [&found]( char bracket, char32_t ch ){
			let skip = ch==(char32_t)(unsigned char)bracket && !found;
			if( skip )
				found = true;
			return skip || isSpace( ch );
		};
		sv trimmed = ltrim( s, [&](char32_t ch){return f(first, ch);} );//spaces and at most one `first`.
		if( found ){//only strip a `last` when a `first` was taken - an unmatched closing bracket is content.
			found = false;
			trimmed = rtrim( trimmed, [&](char32_t ch){return f(last, ch);} );
		}
		else
			trimmed = RTrim( trimmed );
		return trimmed.size()==s.size() ? move(s) : string{trimmed};
	}

	α Str::StartsWithInsensitive( sv value, sv starting )ι->bool{
		bool equal = starting.size() <= value.size();
		if( equal ){
			for( sv::size_type i=0; i<starting.size(); ++i ){
				equal = ::toupper( starting[i] )==::toupper( value[i] );
				if( !equal )
					break;
			}
		}
		return equal;
	}
}