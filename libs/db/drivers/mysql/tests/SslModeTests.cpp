#include <gtest/gtest.h>
#include "../src/MySqlSslMode.h"

#define let const auto

namespace Jde::DB::MySql::Tests{
	using boost::mysql::ssl_mode;
	//#233: the server block's `ssl` key.  Header-only, nothing connects.
	TEST( SslModeTests, ParsesTheThreeModes ){
		EXPECT_EQ( ToSslMode("disable"), ssl_mode::disable );
		EXPECT_EQ( ToSslMode("enable"), ssl_mode::enable );
		EXPECT_EQ( ToSslMode("require"), ssl_mode::require );
	}

	//Anything else is a typo that would otherwise silently pick a default.
	TEST( SslModeTests, RejectsAnythingElse ){
		for( let v : {"", "Enable", "yes", "true", "preferred", "verify_ca"} )
			EXPECT_THROW( ToSslMode(v), Exception ) << v;
	}

	TEST( SslModeTests, NamesRoundTrip ){
		for( let m : {ssl_mode::disable, ssl_mode::enable, ssl_mode::require} )
			EXPECT_EQ( ToSslMode(SslModeName(m)), m );
	}
}
