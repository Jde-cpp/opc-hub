#include <jde/tests/Capture.h>
#include <jde/opc/openProtocol/logTags.h>
#include "helpers.h"

//The data field's rules (§2.3, Table 3), each through the smallest MID that has the field:  a number's digits and its
//blank, a text's characters and padding, a time, a parameter's id and the field's width.
namespace Jde::Opc::OpenProtocol::Tests{
	Τ Ω decoded( uint16_t revision, sv data )ι->Expected<T>{
		return Decode( Frame({.Mid=T::Id, .Revision=revision}, data) ).transform( []( Message&& m ){ return get<T>( move(m.Data) ); } );
	}
	Τ Ω refusal( uint16_t revision, sv data )ι->optional<ERefusal>{ return Refused( Frame({.Mid=T::Id, .Revision=revision}, data) ); }
	Ω dataOf( const Body& body, uint16_t revision=1 )ι->Expected<string>{
		return Encode( {.Revision=revision}, body ).transform( []( string&& frame ){ return frame.substr( HeaderSize, frame.size()-HeaderSize-1 ); } );
	}
	Ω valueRefused( const Body& body, uint16_t revision=1 )ι->bool{
		let y = dataOf( body, revision );
		return !y && y.error().Code==ERefusal::Value;
	}

	//A field of spaces is a value not supported or not defined:  not sent, and written back as spaces.
	TEST( FieldTests, BlankNumber ){
		let pset = decoded<Mid0018>( 1, "   " );
		ASSERT_TRUE( pset ) << pset.error().What;
		EXPECT_FALSE( pset->Pset );
		EXPECT_EQ( dataOf(Mid0018{}).value(), "   " );
		let blank = "01 "s+"02"+string( 5, ' ' )+"03 "+"04 "+"05"+string( 19, ' ' )+"06 ";
		let alarm = decoded<Mid0076>( 3, blank );
		ASSERT_TRUE( alarm ) << alarm.error().What;
		EXPECT_TRUE( *alarm==Mid0076{} );
		EXPECT_EQ( dataOf(Mid0076{}, 3).value(), blank );
	}

	TEST( FieldTests, NumberZeroPadded ){
		EXPECT_EQ( decoded<Mid0018>(1, "000").value().Pset, 0 );
		EXPECT_EQ( decoded<Mid0018>(1, "999").value().Pset, 999 );
		EXPECT_EQ( dataOf(Mid0018{.Pset=7}).value(), "007" );
		EXPECT_EQ( dataOf(Mid0064{.TighteningId=4294967295}, 7).value(), "4294967295" );
	}

	//The specification's own MID 0061 and 0065 examples write their tightening ids left-justified, spaces after them.
	TEST( FieldTests, NumberWithSpacesAfter ){
		EXPECT_EQ( decoded<Mid0064>(1, "345675    ").value().TighteningId, 345675u );
		EXPECT_EQ( decoded<Mid0018>(1, "7  ").value().Pset, 7 );
	}

	TEST( FieldTests, MixedNumber ){
		for( sv pset : {" 12", "1 2", "12a", "a12", "-12", "+12", "1.2", "0x1"} )
			EXPECT_EQ( refusal<Mid0018>(1, pset), ERefusal::Field ) << pset;
		EXPECT_EQ( refusal<Mid0064>(1, "    345675"), ERefusal::Field ) << "spaces before the digits";
	}

	TEST( FieldTests, NumberPastItsRange ){
		EXPECT_EQ( refusal<Mid0064>(1, "4294967296"), ERefusal::Field ) << "a tightening id is 4294967295 at most";
		EXPECT_EQ( refusal<Mid0064>(1, "9999999999"), ERefusal::Field );
		EXPECT_FALSE( refusal<Mid0064>(1, "4294967295") );
		for( let& c : Cases() ){
			if( c.Expected.Head.Mid!=Mid0035::Id || c.Expected.Head.Revision!=5 )
				continue;
			auto frame = c.Frame;
			let sequence = frame.find( "1165535" );
			ASSERT_NE( sequence, string::npos );
			frame.replace( sequence, 7, "1165536" );
			EXPECT_EQ( Refused(frame), ERefusal::Field ) << "a job sequence number is 65535 at most";
		}
	}

	TEST( FieldTests, WrongParameterId ){
		EXPECT_FALSE( refusal<Mid0071>(1, "01E404021031042008-06-02:10:14:26") );
		EXPECT_EQ( refusal<Mid0071>(1, "02E404021031042008-06-02:10:14:26"), ERefusal::Field );
		EXPECT_EQ( refusal<Mid0071>(1, "01E404021041042008-06-02:10:14:26"), ERefusal::Field ) << "04 where 03 belongs";
		EXPECT_EQ( refusal<Mid0071>(1, "01E404021x31042008-06-02:10:14:26"), ERefusal::Field );
		EXPECT_EQ( refusal<Mid0071>(1, " 1E404021031042008-06-02:10:14:26"), ERefusal::Field ) << "an id is two digits";
	}

	TEST( FieldTests, ShortField ){
		let whole = "01E404021031042008-06-02:10:14:26"sv;
		for( uint size=0; size<whole.size(); ++size )
			EXPECT_EQ( refusal<Mid0071>(1, whole.substr(0, size)), ERefusal::Field ) << size;
		EXPECT_EQ( refusal<Mid0018>(1, "12"), ERefusal::Field );
		EXPECT_EQ( refusal<Mid0018>(1, ""), ERefusal::Field );
		EXPECT_EQ( refusal<Mid0005>(1, "001"), ERefusal::Field );
	}

	//Bytes after the revision's last field, as a later firmware's parameters would be, are ignored and logged.
	TEST( FieldTests, BytesPastTheLayout ){
		Logging::Capture capture{ LogTag, ELogLevel::Debug };
		EXPECT_EQ( decoded<Mid0018>(1, "0120").value().Pset, 12 );
		EXPECT_TRUE( decoded<Mid0003>(1, "0") ) << "a MID with no data field";
		let health = decoded<Mid0071>( 1, "01E404021031042008-06-02:10:14:26051" );
		ASSERT_TRUE( health ) << health.error().What;
		EXPECT_TRUE( *health==(Mid0071{.ErrorCode="E404", .ControllerReady=1, .ToolReady=1, .Time=At(2008, 6, 2, 10, 14, 26)}) ) << "revision 3's tool health under a header of revision 1, read as revision 1";
		let lines = capture.Find( []( const Logging::Entry& e ){ return e.Message().ends_with(", the revision's last."); } );
		ASSERT_EQ( lines.size(), 3u );
		EXPECT_EQ( lines[0].Message(), "MID 0018 revision 1:  ignored '0', after byte 23, the revision's last." );
		EXPECT_EQ( lines[1].Message(), "MID 0003 revision 1:  ignored '0', after byte 20, the revision's last." );
		EXPECT_EQ( lines[2].Message(), "MID 0071 revision 1:  ignored '051', after byte 53, the revision's last." );
		//The cost:  a later revision's data field under an earlier revision's header, read by the earlier layout.
		EXPECT_EQ( decoded<Mid0004>(1, "0018002").value().Code, 0 ) << "revision 2's code 002 under a header of revision 1";
		//The first time for a MID and revision is a Warning, unless a test before had it.
		EXPECT_EQ( decoded<Mid0018>(1, "0120").value().Pset, 12 );
		let again = capture.Find( []( const Logging::Entry& e ){ return e.Message().starts_with("MID 0018 revision 1:"); } );
		ASSERT_EQ( again.size(), 2u );
		EXPECT_EQ( again[1].Level, ELogLevel::Debug );
	}

	TEST( FieldTests, TextWithoutItsPadding ){
		let alarm = decoded<Mid0074>( 2, "E40  " );
		ASSERT_TRUE( alarm );
		EXPECT_EQ( alarm->ErrorCode, "E40" );
		EXPECT_EQ( decoded<Mid0074>(2, " E40 ").value().ErrorCode, " E40" ) << "the spaces before stay";
		EXPECT_EQ( decoded<Mid0074>(2, "     ").value().ErrorCode, "" );
		EXPECT_EQ( decoded<Mid0074>(2, "~\x7F{} ").value().ErrorCode, "~\x7F{}" ) << "0x20 to 0x7F";
		EXPECT_EQ( dataOf(Mid0074{.ErrorCode="E4"}, 2).value(), "E4   " );
	}

	TEST( FieldTests, TextOutsideItsCharacters ){
		for( char control : {'\x1F', '\t', '\n', '\0'} ){
			EXPECT_EQ( refusal<Mid0074>(1, string{'E', control, '0', '6'}), ERefusal::Field ) << (int)(uint8_t)control;
			EXPECT_TRUE( valueRefused(Mid0074{.ErrorCode=string{'E', control}}) ) << (int)(uint8_t)control;
		}
		//No UTF-8, or a character past Latin-1's U+00FF:  a lone continuation byte, Latin-1's own é, a lead byte cut short,
		//an overlong é, a lead byte before a space, U+0100 and the euro sign.
		constexpr array<sv,8> notLatin1{ "E\x80", "\xE9", "\xFF", "E\xC3", "\xC1\xA9", "\xC3 ", "\xC4\x80", "\xE2\x82\xAC" };
		for( uint i=0; i<notLatin1.size(); ++i )
			EXPECT_TRUE( valueRefused(Mid0074{.ErrorCode=string{notLatin1[i]}}) ) << "case " << i;
		EXPECT_TRUE( valueRefused(Mid0050{.Vin="VIN \xE2\x82\xAC"}) );
	}

	//A byte past ASCII is Latin-1's character, held as UTF-8:  what code page a controller writes is Phase 10's to see,
	//and Latin-1 loses no byte of it.
	TEST( FieldTests, TextPastAscii ){
		EXPECT_EQ( decoded<Mid0074>(1, "\xC9\xFC\x80\xFF").value().ErrorCode, "\xC3\x89\xC3\xBC\xC2\x80\xC3\xBF" );
		EXPECT_EQ( decoded<Mid0050>(1, "S\xFC" "d   ").value().Vin, "S\xC3\xBC" "d" );
		EXPECT_EQ( dataOf(Mid0074{.ErrorCode="\xC3\x89"}).value(), "\xC9   " ) << "padded by characters, not UTF-8's bytes";
		EXPECT_EQ( dataOf(Mid0074{.ErrorCode="\xC3\x89\xC3\x89\xC3\x89\xC3\x89"}).value(), "\xC9\xC9\xC9\xC9" );
		EXPECT_TRUE( valueRefused(Mid0074{.ErrorCode="\xC3\x89\xC3\x89\xC3\x89\xC3\x89\xC3\x89"}) ) << "five characters at revision 1";
		string vin;
		for( uint i=0; i<25; ++i )
			vin += "\xC3\xBC";
		EXPECT_EQ( dataOf(Mid0050{.Vin=vin}).value(), string(25, '\xFC') );
		EXPECT_TRUE( valueRefused(Mid0050{.Vin=vin+"V"}) ) << "26 characters";
		for( uint b=0x20; b<=0xFF; ++b ){
			let wire = string{ 'E', (char)b, '0', '6' };
			let text = decoded<Mid0074>( 1, wire );
			ASSERT_TRUE( text ) << b << ":  " << text.error().What;
			EXPECT_EQ( dataOf(*text).value(), wire ) << b;
		}
	}

	TEST( FieldTests, TextPastItsWidth ){
		EXPECT_TRUE( valueRefused(Mid0074{.ErrorCode="E4061"}) ) << "four characters at revision 1";
		EXPECT_EQ( dataOf(Mid0074{.ErrorCode="E4061"}, 2).value(), "E4061" );
		EXPECT_TRUE( valueRefused(Mid0050{.Vin=string(26, 'V')}) );
		EXPECT_EQ( dataOf(Mid0050{.Vin=string(25, 'V')}).value(), string(25, 'V') );
	}

	//MID 0050's number runs to the data field's end, 25 characters at most.
	TEST( FieldTests, TextToTheEnd ){
		EXPECT_EQ( decoded<Mid0050>(1, "").value().Vin, "" );
		EXPECT_EQ( decoded<Mid0050>(1, "V").value().Vin, "V" );
		EXPECT_EQ( decoded<Mid0050>(1, "VIN 1   ").value().Vin, "VIN 1" );
		EXPECT_EQ( decoded<Mid0050>(1, string(25, 'V')).value().Vin, string(25, 'V') );
		EXPECT_EQ( refusal<Mid0050>(1, string(26, 'V')), ERefusal::Field );
		EXPECT_EQ( refusal<Mid0050>(1, "VIN\x01"), ERefusal::Field );
	}

	TEST( FieldTests, Time ){
		EXPECT_EQ( decoded<Mid0081>(1, "2026-11-12:06:30:00").value().Time, At(2026, 11, 12, 6, 30, 0) );
		EXPECT_EQ( decoded<Mid0081>(1, "2024-02-29:23:59:59").value().Time, At(2024, 2, 29, 23, 59, 59) );
		EXPECT_EQ( decoded<Mid0081>(1, "0000-01-01:00:00:00").value().Time, At(0, 1, 1, 0, 0, 0) );
		EXPECT_EQ( decoded<Mid0081>(1, "9999-12-31:23:59:59").value().Time, At(9999, 12, 31, 23, 59, 59) );
		EXPECT_FALSE( decoded<Mid0081>(1, string(19, ' ')).value().Time ) << "not sent";
		EXPECT_EQ( dataOf(Mid0081{.Time=At(2026, 1, 2, 3, 4, 5)}).value(), "2026-01-02:03:04:05" );
		EXPECT_EQ( dataOf(Mid0081{}).value(), string(19, ' ') );
		for( sv time : {"2026-11-12 06:30:00", "2026/11/12:06:30:00", "2026-11-12:06.30.00", "2026-11-12:06:30:0 ", "2026-11-12:06:30   ", " 026-11-12:06:30:00", "20261112063000     ",
			"2026-1x-12:06:30:00", "0000-00-00:00:00:0\x7F"} )
			EXPECT_EQ( refusal<Mid0081>(1, time), ERefusal::Field ) << time;
		//Digits in the shape that name no date or time of day are a controller's placeholder, read as not set.
		for( sv time : {"0000-00-00:00:00:00", "2026-00-12:06:30:00", "2026-13-12:06:30:00", "2026-11-00:06:30:00", "2026-11-31:06:30:00", "2026-02-29:06:30:00",
			"2026-11-12:24:00:00", "2026-11-12:06:60:00", "2026-11-12:06:30:60", "9999-99-99:99:99:99"} ){
			let unset = decoded<Mid0081>( 1, time );
			ASSERT_TRUE( unset ) << time << ":  " << unset.error().What;
			EXPECT_FALSE( unset->Time ) << time;
		}
		EXPECT_TRUE( valueRefused(Mid0081{.Time=At(10'000, 1, 1, 0, 0, 0)}) );
		EXPECT_TRUE( valueRefused(Mid0081{.Time=At(-1, 12, 31, 23, 59, 59)}) );
		EXPECT_TRUE( valueRefused(Mid0081{.Time=LocalTime::max()}) );
		EXPECT_TRUE( valueRefused(Mid0081{.Time=LocalTime::min()}) );
	}

	//A torque is the wire's integer over 100, and goes back to the nearest hundredth.
	TEST( FieldTests, Scaled ){
		//MID 0015 revision 2's data field, blank but for the parameter set and the torque min's six bytes at 83.
		let torque = []( sv digits ){
			let blank = []( sv id, uint width ){ return string{ id }+string( width, ' ' ); };
			return decoded<Mid0015>( 2, "01012"+blank("02", 25)+blank("03", 19)+blank("04", 1)+blank("05", 2)+"06"+string{ digits }
				+blank("07", 6)+blank("08", 6)+blank("09", 5)+blank("10", 5)+blank("11", 5)+blank("12", 6)+blank("13", 6) ).value().TorqueMin;
		};
		EXPECT_EQ( torque("000739"), 7.39 );
		EXPECT_EQ( torque("000000"), 0. );
		EXPECT_EQ( torque("999999"), 9999.99 );
		EXPECT_EQ( torque("001   "), 0.01 );
		EXPECT_FALSE( torque("      ") );
		let wire = []( double v ){ return dataOf( Mid0015{.TorqueMin=v}, 2 ).transform( []( string&& d ){ return d.substr( 62, 6 ); } ); };
		for( uint n=0; n<1'000'000; n+=997 )
			EXPECT_EQ( wire(n/100.0).value(), Ƒ("{:06}", n) );
		EXPECT_EQ( wire(7.394).value(), "000739" );
		EXPECT_EQ( wire(7.395001).value(), "000740" );
		EXPECT_EQ( wire(-0.0).value(), "000000" );
		for( double outside : {-0.01, 10'000., 9'999.999, 1e300, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()} )
			EXPECT_TRUE( valueRefused(Mid0015{.TorqueMin=outside}, 2) ) << outside;
	}

	TEST( FieldTests, NumberPastItsWidth ){
		EXPECT_TRUE( valueRefused(Mid0018{.Pset=1000}) );
		EXPECT_TRUE( valueRefused(Mid0038{.JobId=100}) ) << "two digits at revision 1";
		EXPECT_EQ( dataOf(Mid0038{.JobId=100}, 2).value(), "0100" );
		EXPECT_TRUE( valueRefused(Mid0038{.JobId=10'000}, 2) );
		EXPECT_TRUE( valueRefused(Mid0004{.Mid=18, .Code=100}) ) << "two digits at revision 1";
		EXPECT_EQ( dataOf(Mid0004{.Mid=18, .Code=100}, 2).value(), "0018100" );
		EXPECT_TRUE( valueRefused(Mid9997{.Mid=10'000}) );
	}

	//A member the revision does not carry is neither written nor read.
	TEST( FieldTests, MembersOfLaterRevisions ){
		EXPECT_EQ( dataOf(Mid0052{.Vin="V", .IdentifierPart2="2"}).value(), Padded("V", 25) );
		let job = decoded<Mid0038>( 1, "07" );
		ASSERT_TRUE( job );
		EXPECT_EQ( job->JobId, 7 );
	}
}