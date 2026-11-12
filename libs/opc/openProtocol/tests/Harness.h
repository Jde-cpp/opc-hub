#pragma once
#include <jde/opc/openProtocol/Codec.h>

//What the fuzz target (fuzz/Fuzz.cpp) runs on each input and CorpusTests on each corpus file:  the decoder over bytes
//from anywhere.  Nothing may crash, and what it takes must encode to a frame that decodes to the same message.
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
	//What `bytes` broke, empty when nothing.  A frame with a header that reads has its data field tried too, whatever
	//its length says, so the layouts are reached from an input whose length is off.
	Ξ Exercise( sv bytes )ι->string{
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