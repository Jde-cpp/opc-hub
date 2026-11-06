//The Logging::Entry <-> wire conversions.  A number or a bool travels typed;  otherwise the file stores md5 ids and the client
//sends ids too, with each String once per session, so what these have to hold is that no field is dropped or transposed and
//the ids stay the ones the senders compute.
#include <gtest/gtest.h>
#include <jde/fwk/str.h>
#include <jde/app/log/ProtoLog.h>
#include <jde/app/proto/LogProto.h>
#include "helpers.h"

#define let const auto

namespace Jde::App::Tests{
	//A batch as the AppServer reads it, through the bytes the socket carries.
	Ω overTheWire( vector<Logging::Entry>&& entries, LogProto::SentStrings& sent, LogProto::ReceivedStrings& received )ι->LogProto::ReceivedStrings::Entries{
		Log::Proto::ClientBatch parsed;
		EXPECT_TRUE( parsed.ParseFromString(sent.Batch(move(entries)).SerializeAsString()) );
		return received.Read( move(parsed) );
	}

	TEST( LogProtoTests, ClientEntryRoundTrip ){
		let original = entry( tp(5), ELogLevel::Warning, 99, "text {} {}", {"a","b"}, "src/round.cpp", "RoundTrip", ELogTags::Test, UserPK{11} );
		LogProto::SentStrings sent;
		let batch = sent.Batch( {original} );
		ASSERT_EQ( batch.entries_size(), 1 );
		let& proto = batch.entries( 0 );
		EXPECT_EQ( Protobuf::ToGuid(proto.template_id()), original.Id() ); //the template travels by id, not as the formatted message.
		EXPECT_EQ( Protobuf::ToGuid(proto.file_id()), original.FileId() );
		EXPECT_EQ( Protobuf::ToGuid(proto.function_id()), original.FunctionId() );
		ASSERT_EQ( proto.args_size(), 2 );
		EXPECT_EQ( proto.args(0).text(), "a" );
		EXPECT_EQ( proto.args(1).text(), "b" );
		EXPECT_EQ( (int)proto.level(), (int)ELogLevel::Warning ); //the conversion is a cast - the two enums have to line up.
		EXPECT_EQ( proto.tags(), (uint)ELogTags::Test );
		EXPECT_EQ( proto.line(), 99u );
		EXPECT_EQ( proto.user_pk(), 11u );
		EXPECT_EQ( Protobuf::ToTimePoint(proto.time()), tp(5) );
		EXPECT_EQ( batch.strings_size(), 3 ) << "the template, file and function, none sent yet";

		LogProto::ReceivedStrings received;
		let read = received.Read( Log::Proto::ClientBatch{batch} );
		EXPECT_EQ( read.Unresolved, 0u );
		ASSERT_EQ( read.Built.size(), 1u );
		let& back = read.Built[0];
		EXPECT_EQ( back.Text(), "text {} {}" );
		EXPECT_EQ( back.Arguments, (vector<Logging::Argument>{"a","b"}) );
		EXPECT_EQ( back.Level, ELogLevel::Warning );
		EXPECT_EQ( back.Tags, ELogTags::Test );
		EXPECT_EQ( back.Line, 99u );
		EXPECT_EQ( back.Time, tp(5) );
		EXPECT_EQ( back.UserPK.Value, 11u );
		EXPECT_EQ( back.File(), "src/round.cpp" );
		EXPECT_EQ( back.Function(), "RoundTrip" );
		EXPECT_EQ( back.Id(), original.Id() );
		EXPECT_EQ( back.FileId(), original.FileId() );
		EXPECT_EQ( back.FunctionId(), original.FunctionId() );
		EXPECT_EQ( back.Message(), "text a b" );
	}
	//log.md #6:  each String crosses once per session, so a call site's second batch sends only its ids.
	TEST( LogProtoTests, EachStringIsSentOncePerSession ){
		auto line = []( string arg ){ return entry( tp(1), ELogLevel::Information, 1, "once {}", {move(arg)} ); };
		LogProto::SentStrings sent;
		let first = sent.Batch( {line("a")} );
		EXPECT_EQ( first.strings_size(), 3 );
		let second = sent.Batch( {line("b"), line("c")} );
		EXPECT_EQ( second.strings_size(), 0 ) << "the template, file and function went with the first batch";
		EXPECT_FALSE( second.reset() );

		LogProto::ReceivedStrings received;
		EXPECT_EQ( received.Read(Log::Proto::ClientBatch{first}).Built.size(), 1u );
		let read = received.Read( Log::Proto::ClientBatch{second} );
		ASSERT_EQ( read.Built.size(), 2u );
		EXPECT_EQ( read.Built[1].Message(), "once c" );
		EXPECT_EQ( read.Built[1].File(), "src/x.cpp" );
		EXPECT_EQ( received.Size(), 3u );
	}
	//Past its cap the client's table starts again, and its batch tells the AppServer to:  both tables stay bounded, and every
	//String an entry names is one the AppServer holds.
	TEST( LogProtoTests, ACapResetsBothEnds ){
		auto line = []( uint32_t n, string text ){ return entry( tp(n), ELogLevel::Information, n, move(text) ); };//one file and function.
		let cap = LogProto::StringCap{ .Count=4 };
		LogProto::SentStrings sent{ cap };
		LogProto::ReceivedStrings received{ cap };
		EXPECT_EQ( received.Read(sent.Batch({line(1, "one")})).Built.size(), 1u );
		EXPECT_EQ( received.Read(sent.Batch({line(2, "two")})).Built.size(), 1u );
		EXPECT_EQ( received.Size(), 4u );
		let batch = sent.Batch( {line(3, "three")} );
		EXPECT_TRUE( batch.reset() );
		EXPECT_EQ( batch.strings_size(), 3 ) << "the file and function again, with the new template";
		let read = received.Read( Log::Proto::ClientBatch{batch} );
		ASSERT_EQ( read.Built.size(), 1u );
		EXPECT_EQ( read.Built[0].Text(), "three" );
		EXPECT_EQ( read.Built[0].File(), "src/x.cpp" );
		EXPECT_EQ( received.Size(), 3u ) << "the AppServer kept the Strings from before the reset";
	}
	//The text counts as well as the Strings:  a few long ones reset both ends as many short ones do.
	TEST( LogProtoTests, ACapOnBytesResetsBothEnds ){
		let cap = LogProto::StringCap{ .Bytes=40 };
		LogProto::SentStrings sent{ cap };
		LogProto::ReceivedStrings received{ cap };
		let first = sent.Batch( {entry(tp(1), ELogLevel::Information, 1, string(30, 't'))} );
		EXPECT_FALSE( first.reset() );
		EXPECT_EQ( received.Read(Log::Proto::ClientBatch{first}).Built.size(), 1u );
		let second = sent.Batch( {entry(tp(2), ELogLevel::Information, 2, "short")} );
		EXPECT_TRUE( second.reset() ) << "30 bytes of template, 9 of file and 2 of function pass 40";
		let read = received.Read( Log::Proto::ClientBatch{second} );
		ASSERT_EQ( read.Built.size(), 1u );
		EXPECT_EQ( read.Built[0].Text(), "short" );
		EXPECT_EQ( received.Size(), 3u );
	}
	//reviews-issue/log-pass2.md #1:  a peer that keeps sending Strings without the reset SentStrings would send can't grow the
	//AppServer's table past the cap.  Its batch is refused whole, and a reset is taken as usual.
	TEST( LogProtoTests, ABatchPastTheCapWithoutAResetIsRefused ){
		LogProto::SentStrings sent;
		LogProto::ReceivedStrings received{ {.Count=3} };
		EXPECT_EQ( received.Read(sent.Batch({entry(tp(1), ELogLevel::Information, 1, "one")})).Built.size(), 1u );
		EXPECT_EQ( received.Size(), 3u );
		auto over = sent.Batch( {entry(tp(2), ELogLevel::Information, 2, "two")} );
		ASSERT_FALSE( over.reset() );
		EXPECT_THROW( received.Read(Log::Proto::ClientBatch{over}), Exception );
		EXPECT_EQ( received.Size(), 3u ) << "none of the refused batch's Strings were taken";
		over.set_reset( true );
		let read = received.Read( Log::Proto::ClientBatch{over} );
		EXPECT_EQ( read.Unresolved, 1u ) << "the reset cleared the file and function the first batch sent";
		EXPECT_EQ( received.Size(), 1u );
	}
	//A String sent again is taken once, so the bytes it counts toward the cap are the bytes the table holds.
	TEST( LogProtoTests, AStringSentTwiceCountsOnce ){
		auto line = []( uint32_t n ){ return entry( tp(n), ELogLevel::Information, n, "first" ); };
		LogProto::SentStrings sent;
		LogProto::ReceivedStrings received{ {.Bytes=17} };//"first", "src/x.cpp" and "Fn" are 16.
		EXPECT_EQ( received.Read(sent.Batch({line(1)})).Built.size(), 1u );
		auto again = sent.Batch( {line(2)} );
		*again.add_strings() = LogProto::ToString( Logging::Entry::GenerateId("first"), "first" );
		EXPECT_EQ( received.Read(move(again)).Built.size(), 1u );
		EXPECT_EQ( received.Size(), 3u );
		EXPECT_NO_THROW( received.Read(sent.Batch({line(3)})) ) << "the String sent twice was counted twice, past the cap";
	}
	//An entry naming a String this session wasn't sent is dropped, not logged without its text.  Its batch's Strings are kept,
	//since later batches name them.
	TEST( LogProtoTests, AnEntryNamingAnUnsentStringIsDropped ){
		LogProto::SentStrings sent;
		[[maybe_unused]] let unread = sent.Batch( {entry(tp(1), ELogLevel::Information, 1, "first")} );
		let second = sent.Batch( {entry(tp(2), ELogLevel::Information, 2, "second")} );
		LogProto::ReceivedStrings received;
		let read = received.Read( Log::Proto::ClientBatch{second} );
		EXPECT_EQ( read.Unresolved, 1u ) << "its file and function went with the first batch, which this end never read";
		EXPECT_TRUE( read.Built.empty() );
		EXPECT_EQ( received.Size(), 1u ) << "the second batch's own template";
	}
	//reviews-issue/log-pass2.md #3:  the file's dedupe and the archive's merge key a String by its id, so a client that bound its
	//own text to another template's id would rename that template's lines from every instance.  The AppServer checks each id
	//against its text's md5 and refuses the batch whole, its reset included.
	TEST( LogProtoTests, AStringWhoseIdIsntItsMd5IsRefused ){
		LogProto::SentStrings sent;
		LogProto::ReceivedStrings received;
		EXPECT_EQ( received.Read(sent.Batch({entry(tp(1), ELogLevel::Information, 1, "first")})).Built.size(), 1u );
		auto forged = sent.Batch( {entry(tp(2), ELogLevel::Information, 2, "heartbeat {}", {"x"})} );
		ASSERT_EQ( forged.strings_size(), 1 ) << "the template, the file and function having gone with the first batch";
		let victim = Logging::Entry::GenerateId( "Login failed for {}" );
		*forged.mutable_strings( 0 )->mutable_id() = Protobuf::ToBytes( victim );
		*forged.mutable_entries( 0 )->mutable_template_id() = Protobuf::ToBytes( victim );
		forged.set_reset( true );
		EXPECT_THROW( received.Read(Log::Proto::ClientBatch{forged}), Exception );
		EXPECT_EQ( received.Size(), 3u ) << "a refused batch takes nothing, and its reset clears nothing";

		auto empty = sent.Batch( {} );
		*empty.add_strings() = LogProto::ToString( victim, "" );//ProtoLog::AddString asserts an empty text has the empty md5.
		EXPECT_THROW( received.Read(move(empty)), Exception );
		EXPECT_EQ( received.Size(), 3u );
	}
	//An entry without a source, as a LogStack line below its threshold is, sends no file or function.
	TEST( LogProtoTests, ASourcelessEntryCrossesWithoutOne ){
		LogProto::SentStrings sent;
		let batch = sent.Batch( {entry(tp(1), ELogLevel::Information, 5, "no source", {}, "", "")} );
		EXPECT_EQ( batch.strings_size(), 1 );
		EXPECT_TRUE( batch.entries(0).file_id().empty() );
		EXPECT_TRUE( batch.entries(0).function_id().empty() );
		EXPECT_EQ( batch.entries(0).line(), 0u ) << "a line without its file says nothing";
		LogProto::ReceivedStrings received;
		let read = received.Read( Log::Proto::ClientBatch{batch} );
		ASSERT_EQ( read.Built.size(), 1u );
		EXPECT_TRUE( read.Built[0].File().empty() );
		EXPECT_TRUE( read.Built[0].Function().empty() );
	}

	//The file format stores ids;  the text lives once in the string table.
	TEST( LogProtoTests, FileEntryStoresIdsNotText ){
		let e = entry( tp(1), ELogLevel::Error, 7, "boom {}", {"9"}, "src/file.cpp", "Fn", ELogTags::Test, UserPK{3} );
		let proto = LogProto::LogEntryFile( e );
		EXPECT_EQ( Protobuf::ToGuid(proto.template_id()), e.Id() );
		EXPECT_EQ( Protobuf::ToGuid(proto.file_id()), e.FileId() );
		EXPECT_EQ( Protobuf::ToGuid(proto.function_id()), e.FunctionId() );
		ASSERT_EQ( proto.args_size(), 1 );
		EXPECT_EQ( Protobuf::ToGuid(proto.args(0).string_id()), Logging::Entry::GenerateId("9") );
		EXPECT_EQ( (int)proto.level(), (int)ELogLevel::Error );
		EXPECT_EQ( proto.tags(), (uint)ELogTags::Test );
		EXPECT_EQ( proto.line(), 7u );
		EXPECT_EQ( proto.user_pk(), 3u );
		EXPECT_EQ( Protobuf::ToTimePoint(proto.time()), tp(1) );
	}

	//log.md #11:  a number or a bool in a plain {} field is stored typed, with no String;  a field with a spec, and text, by id.
	//A typed one renders back as the entry did, the extremes and an exponent included.
	TEST( LogProtoTests, TypedArgumentsAreStoredTyped ){
		constexpr auto low = std::numeric_limits<int64_t>::min();
		constexpr auto high = std::numeric_limits<uint64_t>::max();
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "{} {} {} {} {} {:x} {}", low, high, 1e-7, false, 0.1f, 255, "text" };
		let proto = LogProto::LogEntryFile( e );
		ASSERT_EQ( proto.args_size(), 7 );
		EXPECT_EQ( proto.args(0).integer(), low );
		EXPECT_EQ( proto.args(1).unsigned_integer(), high );
		EXPECT_EQ( proto.args(2).real(), 1e-7 );
		ASSERT_TRUE( proto.args(3).has_flag() );
		EXPECT_FALSE( proto.args(3).flag() );
		EXPECT_EQ( proto.args(4).real(), 0.1 ) << "a float, as the double that renders as it did";
		EXPECT_EQ( Protobuf::ToGuid(proto.args(5).string_id()), Logging::Entry::GenerateId("ff") ) << "a field with a spec is its text";
		EXPECT_EQ( Protobuf::ToGuid(proto.args(6).string_id()), Logging::Entry::GenerateId("text") );
		for( int i=0; i<5; ++i )
			EXPECT_EQ( LogProto::ArgText(proto.args(i)), Logging::ArgText(e.Arguments[i]) ) << "argument " << i;
		EXPECT_FALSE( LogProto::ArgText(proto.args(6)) ) << "a string's id is the caller's to resolve";
	}
	TEST( LogProtoTests, TextThatLooksLikeANumberIsStoredById ){
		let e = entry( tp(1), ELogLevel::Error, 7, "boom {}", {"9"} );
		EXPECT_TRUE( LogProto::LogEntryFile(e).args(0).has_string_id() ) << "\"9\" is text here, whatever it looks like";
	}
	//An app's typed arguments reach the AppServer typed, so its file stores them as the app's own would;  empty text included.
	TEST( LogProtoTests, TypedArgumentsSurviveTheClientWire ){
		constexpr auto low = std::numeric_limits<int64_t>::min();
		constexpr auto high = std::numeric_limits<uint64_t>::max();
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "{} {} {} {} {:x} {} {}", low, high, 1e-7, true, 255, "text", "" };
		LogProto::SentStrings sent;
		LogProto::ReceivedStrings received;
		let read = overTheWire( {e}, sent, received );
		ASSERT_EQ( read.Built.size(), 1u );
		let& back = read.Built[0];
		EXPECT_EQ( back.Arguments, (vector<Logging::Argument>{low, high, 1e-7, true, "ff", "text", ""}) );
		EXPECT_EQ( back.Message(), e.Message() );
		let file = LogProto::LogEntryFile( back, 1, 2 );
		EXPECT_EQ( file.args(0).integer(), low ) << "stored typed by the AppServer";
		EXPECT_TRUE( file.args(6).has_string_id() );
	}

	TEST( LogProtoTests, FileEntryCanLeaveOutItsSource ){
		let e = entry( tp(1), ELogLevel::Error, 7, "boom", {}, "src/file.cpp", "Fn" );
		let none = LogProto::LogEntryFile( e, 0, 0, {false, false} );
		EXPECT_TRUE( none.file_id().empty() );
		EXPECT_TRUE( none.function_id().empty() );
		EXPECT_EQ( none.line(), 0u ) << "the line goes with the file";
		EXPECT_EQ( Protobuf::ToGuid(none.template_id()), e.Id() );
		let fileOnly = LogProto::LogEntryFile( e, 0, 0, {true, false} );
		EXPECT_EQ( Protobuf::ToGuid(fileOnly.file_id()), e.FileId() );
		EXPECT_EQ( fileOnly.line(), 7u );
		EXPECT_TRUE( fileOnly.function_id().empty() );
	}

	//The defaults, on a ProtoLog of its own:  file and line on warnings and above and on exceptions, the function on
	//exceptions alone - and a String record only for what some entry keeps.
	TEST( LogProtoTests, ProtoLogKeepsTheSourceItsSettingsPass ){
		let dir = fs::temp_directory_path()/"Jde.App.Tests.ProtoLogSource";
		{
			ProtoLog log{ jobject{{"path", dir.string()}} };
			let info = entry( tp(1), ELogLevel::Information, 1, "info", {}, "src/info.cpp", "InfoFn" );
			let warning = entry( tp(2), ELogLevel::Warning, 2, "warning", {}, "src/warning.cpp", "WarningFn" );
			let thrown = entry( tp(3), ELogLevel::Debug, 3, "thrown", {}, "src/thrown.cpp", "ThrownFn", ELogTags::Sql | ELogTags::Exception );
			for( let& e : {info, warning, thrown} )
				log.Write( e );
			flat_map<uuid,Log::Proto::LogEntryFile> byTemplate; flat_set<string> strings;
			for( auto& r : log.Entries() ){
				if( r.has_entry() )
					byTemplate.emplace( Protobuf::ToGuid(r.entry().template_id()), r.entry() );
				else if( r.has_str() )
					strings.emplace( r.str().value() );
			}
			ASSERT_EQ( byTemplate.size(), 3u );
			let& i = byTemplate.at( info.Id() ), w = byTemplate.at( warning.Id() ), t = byTemplate.at( thrown.Id() );
			EXPECT_TRUE( i.file_id().empty() && i.function_id().empty() && i.line()==0 ) << "Information keeps no source";
			EXPECT_EQ( Protobuf::ToGuid(w.file_id()), warning.FileId() );
			EXPECT_EQ( w.line(), 2u );
			EXPECT_TRUE( w.function_id().empty() ) << "the function only for exceptions";
			EXPECT_EQ( Protobuf::ToGuid(t.file_id()), thrown.FileId() ) << "an exception keeps its source at any level";
			EXPECT_EQ( Protobuf::ToGuid(t.function_id()), thrown.FunctionId() );
			EXPECT_EQ( t.line(), 3u );
			for( sv kept : {"src/warning.cpp", "src/thrown.cpp", "ThrownFn"} )
				EXPECT_TRUE( strings.contains(string{kept}) ) << kept;
			for( sv dropped : {"src/info.cpp", "InfoFn", "WarningFn"} )
				EXPECT_FALSE( strings.contains(string{dropped}) ) << "a String no entry names: " << dropped;
		}
		std::error_code ec;
		fs::remove_all( dir, ec );
	}

	//An entry the app server logs on behalf of a gateway/opc instance carries who it came from;  its own carry nothing.
	TEST( LogProtoTests, ForwardedEntryCarriesAttribution ){
		let e = entry( tp(1), ELogLevel::Error, 7, "boom" );
		let proto = LogProto::LogEntryFile( e, 5, 6 );
		EXPECT_EQ( proto.app_pk(), 5 );
		EXPECT_EQ( proto.app_instance_pk(), 6 );
		EXPECT_EQ( Protobuf::ToGuid(proto.template_id()), e.Id() );
		EXPECT_EQ( LogProto::LogEntryFile(e).app_pk(), 0 );
	}

	TEST( LogProtoTests, StringMessageCarriesTheRawId ){
		let id = Logging::Entry::GenerateId( "some text" );
		let s = LogProto::ToString( id, string{"some text"} );
		ASSERT_EQ( s.id().size(), 16u ); //the 16 raw md5 bytes, not the dashed spelling.
		EXPECT_EQ( Protobuf::ToGuid(s.id()), id );
		EXPECT_EQ( s.value(), "some text" );
	}

	//Log.proto's text fields were proto3 strings, which refuse to parse invalid UTF-8.  ProtoLog wrote such an argument anyway, so
	//from then on every read of the daily file threw and no archive round could run.  In memory here, not through ProtoLog:  a
	//regression would otherwise poison the daily file every later run of the suite reads.
	TEST( LogProtoTests, InvalidUtf8SurvivesTheFile ){
		const string arg{ "\xff\xfe" };
		Log::Proto::FileEntry fe;
		*fe.mutable_str() = LogProto::ToString( Logging::Entry::GenerateId(arg), string{arg} );
		string bytes;//a run of the daily file, as the record layer frames it.
		Log::Appender appender{ bytes, 0 };
		appender.Add( Log::Proto::FileEntry{fe} );
		appender.Seal();
		vector<Log::Proto::FileEntry> back;
		ASSERT_NO_THROW( back = Log::ParseRecords(bytes, "LogProtoTests") );
		ASSERT_EQ( back.size(), 1u );
		EXPECT_EQ( back[0].str().value(), arg );
	}
	//The same, on the wire a client's remote log sends to the app server.
	TEST( LogProtoTests, InvalidUtf8SurvivesTheClientWire ){
		const string arg{ "\xff\xfe" };
		LogProto::SentStrings sent;
		LogProto::ReceivedStrings received;
		let read = overTheWire( {entry(tp(5), ELogLevel::Warning, 99, "text {}", {arg})}, sent, received );
		ASSERT_EQ( read.Built.size(), 1u );
		EXPECT_EQ( read.Built[0].Arguments, vector<Logging::Argument>{arg} );
	}

	TEST( LogProtoTests, DebugStringOfAString ){
		let id = Logging::Entry::GenerateId( "some text" );
		Log::Proto::FileEntry fe;
		*fe.mutable_str() = LogProto::ToString( id, string{"some text"} );
		let idText = Jde::ToString( id );
		EXPECT_EQ( LogProto::DebugString(fe), Ƒ("[{}]some text", idText.substr(idText.size()-4)) ); //ids are abbreviated to their tail.
	}

	TEST( LogProtoTests, DebugStringOfAnEntry ){
		Log::Proto::FileEntry fe;
		*fe.mutable_entry() = LogProto::LogEntryFile( entry(tp(0), ELogLevel::Error, 7, "boom", {"9"}, "src/x.cpp", "Fn", ELogTags::Test, UserPK{4242}) );
		let s = LogProto::DebugString( fe );
		EXPECT_NE( s.find("Error"), string::npos );
		EXPECT_NE( s.find("test"), string::npos ); //ELogTags::Test
		let argId = Jde::ToString( Logging::Entry::GenerateId("9") );
		EXPECT_NE( s.find(argId.substr(argId.size()-4)), string::npos );
		//L8: the format had four placeholders and was handed five arguments, which fmt permits - so the user was dropped in silence.
		EXPECT_NE( s.find("4242"), string::npos ) << "the entry's user is missing from its debug string: " << s;
	}

	TEST( LogProtoTests, DebugStringOfAnEmptyFileEntry ){
		EXPECT_EQ( LogProto::DebugString(Log::Proto::FileEntry{}), "Error" ); //nothing set.
	}
}