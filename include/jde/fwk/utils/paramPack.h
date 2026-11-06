#pragma once

namespace Jde::ParamPack{
	//An argument as a log entry or exception stores it:  fmt's {}, which is what spdlog prints for the same field.  An ostream
	//wrote a bool as 1, a uint8_t as a raw character and a double to six significant digits.  Text is copied as is:  that is
	//what fmt prints for it, and a string type fmt has no formatter for (boost::json::string) still converts.
	Ŧ ToString( T&& x )ι->string{
		if constexpr( std::is_same_v<std::remove_cvref_t<T>, string> )
			return string{ FWD(x) };
		else if constexpr( std::is_convertible_v<const T&, sv> )
			return string{ sv{x} };
		else
			return Ƒ( "{}", x );
	}
	template<class... Args>
	α Append( vector<string>& values, Args&&... args )ι->void{
		values.reserve( values.size()+sizeof...(args) );
		( values.push_back(ToString(FWD(args))), ... );
	}
}