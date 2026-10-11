#pragma once
#include "Owned.h"

namespace Jde::Opc{
	Ξ ToSV( UA_String s )ι->sv{ return sv{ (const char*)s.data, s.length }; }
	Ξ ToString( UA_String s )ι->string{ return string{ (const char*)s.data, s.length }; }
	Ξ ToUV( sv s )ι->UA_String{ return { s.size(), (UA_Byte*)s.data() }; }
	//By size, not through c_str():  UA_String_fromChars measures with strlen, so an identifier or a display name holding
	//an embedded NUL was truncated at it
	Ξ AllocUAString( sv s )ι->UA_String{
		UA_String y;
		UA_ByteString_allocBuffer( &y, s.size() );
		if( s.size() )//an empty string leaves y.data as UA_EMPTY_ARRAY_SENTINEL and s.data() may be null - memcpy is UB
			::memcpy( y.data, s.data(), s.size() );
		return y;
	}

	struct UAString final : Owned<UA_String, UA_TYPES_STRING>, noncopyable{
		α ToString()Ι->string{ return Opc::ToString( *this ); }
	};
}