#pragma once
#include <jde/opc/openProtocol/Codec.h>

//A header of its own, so the fuzz target streams its inputs through it with no socket (tests/Harness.h).
namespace Jde::Opc::OpenProtocol{
	//A byte stream cut into frames by their lengths, never by the NUL (§3.8).  Bytes whose length does not read have lost
	//their place, and so have those after a frame whose length does not end at its NUL:  they are dropped through the next
	//NUL, where a frame can start again.
	struct Framer final{
		struct Noise{ string Start; };//the first bytes of what begins no frame.
		using Read = variant<std::monostate,string,Noise>;//nothing whole yet, a frame by its length, or noise.
		α Add( sv bytes )ι->void{ _buffer += bytes; }
		α Next()ι->Read{
			if( _lost ){
				const auto nul = _buffer.find( '\0' );
				_buffer.erase( 0, nul==string::npos ? nul : nul+1 );
				if( nul==string::npos )
					return {};
				_lost = false;
			}
			const auto noise = [this]{ _lost = true; return Noise{ _buffer.substr(0, HeaderSize) }; };
			//Only four digits make a length:  anything else is noise without FrameSize's refusal, whose text nothing here reads.
			if( !std::ranges::all_of(sv{ _buffer }.substr(0, 4), []( char c ){ return c>='0' && c<='9'; }) )
				return noise();
			const auto size = FrameSize( _buffer );
			if( !size )
				return noise();
			if( !*size || _buffer.size()<**size )
				return {};
			auto frame = _buffer.substr( 0, **size );
			_buffer.erase( 0, **size );
			_lost = !frame.ends_with( '\0' );//the decoder refuses it, and what follows begins no frame.
			return frame;
		}
	private:
		string _buffer;
		bool _lost{};
	};
}