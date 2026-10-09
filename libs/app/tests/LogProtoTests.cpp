//The Logging::Entry <-> wire conversions.  The client sends text, the file stores md5 ids, and the app server converts
//between them - so what these have to hold is that no field is dropped or transposed and the ids stay the ones the
//senders compute.
#include <gtest/gtest.h>
#include <jde/fwk/str.h>
#include <jde/app/proto/LogProto.h>
#include "helpers.h"

#define let const auto

namespace Jde::App::Tests{
	TEST( LogProtoTests, ClientEntryRoundTrip ){
		let original = entry( tp(5), ELogLevel::Warning, 99, "text {} {}", {"a","b"}, "src/round.cpp", "RoundTrip", ELogTags::Test, UserPK{11} );
		let proto = LogProto::LogEntryClient( Logging::Entry{original} );
		EXPECT_EQ( proto.text(), "text {} {}" ); //the template travels, not the formatted message.
		ASSERT_EQ( proto.args_size(), 2 );
		EXPECT_EQ( proto.args(0), "a" );
		EXPECT_EQ( proto.args(1), "b" );
		EXPECT_EQ( (int)proto.level(), (int)ELogLevel::Warning ); //the conversion is a cast - the two enums have to line up.
		EXPECT_EQ( proto.tags(), (uint)ELogTags::Test );
		EXPECT_EQ( proto.line(), 99u );
		EXPECT_EQ( proto.user_pk(), 11u );
		EXPECT_EQ( proto.file(), "src/round.cpp" );
		EXPECT_EQ( proto.function(), "RoundTrip" );
		EXPECT_EQ( Protobuf::ToTimePoint(proto.time()), tp(5) );

		auto back = LogProto::FromLogEntry( Log::Proto::LogEntryClient{proto} );
		EXPECT_EQ( back.Text, "text {} {}" );
		EXPECT_EQ( back.Arguments, (vector<string>{"a","b"}) );
		EXPECT_EQ( back.Level, ELogLevel::Warning );
		EXPECT_EQ( back.Tags, ELogTags::Test );
		EXPECT_EQ( back.Line, 99u );
		EXPECT_EQ( back.Time, tp(5) );
		EXPECT_EQ( back.UserPK.Value, 11u );
		EXPECT_EQ( back.File(), "src/round.cpp" );
		EXPECT_EQ( back.Function(), "RoundTrip" );
		EXPECT_EQ( back.Id(), original.Id() ); //recomputed from the template, so it survives a round trip it was never sent on.
		EXPECT_EQ( back.Message(), "text a b" );
	}

	//The file format stores ids;  the text lives once in the string table.
	TEST( LogProtoTests, FileEntryStoresIdsNotText ){
		let e = entry( tp(1), ELogLevel::Error, 7, "boom {}", {"9"}, "src/file.cpp", "Fn", ELogTags::Test, UserPK{3} );
		let proto = LogProto::LogEntryFile( e );
		EXPECT_EQ( Protobuf::ToGuid(proto.template_id()), e.Id() );
		EXPECT_EQ( Protobuf::ToGuid(proto.file_id()), e.FileId() );
		EXPECT_EQ( Protobuf::ToGuid(proto.function_id()), e.FunctionId() );
		ASSERT_EQ( proto.args_size(), 1 );
		EXPECT_EQ( Protobuf::ToGuid(proto.args(0)), Logging::Entry::GenerateId("9") );
		EXPECT_EQ( (int)proto.level(), (int)ELogLevel::Error );
		EXPECT_EQ( proto.tags(), (uint)ELogTags::Test );
		EXPECT_EQ( proto.line(), 7u );
		EXPECT_EQ( proto.user_pk(), 3u );
		EXPECT_EQ( Protobuf::ToTimePoint(proto.time()), tp(1) );
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

	//An older build wrote a forwarded entry as LogEntryFileExternal;  ToEntry folds it in, attribution and all.
	TEST( LogProtoTests, ToEntryFoldsAnOlderExternalEntry ){
		let e = entry( tp(1), ELogLevel::Error, 7, "boom {}", {"9"} );
		let external = LogProto::LogEntryFile( e, 5, 6 );
		let plain = LogProto::ToEntry( asOlderExternal(external) );
		EXPECT_EQ( Protobuf::ToGuid(plain.template_id()), e.Id() );
		EXPECT_EQ( Protobuf::ToGuid(plain.file_id()), e.FileId() );
		EXPECT_EQ( Protobuf::ToGuid(plain.function_id()), e.FunctionId() );
		ASSERT_EQ( plain.args_size(), 1 );
		EXPECT_EQ( Protobuf::ToGuid(plain.args(0)), Logging::Entry::GenerateId("9") );
		EXPECT_EQ( plain.level(), external.level() );
		EXPECT_EQ( plain.tags(), external.tags() );
		EXPECT_EQ( plain.line(), 7u );
		EXPECT_EQ( plain.user_pk(), external.user_pk() );
		EXPECT_EQ( Protobuf::ToTimePoint(plain.time()), tp(1) );
		EXPECT_EQ( plain.app_pk(), 5 );
		EXPECT_EQ( plain.app_instance_pk(), 6 );
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
		let bytes = Protobuf::SizePrefixed( fe );
		vector<Log::Proto::FileEntry> back;
		ASSERT_NO_THROW( back = Protobuf::DeserializeVector<Log::Proto::FileEntry>(sv{(const char*)bytes.data(), bytes.size()}) );
		ASSERT_EQ( back.size(), 1u );
		EXPECT_EQ( back[0].str().value(), arg );
	}
	//The same, on the wire a client's remote log sends to the app server.
	TEST( LogProtoTests, InvalidUtf8SurvivesTheClientWire ){
		const string arg{ "\xff\xfe" };
		Log::Proto::LogEntryClient parsed;
		ASSERT_TRUE( parsed.ParseFromString(LogProto::LogEntryClient(entry(tp(5), ELogLevel::Warning, 99, "text {}", {arg})).SerializeAsString()) );
		EXPECT_EQ( LogProto::FromLogEntry(move(parsed)).Arguments, vector<string>{arg} );
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
