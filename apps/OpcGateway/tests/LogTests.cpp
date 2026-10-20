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

	TEST_F( LogTests, Remote ){
		App::Client::RemoteLog remote{ {{"delay", "PT0.001S"}}, AppClient() };
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "Test message" };
		remote.Write( e );
		remote.Shutdown();
		std::this_thread::sleep_for( 1s );
	}

	TEST_F( LogTests, LogTagsIntrospection ){
		auto q = "__type( name: \"logTags\" ){ enumValues{id name description} }";
		let value = Socket().QuerySync( move(q), {} );
		TRACE( "Received: {}", serialize(value) );
	}
}