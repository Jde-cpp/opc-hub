//The `hist` block both hosts read; only the path's default is the host's.
#include <jde/historian/Historian.h>

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;

	TEST( SettingsTests, Defaults ){
		let settings = Settings{ jobject{}, "logs/hist/opc-server" };
		EXPECT_EQ( settings.Path, "logs/hist/opc-server" );
		EXPECT_EQ( settings.Delay, 1min );
		EXPECT_EQ( settings.MaxBuffer, 64*1024*1024 );
		EXPECT_EQ( settings.TimeZone, locate_zone("UTC") );//not the machine's zone, as the log's is.
		EXPECT_EQ( settings.ReadLimit, 10'000 );
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

	TEST( SettingsTests, Invalid ){
		EXPECT_THROW( (Settings{parse(R"({"delay":"PT0S"})").as_object(), "hist"}), Exception );
		EXPECT_THROW( (Settings{parse(R"({"readLimit":0})").as_object(), "hist"}), Exception );
		EXPECT_THROW( (Settings{parse(R"({"path":""})").as_object(), "hist"}), Exception );
	}
}