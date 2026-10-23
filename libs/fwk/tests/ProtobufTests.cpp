#include <jde/fwk/io/protobuf.h>

#define let const auto

namespace Jde::Tests{
	//ToGuid copies at most 16 bytes, and no more than it is given:  an id field unset (proto3's "") or from a foreign writer
	//is short, a foreign one may be long.  Without the clamp a short input over-reads and a long one overflows the uuid -
	//both ASan aborts here.  The literal's ascending bytes pin the order too.
	TEST( ProtobufTests, ToGuidClampsToTheInputLength ){
		let g = Protobuf::ToGuid( "abc" );
		EXPECT_EQ( sv((const char*)g.data(), 3), "abc" );
		EXPECT_TRUE( std::all_of(g.begin()+3, g.end(), [](auto b){ return b==0; }) ) << "the tail past a short input is zero";
		EXPECT_EQ( Protobuf::ToGuid(""), uuid{} );
		let full = uuid{ {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15} };
		EXPECT_EQ( Protobuf::ToGuid(string{Protobuf::ToBytes(full)}+"xx"), full ) << "bytes past 16 are ignored";
	}
}