#include "helpers.h"

//docs/open-protocol/plan.md Phase 10a (#472):  what Jarrekstar's open-protocol-device-simulator sends, at the commit the
//plan pins, that is not the specification's tables', each frame as the simulator wrote it.  Every other frame it sent of
//the MIDs the driver speaks decoded as its table has it, which the gateway's DriverSimulatorTests check by their values.
namespace Jde::Opc::OpenProtocol{
namespace Tests{
	//Its MID 0002 through revision 3, and what revision 4 adds:  the RBU type at 19 characters for Table 16's 24, and the
	//serial number at 25 for its 10.
	Ω identity()ι->string{ return "010001020103"+Padded( "OpenProtocolSimulator", 25 )+"04SIM05"+Padded( "2.8.0", 19 )+"06"+Padded( "SIM-1.0.0", 19 )+"07"+Padded( "TOOL-1.0.0", 19 ); }
	Ω revision4()ι->string{ return identity()+"08"+Padded( "SIM-RBU", 19 )+"09"+Padded( "SIM0000000000000001", 25 ); }
}
	α Tests::Simulator()ι->vector<std::pair<string,string>>{
		return {
			{ "simulator-mid0002-r3", ExampleFrame("01250002003", identity()) },
			{ "simulator-mid0002-r4", ExampleFrame("01730002004", revision4()) },
			{ "simulator-mid0002-r5", ExampleFrame("01790002005", revision4()+"100111") },//the system type and subtype at 1 digit for Table 17's 3.
			{ "simulator-mid0002-r6", ExampleFrame("01890002006", revision4()+"1001111201301401") },//the station id at 2 digits for Table 18's 10, and no station name or client id.
			{ "simulator-mid0005-r7", ExampleFrame("00240005007", "0060") },//to MID 0060 at 7.
			{ "simulator-mid0004-r2", ExampleFrame("00260004002", "003820") },//to MID 0038 at 2:  code 20, Job can not be set.
			{ "simulator-mid0004-r3", ExampleFrame("00260004003", "007099") }//to MID 0070 at 3:  code 99, Unknown MID.
		};
	}
namespace Tests{
	Ω frame( sv file )ι->string{
		for( let& [name, frame] : Simulator() ){
			if( name==file )
				return frame;
		}
		ADD_FAILURE() << "no frame " << file;
		return {};
	}
	Ω refused( uint16_t mid, uint16_t code )ι->Body{ return Mid0004{ .Mid=mid, .Code=code }; }

	//Its acknowledgements carry the revision of the request they answer, where Table 24 gives MID 0005 one revision and
	//Tables 21 and 22 give MID 0004 a code of two digits at 1 and three at 2.  No request names an acknowledgement's
	//revision, and one the codec refused would leave its request to replyTimeout and the link to its loss, so each is read by
	//its data:  MID 0005 as Table 24's, MID 0004's code as two digits where its frame holds no third.  What the decoder
	//gives is the revision of the table it read by, which is the frame the encoder writes.
	TEST( SimulatorTests, AnAcknowledgementIsReadByItsData ){
		struct Read{ sv File; Body Data; string Encoded; };
		for( let& ack : {Read{"simulator-mid0005-r7", Mid0005{.Mid=60}, "00240005001001010000"s+"0060"+'\0'}, Read{"simulator-mid0004-r2", refused(38, 20), "00260004001001010000"s+"003820"+'\0'},
			Read{"simulator-mid0004-r3", refused(70, 99), "00260004001001010000"s+"007099"+'\0'}} ){
			let decoded = Decode( frame(ack.File) );
			ASSERT_TRUE( decoded ) << ack.File << ":  " << decoded.error().What;
			EXPECT_EQ( decoded->Head.Revision, 1 ) << ack.File;
			EXPECT_TRUE( decoded->Data==ack.Data ) << ack.File;
			EXPECT_EQ( Encode(*decoded).value_or("refused"), ack.Encoded ) << ack.File;
		}
		let three = Decode( ExampleFrame("00270004003", "0038020") );
		ASSERT_TRUE( three ) << three.error().What;
		EXPECT_EQ( three->Head.Revision, 2 ) << "Table 22's three digits, under a revision past it";
		EXPECT_TRUE( three->Data==refused(38, 20) );
		let first = Decode( ExampleFrame("00270004001", "0038020") );
		ASSERT_TRUE( first ) << first.error().What;
		EXPECT_TRUE( first->Data==refused(38, 2) ) << "revision 1 is Table 21's two digits, whatever follows them";

		EXPECT_EQ( DecodeHeader(frame("simulator-mid0005-r7")).value().Head.Revision, 7 ) << "the header as the wire has it";
		EXPECT_EQ( Refused(ExampleFrame("002400050 7", "0060")), ERefusal::Revision ) << "a revision that is no number is refused still";
		let asked = DecodeBody( {.Mid=5, .Revision=7}, "0060" );
		ASSERT_FALSE( asked );
		EXPECT_EQ( asked.error().Code, ERefusal::UnknownRevision ) << "a body is read by the header its caller gives";
		EXPECT_FALSE( Encode({.Revision=7}, Mid0005{.Mid=60}) ) << "the encoder writes Table 24's one";
	}

	//Its MID 0002 from revision 4 is not Tables 16 to 18's:  the reader finds the serial number's start where parameter 09's
	//id should be, at each of 4, 5 and 6, and the frame is refused.  A row against the simulator keeps MID 0002 at 3, which
	//reads.
	TEST( SimulatorTests, StartAcknowledgeFromRevision4IsNotTheTables ){
		let read = Decode( frame("simulator-mid0002-r3") );
		ASSERT_TRUE( read ) << read.error().What;
		const Body identity{ Mid0002{.CellId=1, .ChannelId=1, .ControllerName="OpenProtocolSimulator", .SupplierCode="SIM", .OpenProtocolVersion="2.8.0", .ControllerSoftwareVersion="SIM-1.0.0", .ToolSoftwareVersion="TOOL-1.0.0"} };
		EXPECT_TRUE( read->Data==identity );
		for( uint16_t revision=4; revision<=6; ++revision ){
			let refused = Decode( frame(Ƒ("simulator-mid0002-r{}", revision)) );
			ASSERT_FALSE( refused ) << revision;
			EXPECT_EQ( refused.error().Code, ERefusal::Field ) << revision;
			EXPECT_EQ( refused.error().What, Ƒ("MID 0002 revision {}, parameter 09 at byte 152:  '00' is not the parameter's id.", revision) );
		}
	}
}}