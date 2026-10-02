//The `hist` block both hosts read; only the path's default is the host's.
#include <jde/historian/Historian.h>

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;

	TEST( SettingsTests, Defaults ){
		for( sv hist : {"{}", R"({"path":null,"delay":null,"maxBuffer":null,"timeZone":null,"readLimit":null})"} ){
			SCOPED_TRACE( hist );
			let settings = Settings{ parse(hist).as_object(), "logs/hist/opc-server" };
			EXPECT_EQ( settings.Path, "logs/hist/opc-server" );
			EXPECT_EQ( settings.Delay, 1min );
			EXPECT_EQ( settings.MaxBuffer, 64*1024*1024 );
			EXPECT_EQ( settings.TimeZone, locate_zone("UTC") );//not the machine's zone, as the log's is.
			EXPECT_EQ( settings.ReadLimit, 10'000 );
		}
	}

	TEST( SettingsTests, Parse ){
		let hist = parse( R"({"path":"/var/hist","delay":"PT30S","maxBuffer":1048576,"timeZone":"America/New_York","readLimit":500})" ).as_object();
		let settings = Settings{ hist, "logs/hist/opc-gateway" };
		EXPECT_EQ( settings.Path, "/var/hist" );
		EXPECT_EQ( settings.Delay, 30s );
		EXPECT_EQ( settings.MaxBuffer, 1048576 );
		EXPECT_EQ( settings.TimeZone->name(), "America/New_York" );
		EXPECT_EQ( settings.ReadLimit, 500 );
	}

	//A malformed value is an error, never its default.
	TEST( SettingsTests, Invalid ){
		for( sv hist : {
			R"({"path":""})", R"({"path":7})",
			R"({"delay":"PT0S"})", R"({"delay":"30s"})", R"({"delay":30})",
			R"({"maxBuffer":0})", R"({"maxBuffer":-1})", R"({"maxBuffer":1.5})", R"({"maxBuffer":"1048576"})",
			R"({"timeZone":"America/New_Yrok"})", R"({"timeZone":-5})",
			R"({"readLimit":0})", R"({"readLimit":-5})", R"({"readLimit":"500"})" }){
			SCOPED_TRACE( hist );
			EXPECT_THROW( (Settings{parse(hist).as_object(), "hist"}), Exception );
		}
	}
}