#pragma once
#include "../src/Framer.h"

//What the fuzz target (fuzz/Fuzz.cpp) runs on each input and CorpusTests on each corpus file:  the decoder over bytes
//from anywhere, and the framer over them as a socket's stream.  Nothing may crash, and what the decoder takes must encode
//to a frame that decodes to the same message.
namespace Jde::Opc::OpenProtocol::Tests{
	//What `m` broke, empty when nothing.
	Ξ RoundTrip( const Message& m )ι->string{
		const auto frame = Encode( m );
		if( !frame )
			return "a decoded message does not encode:  "+frame.error().What;
		const auto size = FrameSize( *frame );
		if( !size || *size!=frame->size() )
			return "the encoded frame's size is not its length's";
		const auto again = Decode( *frame );
		if( !again )
			return "the encoded frame does not decode:  "+again.error().What;
		return *again==m ? string{} : "the encoded frame decodes to another message";
	}
	//What `bytes` broke as a stream, in two reads so a frame can straddle them, empty when nothing.  Each frame the
	//framer cuts that the decoder takes goes round as the input's does.
	Ξ Stream( sv bytes )ι->string{
		Framer framer;
		size_t cut{};
		const auto half = bytes.size()/2;
		for( const auto read : {bytes.substr(0, half), bytes.substr(half)} ){
			framer.Add( read );
			for( auto next = framer.Next(); !holds_alternative<std::monostate>(next); next = framer.Next() ){
				const auto frame = get_if<string>( &next );
				if( !frame )
					continue;
				cut += frame->size();
				if( frame->size()<HeaderSize || cut>bytes.size() )
					return "the framer cut a frame from bytes it was not given";
				if( frame->size()==bytes.size() )
					continue;//the whole input, which Exercise decodes.
				if( const auto m = Decode(*frame) ){
					if( auto broke = RoundTrip(*m); !broke.empty() )
						return broke;
				}
			}
		}
		return {};
	}
	//What `bytes` broke, empty when nothing.  A frame with a header that reads has its data field tried too, whatever
	//its length says, so the layouts are reached from an input whose length is off.
	Ξ Exercise( sv bytes )ι->string{
		if( auto broke = Stream(bytes); !broke.empty() )
			return broke;
		(void)FrameSize( bytes );
		if( const auto whole = Decode(bytes) )
			return RoundTrip( *whole );
		const auto head = DecodeHeader( bytes );
		if( !head )
			return {};
		auto data = bytes.substr( HeaderSize );
		if( data.ends_with('\0') )
			data.remove_suffix( 1 );
		const auto body = DecodeBody( head->Head, data );
		return body ? RoundTrip( {head->Head, *body} ) : string{};
	}
}