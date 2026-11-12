#include "helpers.h"

//Each MID at each revision, both ways, against the frames Cases.cpp writes from the specification's tables, and the
//specification's own example frames decoded.
namespace Jde::Opc::OpenProtocol::Tests{
	//What `decoded` would be on the wire, to show beside the frame it came from where the two messages differ.
	Ω reencoded( const Message& decoded )ι->string{
		let frame = Encode( decoded );
		return frame ? *frame : frame.error().What;
	}

	TEST( TableTests, EachRevisionDecodes ){
		for( let& c : Cases() ){
			let size = FrameSize( c.Frame );
			ASSERT_TRUE( size && *size ) << c.Name;
			EXPECT_EQ( **size, c.Frame.size() ) << c.Name;
			let decoded = Decode( c.Frame );
			ASSERT_TRUE( decoded ) << c.Name << ":  " << decoded.error().What;
			EXPECT_TRUE( *decoded==c.Expected ) << c.Name << " decoded to what encodes as\n" << reencoded( *decoded ) << "\nfrom\n" << c.Frame;
		}
	}

	TEST( TableTests, EachRevisionEncodes ){
		for( let& c : Cases() ){
			let frame = Encode( c.Expected );
			ASSERT_TRUE( frame ) << c.Name << ":  " << frame.error().What;
			EXPECT_EQ( *frame, c.Frame ) << c.Name;
		}
	}

	//A type added to Body, or a revision to a type, without its case fails here.
	TEST( TableTests, CasesCoverEveryRevision ){
		std::set<std::pair<uint16_t,uint16_t>> covered;
		for( let& c : Cases() ){
			EXPECT_EQ( c.Expected.Head.Mid, MidOf(c.Expected.Data) ) << c.Name;
			EXPECT_TRUE( covered.emplace(c.Expected.Head.Mid, c.Expected.Head.Revision).second ) << c.Name << " twice";
		}
		uint revisions{};
		for( let& [mid, last] : Revisions() ){
			for( uint16_t r=1; r<=last; ++r, ++revisions )
				EXPECT_TRUE( covered.contains({mid, r}) ) << "MID " << mid << " revision " << r << " has no case";
		}
		EXPECT_EQ( covered.size(), revisions );
		EXPECT_EQ( Revisions().size(), 41u ) << "0001 to 0005, 0014 to 0018, 0034 to 0038, 0042, 0043, 0050 to 0054, 0060 to 0065, 0070 to 0077, 0080, 0081 and 9997 to 9999";
	}

	//A message decodes by the revision in its own header:  no call here names one.
	TEST( TableTests, DecodesByItsOwnRevision ){
		for( let& c : Cases() ){
			let head = DecodeHeader( c.Frame );
			ASSERT_TRUE( head ) << c.Name;
			EXPECT_EQ( head->Head, c.Expected.Head ) << c.Name;
			EXPECT_EQ( head->Length+1u, c.Frame.size() ) << c.Name;
			let body = DecodeBody( head->Head, sv{c.Frame}.substr(HeaderSize, head->Length-HeaderSize) );
			ASSERT_TRUE( body ) << c.Name << ":  " << body.error().What;
			EXPECT_TRUE( *body==c.Expected.Data ) << c.Name;
		}
	}

	//A revision past a type's last is refused as that, which a controller's side answers with MID 0004 code 97.
	TEST( TableTests, RevisionPastTheLast ){
		for( let& [mid, last] : Revisions() ){
			let frame = Frame( {.Mid=mid, .Revision=(uint16_t)(last+1)}, {} );
			EXPECT_EQ( Refused(frame), ERefusal::UnknownRevision ) << "MID " << mid;
			let none = DecodeBody( {.Mid=mid, .Revision=0}, {} );//no frame's:  a header's 000 reads as 1.
			ASSERT_FALSE( none ) << "MID " << mid;
			EXPECT_EQ( none.error().Code, ERefusal::UnknownRevision ) << "MID " << mid;
		}
	}

	TEST( TableTests, SpecificationExamples ){
		for( let& c : Examples() ){
			let decoded = Decode( c.Frame );
			ASSERT_TRUE( decoded ) << c.Name << ":  " << decoded.error().What;
			EXPECT_TRUE( *decoded==c.Expected ) << c.Name << " decoded to what encodes as\n" << reencoded( *decoded ) << "\nfrom\n" << c.Frame;
		}
	}

	//§1.4:  an ask at a revision the controller lacks is answered at the one it has.  MID 0051 asks for MID 0052 at
	//revision 2, and a controller with the VIN alone answers at 1, which its header says.
	TEST( TableTests, Mid0052AnsweredAtRevision1 ){
		let ask = Encode( {.Revision=2}, Mid0051{} );
		ASSERT_TRUE( ask );
		EXPECT_EQ( *ask, "00200051002001010000\0"s );
		let answer = Decode( ExampleFrame("00450052001", Padded("KPOL3456JKLO897", 25)) );
		ASSERT_TRUE( answer ) << answer.error().What;
		EXPECT_EQ( answer->Head.Revision, 1 );
		let& vin = get<Mid0052>( answer->Data );
		EXPECT_EQ( vin.Vin, "KPOL3456JKLO897" );
		EXPECT_TRUE( vin.IdentifierPart2.empty() && vin.IdentifierPart3.empty() && vin.IdentifierPart4.empty() );
	}
}