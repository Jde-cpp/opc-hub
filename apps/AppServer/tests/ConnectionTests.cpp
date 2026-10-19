#include <jde/db/db.h>
#include <jde/db/IDataSource.h>
#include <jde/db/Row.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include "helpers.h"
#include "../src/appStartup.h"
#include "../src/WebServer.h"
#include <jde/fwk/io/Cache.h>
#include <thread>
#define let const auto

//the connections bookkeeping in appStartup.cpp: AddConnection's proc-minted pks and EndConnection's deleted stamp.
namespace Jde::App::Server::Tests{
	using EnumMap = flat_map<uint,string>;//aliased: the comma in the template args would split the EXPECT_ macro's arguments.

	struct ConnectionTests : ::testing::Test{
		Ω ConnectionsTable()->string{ return Server::AppSchema()->GetView("connections").DBName; }
		Ω LiveCount( App::ConnectionPK connectionId )->uint{
			auto rows = Server::AppSchema()->DS()->Select( {Ƒ("select count(*) from {} where connection_id=? and deleted is null", ConnectionsTable()), {DB::Value{connectionId}}} );
			return rows.empty() ? 0u : (uint)rows[0].Get<_int>( 0 );
		}
	};

	//appserver-review2 r2.24: every enum/flags lookup is loaded before the server starts, so no request path takes
	//SelectEnumSync's blocking cache miss - on MySQL that miss parks the caller on the very pool that has to answer it.
	TEST_F( ConnectionTests, EnumsAreLoadedAtStartup ){
		uint checked{};
		for( let& schema : Server::Schemas() ){
			for( let& [name, table] : schema->Tables ){
				if( !table->IsEnum() && !table->IsFlags )
					continue;
				++checked;
				EXPECT_TRUE( Cache::Get<EnumMap>(table->Name) ) << table->Name << " was not loaded - its next render blocks the caller";
			}
		}
		EXPECT_GT( checked, 0u ) << "no lookup tables found - the walk no longer matches what SelectAwait renders from";
	}

	//`hosts` has the enum shape but grows, and loading it with no expiry means nothing else would ever refresh it.
	TEST_F( ConnectionTests, ANewHostReachesTheLoadedMap ){
		let host = Ƒ( "load-host-{}", steady_clock::now().time_since_epoch().count() );
		App::AddConnection( "Tests.Connections", "loads-host", host, 116 );
		auto hosts = Cache::Get<EnumMap>( "hosts" );
		ASSERT_TRUE( hosts );
		EXPECT_TRUE( std::ranges::any_of(*hosts, [&](let& kv){ return kv.second==host; }) ) << "a host added after startup never reaches the map the renderer reads";
	}

	TEST_F( ConnectionTests, AddConnectionMintsPks ){
		let [program, instance, connection] = App::AddConnection( "Tests.Connections", "mints", "connection-host", 111 );
		EXPECT_NE( program, 0u );
		EXPECT_NE( instance, 0u );
		EXPECT_NE( connection, 0u );
		EXPECT_EQ( LiveCount(connection), 1u );
	}

	//same program+instance+host reuses the program/instance rows; every call is a fresh connection row and - a
	//reconnect implies the old socket died - the insert proc stamps the instance's prior live connections deleted.
	TEST_F( ConnectionTests, AddConnectionReusesProgramAndInstance ){
		let [program1, instance1, connection1] = App::AddConnection( "Tests.Connections", "reuses", "connection-host", 112 );
		let [program2, instance2, connection2] = App::AddConnection( "Tests.Connections", "reuses", "connection-host", 112 );
		EXPECT_EQ( program1, program2 );
		EXPECT_EQ( instance1, instance2 );
		EXPECT_NE( connection1, connection2 );
		EXPECT_EQ( LiveCount(connection1), 0u ) << "a reconnect supersedes the instance's previous connection";
		EXPECT_EQ( LiveCount(connection2), 1u );

		let [program3, instance3, connection3] = App::AddConnection( "Tests.Connections", "reuses-other", "connection-host", 113 );
		EXPECT_EQ( program1, program3 );
		EXPECT_NE( instance1, instance3 );
		EXPECT_NE( connection2, connection3 );
	}

	//EndConnection ends one *connection* - another instance's live connection must not be stamped.
	TEST_F( ConnectionTests, EndConnectionStampsDeleted ){
		let [program, instance, connection] = App::AddConnection( "Tests.Connections", "ends", "connection-host", 114 );
		let [program2, instance2, survivor] = App::AddConnection( "Tests.Connections", "ends-other", "connection-host", 115 );
		ASSERT_EQ( LiveCount(connection), 1u );
		App::EndConnection( connection );//fire-and-forget task - poll for the stamp.
		WaitUntil( [=]{ return !LiveCount(connection); } );
		EXPECT_EQ( LiveCount(connection), 0u );
		EXPECT_EQ( LiveCount(survivor), 1u ) << "only the ended connection may be stamped";
	}
}