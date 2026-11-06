//The log tests that need a live gateway.  Everything that needed only Jde.App.Shared and a filesystem - the daily
//file, the archive rounds and the QL read-back - moved to libs/app/tests/LogTests.cpp.
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/str.h>
#include <jde/web/client/socket/ClientSocketAwait.h>
#include <jde/app/client/RemoteLog.h>
#include "../src/GatewayAppClient.h"
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	struct LogTests : public ::testing::Test{};

	//What RemoteLog sends, the AppServer this process hosts logs again, rebuilt from the session's Strings (log.md #6).
	TEST_F( LogTests, Remote ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )//Logging::Find reads it:  run alone, no earlier suite has added one.
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		App::Client::RemoteLog remote{ {{"delay", "PT0.001S"}}, AppClient() };
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "Remote test message {}", 7 };
		remote.Write( e );
		remote.Shutdown();
		vector<Logging::Entry> found;
		for( uint i=0; i<200 && found.empty(); ++i ){
			std::this_thread::sleep_for( 10ms );
			found = Logging::Find( e.Id() );
		}
		ASSERT_EQ( found.size(), 1u ) << "the AppServer never logged the entry";
		EXPECT_EQ( found[0].Message(), "Remote test message 7" );
		EXPECT_EQ( found[0].Arguments, e.Arguments );
		EXPECT_EQ( found[0].File(), e.File() );
		EXPECT_EQ( found[0].FunctionId(), e.FunctionId() );
	}

	TEST_F( LogTests, LogTagsIntrospection ){
		auto q = "__type( name: \"logTags\" ){ enumValues{id name description} }";
		let value = Socket().QuerySync( move(q), {} );
		TRACE( "Received: {}", serialize(value) );
	}
}