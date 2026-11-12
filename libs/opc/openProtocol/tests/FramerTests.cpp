#include "helpers.h"
#include "../src/Framer.h"

//§3.8:  a stream's frames by their lengths, from bytes alone.  The session's cases have the rest through a socket.
namespace Jde::Opc::OpenProtocol::Tests{
	//A frame whose length stops short of its NUL:  the rest of it begins no frame, and is dropped through the next NUL rather
	//than read as a length that would swallow the frames after it.
	TEST( FramerTests, LengthShortOfItsNul ){
		Framer framer;
		let after = Frame( {.Mid=9999}, {} );
		framer.Add( "00200061001         x0500\0"s+after );
		let cut = framer.Next();
		ASSERT_TRUE( holds_alternative<string>(cut) );
		EXPECT_EQ( get<string>(cut), "00200061001         x" ) << "its length's bytes, which the decoder refuses";
		let next = framer.Next();
		ASSERT_TRUE( holds_alternative<string>(next) ) << "the frame after it, not '0500' read as a length";
		EXPECT_EQ( get<string>(next), after );
		EXPECT_TRUE( holds_alternative<std::monostate>(framer.Next()) );
	}
}