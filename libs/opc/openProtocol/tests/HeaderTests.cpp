#include "helpers.h"
#include <jde/opc/openProtocol/logTags.h>

//Table 1's twenty bytes:  the defaults of its blank fields, the digits the encoder writes, §7.1's part characters and
//every way a frame is refused before its data field is read.
namespace Jde::Opc::OpenProtocol::Tests{
	//The header's twelve bytes after the MID, the revision, the no-ack flag, the station, the spindle, the sequence number
	//and the two part fields:  blank but for `bytes` from `at`.
	Ω after( uint at=0, sv bytes={} )ι->string{ return string( 12, ' ' ).replace( at, bytes.size(), bytes ); }
	constexpr uint NoAck{ 3 }, Station{ 4 }, Spindle{ 6 }, Sequence{ 8 }, Parts{ 10 };
	//A MID 0005 naming MID 0018 under those twelve bytes.
	Ω accepted( sv afterMid )ι->string{ return "00240005"+string{ afterMid }+"0018"+'\0'; }
	Ω header( sv afterMid )ι->Expected<FrameHeader>{ return DecodeHeader( accepted(afterMid) ); }
	Ω withoutNul( string frame )ι->string{ frame.pop_back(); return frame; }

	//§5.2.1's example, a communication start asking for MID 0002 at revision 3:  nine blank bytes after the revision.
	TEST( HeaderTests, BlankFieldsAreTheirDefaults ){
		let m = Decode( ExampleFrame("00200001003", {}) );
		ASSERT_TRUE( m ) << m.error().What;
		EXPECT_EQ( m->Head, (Header{.Mid=1, .Revision=3, .NoAck=false, .Station=1, .Spindle=1, .Sequence=0, .Parts=0, .Part=0}) );
		EXPECT_TRUE( holds_alternative<Mid0001>(m->Data) );
	}

	TEST( HeaderTests, RevisionOfSpacesOrZeroesIs1 ){
		for( sv revision : {"   ", "000", "001"} ){
			let head = header( after(0, revision) );
			ASSERT_TRUE( head ) << revision;
			EXPECT_EQ( head->Head.Revision, 1 ) << revision;
		}
		EXPECT_EQ( header(after(0, "002"))->Head.Revision, 2 );
		EXPECT_EQ( header(after(0, "999"))->Head.Revision, 999 );
	}

	TEST( HeaderTests, FieldsRead ){
		let head = header( "0011020345  " );
		ASSERT_TRUE( head ) << head.error().What;
		EXPECT_EQ( head->Length, 24 );
		EXPECT_EQ( head->Head, (Header{.Mid=5, .Revision=1, .NoAck=true, .Station=2, .Spindle=3, .Sequence=45}) );
		let zeroes = header( "001000000000" );
		ASSERT_TRUE( zeroes ) << zeroes.error().What;
		EXPECT_EQ( zeroes->Head, (Header{.Mid=5, .Station=0, .Spindle=0}) ) << "00 is the station it says, not the default";
	}

	TEST( HeaderTests, EncoderWritesDigits ){
		EXPECT_EQ( Encode({.Revision=3}, Mid0001{}).value(), "00200001003001010000\0"s );
		EXPECT_EQ( Encode({.Revision=7, .NoAck=true, .Station=2, .Spindle=3, .Sequence=45}, Mid0060{}).value(), "00200060007102034500\0"s );
		EXPECT_EQ( Encode({.Mid=9999, .Parts=1, .Part=1}, Mid0005{.Mid=18}).value(), "002400050010010100110018\0"s ) << "the MID is the body's";
	}

	//§7.1's table:  a part count from its one character.
	TEST( HeaderTests, PartCharacters ){
		struct Coded{ char Character; uint8_t Parts; };
		for( let& coded : {Coded{'1', 1}, Coded{'9', 9}, Coded{'A', 10}, Coded{'Z', 35}, Coded{'[', 36}, Coded{'^', 39}, Coded{'a', 40}, Coded{'z', 65},
			Coded{'{', 66}, Coded{'~', 69}, Coded{'!', 70}, Coded{'/', 84}, Coded{':', 85}, Coded{'@', 91}, Coded{'\xE0', 85}, Coded{'\xE7', 92}, Coded{'\xEE', 99}} ){
			let head = header( after(Parts, string{coded.Character, '1'}) );
			ASSERT_TRUE( head ) << coded.Character << ":  " << head.error().What;
			EXPECT_EQ( head->Head.Parts, coded.Parts ) << coded.Character;
			EXPECT_EQ( head->Head.Part, 1 ) << coded.Character;
		}
		for( sv notUsed : {"  ", "00", " 0", "0 "} ){
			let head = header( after(Parts, notUsed) );
			ASSERT_TRUE( head ) << notUsed;
			EXPECT_EQ( head->Head.Parts, 0 ) << notUsed;
			EXPECT_EQ( head->Head.Part, 0 ) << notUsed;
		}
	}

	TEST( HeaderTests, PartCharacterOutsideTheTable ){
		for( char outside : {'_', '`', '\x7F', '\x01', '\0', '\x80', '\xDF', '\xEF', '\xFF'} ){
			for( let& parts : {string{outside, '1'}, string{'1', outside}} ){
				let head = header( after(Parts, parts) );
				ASSERT_FALSE( head ) << (int)(uint8_t)outside;
				EXPECT_EQ( head.error().Code, ERefusal::Parts ) << head.error().What;
			}
		}
	}

	TEST( HeaderTests, PartsThatDisagree ){
		for( sv parts : {" 1", "01", "2 ", "20", "23", "1A"} ){
			let head = header( after(Parts, parts) );
			ASSERT_FALSE( head ) << parts;
			EXPECT_EQ( head.error().Code, ERefusal::Parts ) << parts;
		}
	}

	//One part of several has a header that reads, so a session can acknowledge it, and a body the codec does not join.
	TEST( HeaderTests, LinkedPartRefused ){
		ASSERT_TRUE( header(after(Parts, "21")) );
		EXPECT_EQ( Refused(accepted(after(Parts, "21"))), ERefusal::Linked );
		EXPECT_EQ( Refused(accepted(after(Parts, "22"))), ERefusal::Linked );
		EXPECT_FALSE( Refused(accepted(after(Parts, "11"))) ) << "a message of one part is whole";
		let two = Encode( {.Parts=2, .Part=1}, Mid0005{.Mid=18} );
		ASSERT_FALSE( two );
		EXPECT_EQ( two.error().Code, ERefusal::Linked );
	}

	TEST( HeaderTests, FrameSizeIsTheLengthAndItsNul ){
		EXPECT_FALSE( FrameSize("002").value() ) << "not the length's four digits yet";
		EXPECT_EQ( FrameSize("0020").value(), 21u );
		EXPECT_EQ( FrameSize("99990061").value(), 10'000u );
		for( sv length : {"0019", "0000", "002 ", " 020", "    ", "0x20", "-020"} ){
			let size = FrameSize( length );
			ASSERT_FALSE( size ) << length;
			EXPECT_EQ( size.error().Code, ERefusal::Length ) << length;
		}
	}

	TEST( HeaderTests, LengthRefused ){
		EXPECT_EQ( Refused("0020000100300101000"), ERefusal::Length ) << "nineteen bytes";
		EXPECT_EQ( Refused(""), ERefusal::Length );
		EXPECT_EQ( Refused(ExampleFrame("00190001003", {})), ERefusal::Length ) << "under the header's twenty";
		EXPECT_EQ( Refused(ExampleFrame("002 0001003", {})), ERefusal::Length );
		EXPECT_EQ( Refused(ExampleFrame("    0001003", {})), ERefusal::Length ) << "the length has no default";
		EXPECT_EQ( Refused(withoutNul(ExampleFrame("00240005", "001"))), ERefusal::Length ) << "a byte short of its length";
		EXPECT_EQ( Refused(ExampleFrame("00240005", "00180")), ERefusal::Length ) << "a byte past its length";
		//A length past 9999 has no four digits:  the field's largest, against a frame of 10 000 bytes and its NUL.
		EXPECT_EQ( Refused(ExampleFrame("99990050", string(9'980, 'A'))), ERefusal::Length );
		//A fifth digit reads as the MID's first:  a length of 1000, which the frame is not.
		EXPECT_EQ( Refused(ExampleFrame("10000005001001010000", string(9'980, 'A'))), ERefusal::Length );
	}

	TEST( HeaderTests, MissingNul ){
		EXPECT_EQ( Refused(withoutNul(ExampleFrame("00240005", "0018"))), ERefusal::Nul );
		EXPECT_EQ( Refused(withoutNul(ExampleFrame("00240005", "0018"))+' '), ERefusal::Nul );
		EXPECT_EQ( Refused(ExampleFrame("00240005", "001")), ERefusal::Nul ) << "the NUL a byte early";
		EXPECT_EQ( Refused(withoutNul(ExampleFrame("00200001003", {}))+'0'), ERefusal::Nul );
		EXPECT_FALSE( Refused(ExampleFrame("00240005", "0018")) );
	}

	TEST( HeaderTests, MixedDigitFields ){
		for( sv revision : {"0 1", " 01", "1  ", "00a", "-01", "0.1"} ){
			let head = header( after(0, revision) );
			ASSERT_FALSE( head ) << revision;
			EXPECT_EQ( head.error().Code, ERefusal::Revision ) << revision;
		}
		for( sv mid : {"00a5", " 005", "5   ", "    "} )
			EXPECT_EQ( Refused(ExampleFrame("0024"+string{mid}, "0018")), ERefusal::Header ) << mid;
		struct Field{ uint At; sv Bytes; };
		for( let& f : {Field{NoAck, "2"}, Field{NoAck, "x"}, Field{Station, "1 "}, Field{Station, " 1"}, Field{Station, "1x"}, Field{Spindle, "0 "}, Field{Spindle, "x1"},
			Field{Sequence, "4 "}, Field{Sequence, " 4"}, Field{Sequence, "-1"}} ){
			let head = header( after(f.At, f.Bytes) );
			ASSERT_FALSE( head ) << f.At << " '" << f.Bytes << "'";
			EXPECT_EQ( head.error().Code, ERefusal::Header ) << head.error().What;
		}
	}

	TEST( HeaderTests, UnknownMid ){
		for( sv mid : {"0900", "0006", "0000", "9996"} )
			EXPECT_EQ( Refused(ExampleFrame("0020"+string{mid}, {})), ERefusal::UnknownMid ) << mid;
		ASSERT_TRUE( DecodeHeader(ExampleFrame("00200900", {})) ) << "its header reads, so a session can acknowledge it";
	}

	TEST( HeaderTests, EncoderRefusals ){
		let code = []( Header head, const Body& body )ι->optional<ERefusal>{
			let frame = Encode( head, body );
			return frame ? nullopt : optional{ frame.error().Code };
		};
		EXPECT_EQ( code({.Revision=0}, Mid0001{}), ERefusal::UnknownRevision );
		EXPECT_EQ( code({.Revision=7}, Mid0001{}), ERefusal::UnknownRevision );
		EXPECT_EQ( code({.Revision=8}, Mid0061{}), ERefusal::UnknownRevision );
		EXPECT_FALSE( code({.Revision=7}, Mid0061{}) ) << "every field blank";
		EXPECT_EQ( code({.Station=100}, Mid0003{}), ERefusal::Value );
		EXPECT_EQ( code({.Spindle=100}, Mid0003{}), ERefusal::Value );
		EXPECT_EQ( code({.Sequence=100}, Mid0003{}), ERefusal::Value );
		EXPECT_FALSE( code({.Station=99, .Spindle=99, .Sequence=99}, Mid0003{}) );
	}

	//The framework reads the registered name, so a jsonnet `logging.spd.tags` block can name the tag, and a line under it
	//does not trap the debugger at the break level, as an OPC line does not.
	TEST( HeaderTests, LogTag ){
		EXPECT_EQ( ToLogTags(sv{"openProtocol"}), LogTag );
		EXPECT_EQ( ToName(LogTag), "openProtocol" );
		EXPECT_FALSE( BreaksFor(LogTag) );
		EXPECT_THROW( Logging::AddTags(TagNames), std::logic_error ) << "main.cpp registered it already";
	}

	TEST( HeaderTests, ErrorNames ){
		EXPECT_EQ( ErrorName(0), "No Error" );
		EXPECT_EQ( ErrorName(2), "Parameter set ID not present" );
		EXPECT_EQ( ErrorName(35), "Other master client already connected" );
		EXPECT_EQ( ErrorName(74), "Subscribed MID Revision unsupported" );
		EXPECT_EQ( ErrorName(76), "Requested MID Revision unsupported" );
		EXPECT_EQ( ErrorName(92), "Open protocol commands disabled" );
		EXPECT_EQ( ErrorName(97), "MID revision unsupported" );
		EXPECT_EQ( ErrorName(103), "Illegal program ID" );
		EXPECT_EQ( ErrorName(950), "Reserved for application specific error codes" );
		for( uint16_t unlisted : {5, 37, 45, 104, 899, 1000} )
			EXPECT_TRUE( ErrorName(unlisted).empty() ) << unlisted;
		EXPECT_EQ( LinkErrorName(1), "Invalid length" );
		EXPECT_EQ( LinkErrorName(3), "Invalid sequence number" );
		EXPECT_TRUE( LinkErrorName(0).empty() && LinkErrorName(5).empty() );
	}
}