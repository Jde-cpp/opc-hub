#include <absl/cleanup/cleanup.h>
#include <jde/db/db.h>
#include <jde/db/IDataSource.h>
#include <jde/db/Row.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/tests/HttpTest.h>
#include "helpers.h"
#include "../src/appStartup.h"
#include "../src/WebServer.h"
#include <thread>
#define let const auto

//the connections bookkeeping in appStartup.cpp: AddConnection's proc-minted pks and EndConnection's deleted stamp.
namespace Jde::App::Server::Tests{
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
				EXPECT_TRUE( DB::EnumCache::Instance().Get(table->Name) ) << table->Name << " was not loaded - its next render blocks the caller";
			}
		}
		EXPECT_GT( checked, 0u ) << "no lookup tables found - the walk no longer matches what SelectAwait renders from";
	}

	//`hosts` has the enum shape but grows, and loading it with no expiry means nothing else would ever refresh it.
	TEST_F( ConnectionTests, ANewHostReachesTheLoadedMap ){
		let host = Ƒ( "load-host-{}", steady_clock::now().time_since_epoch().count() );
		App::AddConnection( "Tests.Connections", "loads-host", host, 116 );
		auto hosts = DB::EnumCache::Instance().Get( "hosts" );
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

	//`connections` declares no right, so any known user read all of it, and with a guest that is every visitor:  the demo's walk
	//listed the hub and its PLCs with their hosts (docs/demo/plan.md Phase 8).  What a route finds its instance by stays any known
	//user's;  the host, the pid, the start, a run's end and the live status, a filter or an order by anything else, and the hosts'
	//rows, queried or introspected as an enum, take Read on app/instances once that is enforced (Server::CustomQuery), as do a
	//top-level status's memory and start (AppQL::StatusQuery) and /opcGateways' pid and start (HttpRequestAwait.cpp).
	TEST_F( ConnectionTests, TheRegistryBeyondALookupIsTheInstances ){
		App::AddConnection( "Tests.Connections", "gated", "gated-host", 121 );
		let schema = Server::AppSchema()->Name;
		let nobody = User( "registryNobody" ), reader = User( "registryReader" );
		let role = FindOrCreate( "role", "registryReader" );
		RunQL( "addRole( id:$role, permissionRight:{ allowed:2, denied:0, resource:{ schemaName:$schema, slug:\"instances\" } } )", {{"role", role}, {"schema", schema}} );
		let acl = RunQL( "acl( identityId:$user ){ role{ id } }", {{"user", reader.Value}} );
		if( !acl.is_array() || std::ranges::none_of(acl.get_array(), [role](let& row){ return row.is_object() && Json::FindNumber<uint>(Json::FindDefaultObject(row.get_object(), "role"), "id")==role; }) )
			RunQL( "createAcl( identity:{ id:$user }, role:{ id:$role } )", {{"user", reader.Value}, {"role", role}} );

		const vector<string> lookups{ "connections( deleted:null ){ id instanceId instanceName programName }", "status{ timeZone }" };
		const vector<string> own{
			"connections( deleted:null ){ id hostName }",
			"connections{ id created }",
			"connections{ id deleted }",
			"connections( deleted:{ne:null} ){ id }",
			"connections{ id pid }",
			"connections{ id status{ memory } }",
			"status{ memory startTime uptimeSeconds }",
			"connections( hostName:\"gated-host\" ){ id }",
			"hosts{ id name }",
			"__type( name:\"Host\" ){ enumValues{ id name } }"
		};
		for( let& q : own )
			EXPECT_EQ( Status([&]{ RunQL(q, {}, nobody); }), EHttpStatus::None ) << "unenforced, as installed:  " << q;

		let resource = Json::AsArray( RunQL(Ƒ("resources( schemaName:[\"{}\"], slug:\"instances\", criteria:null ){{ id deleted }}", schema)) );
		ASSERT_EQ( resource.size(), 1u );
		ASSERT_FALSE( Json::AsObject(resource[0]).at("deleted").is_null() ) << "the suite leaves app/instances unenforced";
		let id = Id( resource[0] );
		RunQL( Ƒ("restoreResource( id:{} )", id) );
		absl::Cleanup unenforce{ [&]{ RunQL( Ƒ("deleteResource( id:{} )", id) ); } };

		for( let& q : lookups )
			EXPECT_EQ( Status([&]{ RunQL(q, {}, nobody); }), EHttpStatus::None ) << "what a route finds its instance by, and a calendar's zone:  " << q;
		for( let& q : own ){
			EXPECT_EQ( Status([&]{ RunQL(q, {}, nobody); }), EHttpStatus::Forbidden ) << q;
			EXPECT_EQ( Status([&]{ RunQL(q, {}, reader); }), EHttpStatus::None ) << q;
			EXPECT_EQ( Status([&]{ RunQL(q); }), EHttpStatus::None ) << "the system:  " << q;
		}
		EXPECT_EQ( Status([&]{ RunQL("connections( orderBy:\"hostName\" ){ id }", {}, nobody); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Status([&]{ RunQL(own.front(), {}, {}); }), EHttpStatus::Unauthorized ) << "enforced:  who is asking";
		EXPECT_EQ( Status([&]{ RunQL("__type( name:\"Host\" ){ fields{ name } }", {}, nobody); }), EHttpStatus::None ) << "the type's shape is no row";
		let hosts = Json::AsArray( RunQL(own.front(), {}, reader) );
		EXPECT_TRUE( std::ranges::any_of(hosts, [](let& row){ return Json::FindDefaultSV(Json::AsObject(row), "hostName")=="gated-host"; }) ) << serialize( hosts );

		auto client = Connect();//so /opcGateways, which answers a caller with a user the pid and the start (types/rest/json.h)
		absl::Cleanup close{ [&]{ BlockAwait( client->Close(true, SRCE_CUR) ); } };
		RegisterInstance( *client, "Jde.OpcGateway", "gated-gw", "gw-host", 4324, 4444 );
		let gateway = [&]( Jde::UserPK user )->jobject{
			for( let& jserver : Json::AsArray(HttpTest::Get(Port(), "/opcGateways", Ƒ("{:x}", MintSession(user))).Json(), "servers") ){
				if( let& o = jserver.as_object(); Json::AsString(o, "instanceName")=="gated-gw" )
					return o;
			}
			return {};
		};
		let refused = gateway( nobody );
		ASSERT_FALSE( refused.empty() ) << "discovery stays every caller's";
		EXPECT_FALSE( refused.contains("pid") || refused.contains("startTime") ) << serialize( refused );
		EXPECT_EQ( gateway(reader).at("pid").to_number<uint32>(), 4444u );
	}
}