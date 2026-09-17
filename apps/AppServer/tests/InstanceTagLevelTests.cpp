#include <jde/db/db.h>
#include <jde/db/IDataSource.h>
#include <jde/db/Row.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h>
#include <jde/ql/QLAwait.h>
#include "helpers.h"
#include "../src/LocalClient.h"
#include "../src/appStartup.h"
#include "../src/ql/AppServerQL.h"
#include <jde/fwk/log/SpdLog.h>	//no longer reachable through <jde/fwk.h>
#define let const auto

//the per-instance log-level overrides: updateInstanceTagLevel mutations and the instanceTagLevels query, driven
//through the same AppServerQL custom hooks the web ui uses.
namespace Jde::App::Server::Tests{

	struct InstanceTagLevelTests : ::testing::Test{
		Ω RunQL( string query, jobject vars={} )->jvalue{
			return BlockAwait<QL::QLAwait<jvalue>,jvalue>( QL::QLAwait<jvalue>{move(query), move(vars), Jde::UserPK{Jde::UserPK::System}, Server::QLPtr()} );
		}
		Ω TagRows( ProgInstPK instanceId )->vector<DB::Row>{
			let table = Server::AppSchema()->GetView( "instance_tag_levels" ).DBName;
			return Server::AppSchema()->DS()->Select( {Ƒ("select type, tag, level_id from {} where instance_id=?", table), {DB::Value{instanceId}}} );
		}
		Ω MintInstance( str name )->ProgInstPK{
			let [program, instance, connection] = App::AddConnection( "Tests.TagLevels", name, "taglevel-host", 115 );
			return instance;
		}
		Ω TextLogger()->Logging::SpdLog&{ auto p = Logging::FindLogger<Logging::SpdLog>(); return *p; }
		Ω FindConfigured( ELogTags tags )->optional<ELogLevel>{
			optional<ELogLevel> y;
			TextLogger().ConfiguredTags().cvisit( tags, [&](let& kv){ y = kv.second; } );
			return y;
		}
	};

	//the mutation is not just a write: the instance it names has to end up logging at the new levels.  For our own
	//instance there is no socket, so the push goes back through the local ql - same updateLogSetting either way.
	TEST_F( InstanceTagLevelTests, PushesLevelsToTheInstance ){
		let instanceId = Server::AppClient()->InstancePK();
		ASSERT_TRUE( instanceId ) << "AppStartup registers this process; without its pk there is nothing to push to";
		let restore = FindConfigured( ELogTags::Threads );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["threads"],level:"Critical"}}] ))", instanceId) );
		EXPECT_EQ( FindConfigured(ELogTags::Threads), ELogLevel::Critical ) << "the push should have applied the level in this process";

		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["threads"],level:null}}] ))", instanceId) );
		//install-issues #21: `threads` is Information in App.Server.Tests.jsonnet, and that - not nothing, not Critical - is what
		//deleting the row has to leave the tag at.
		EXPECT_EQ( FindConfigured(ELogTags::Threads), ELogLevel::Information ) << "deleting the row should put the tag back to its configured level, not leave the override in place";
		if( restore )
			TextLogger().SetLevel( ELogTags::Threads, *restore );
	}

	//`break` is a pseudo-tag - it sets this process's debugger-trap level.  It must reach neither the table (where tag 0
	//means `default`) nor the push (where ToLogTags folds it into None, which is also `default`): either would silently
	//rewrite the default level instead of the trap level.
#ifndef NDEBUG
	TEST_F( InstanceTagLevelTests, BreakSetsTheTrapLevelWithoutPersisting ){
		let instanceId = MintInstance( "break" );
		let restore = Logging::BreakLevel();
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["break"],level:"Critical"}},{{tags:["sql"],level:"Debug"}}] ))", instanceId) );
		EXPECT_EQ( Logging::BreakLevel(), ELogLevel::Critical );
		let rows = TagRows( instanceId );
		ASSERT_EQ( rows.size(), 1u ) << "break is not a tag level - only sql should have been written";
		EXPECT_EQ( rows[0].Get<uint>(1), underlying(ELogTags::Sql) );
		Logging::SetBreakLevel( restore );
	}
#endif

	//an instance with no session is the normal case (it is offline, or it is a browser): the rows are still written.
	TEST_F( InstanceTagLevelTests, PushToDisconnectedInstanceStillWrites ){
		let instanceId = MintInstance( "offline" );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["default"],level:"Warning"}}] ))", instanceId) );
		EXPECT_EQ( TagRows(instanceId).size(), 1u );
	}

	TEST_F( InstanceTagLevelTests, UpsertAndQuery ){
		let instanceId = MintInstance( "upsert" );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["default"],level:"Warning"}},{{tags:["sql"],level:"Debug"}}], "binary":[{{tags:["default"],level:"Debug"}}] ))", instanceId) );
		let rows = TagRows( instanceId );
		ASSERT_EQ( rows.size(), 3u );
		auto foundSql = false;
		for( auto&& row : rows ){
			if( row.GetString(0)=="text" && row.Get<uint>(1)==underlying(ELogTags::Sql) ){
				foundSql = true;
				EXPECT_EQ( row.GetOpt<uint8>(2).value_or(0), (uint8)underlying(ELogLevel::Debug) );
			}
		}
		EXPECT_TRUE( foundSql );

		auto y = RunQL( Ƒ("instanceTagLevels( id: {} ){{ text binary }}", instanceId) );
		let& o = y.as_object();
		ASSERT_TRUE( o.contains("text") );
		ASSERT_TRUE( o.contains("binary") );
		//keyed by level, tags as values: { "Warning":["default"], "Debug":["sql"] }
		let& text = o.at( "text" ).as_object();
		ASSERT_EQ( text.size(), 2u );
		let& warning = text.at( "Warning" ).as_array();
		ASSERT_EQ( warning.size(), 1u );
		EXPECT_EQ( warning[0].as_string(), "default" );
		let& debug = text.at( "Debug" ).as_array();
		ASSERT_EQ( debug.size(), 1u );
		EXPECT_EQ( debug[0].as_string(), "sql" ) << "a tag value is the bare name; only a tag *key* had to be spelled [\"sql\"]";
		EXPECT_EQ( ToLogTags(debug[0]), ELogTags::Sql ) << "and it has to parse back";
		let& binary = o.at( "binary" ).as_object();
		EXPECT_EQ( binary.at("Debug").as_array()[0].as_string(), "default" );
	}

	//the shape's reason for existing, on both legs: a combined tag has no name of its own, so it goes out as an array
	//value - legal json where the equivalent key was not - and comes back the same way, straight into ToLogTags( jvalue ).
	TEST_F( InstanceTagLevelTests, MultiTagOverrideRoundTrips ){
		let instanceId = MintInstance( "multiTag" );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["socket","client","read"],level:"Error"}}] ))", instanceId) );
		auto y = RunQL( Ƒ("instanceTagLevels( id: {} ){{ text }}", instanceId) );
		let& text = y.as_object().at( "text" ).as_object();
		ASSERT_EQ( text.size(), 1u );
		let& error = text.at( "Error" ).as_array();
		ASSERT_EQ( error.size(), 1u );
		EXPECT_TRUE( error[0].is_array() ) << "socket|client|read has no single name - only the array spells it";
		EXPECT_EQ( ToLogTags(error[0]), ELogTags::SocketClientRead );
	}

	//only the requested groups come back.
	TEST_F( InstanceTagLevelTests, QueryFiltersColumns ){
		let instanceId = MintInstance( "filters" );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["default"],level:"Information"}}], "binary":[{{tags:["default"],level:"Debug"}}] ))", instanceId) );
		auto y = RunQL( Ƒ("instanceTagLevels( id: {} ){{ text }}", instanceId) );
		let& o = y.as_object();
		EXPECT_TRUE( o.contains("text") );
		EXPECT_FALSE( o.contains("binary") );
		EXPECT_FALSE( o.contains("appServer") );
	}

	//a null level deletes the override instead of upserting it - as does a record with no level at all.
	TEST_F( InstanceTagLevelTests, NullLevelDeletes ){
		let instanceId = MintInstance( "deletes" );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["default"],level:"Warning"}},{{tags:["sql"],level:"Debug"}}] ))", instanceId) );
		ASSERT_EQ( TagRows(instanceId).size(), 2u );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["sql"],level:null}}] ))", instanceId) );
		let rows = TagRows( instanceId );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( rows[0].Get<uint>(1), 0u ) << "only the default override should remain";

		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["default"]}}] ))", instanceId) );
		EXPECT_EQ( TagRows(instanceId).size(), 0u ) << "an omitted level is the same 'remove this override' a null one is";
	}

	//the argument takes either spelling of a combined tag: the parts, or the joined name the ui and the config use.
	TEST_F( InstanceTagLevelTests, TagsAcceptTheJoinedSpelling ){
		let instanceId = MintInstance( "spelling" );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["socket.client.read"],level:"Error"}}] ))", instanceId) );
		let rows = TagRows( instanceId );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( rows[0].Get<uint>(1), underlying(ELogTags::SocketClientRead) );
	}

	//install-issues #19: the rows are only what the page saved; `running` is what the instance logs at - its own logSetting
	//answer, tag->level with `default`, for the sinks asked for.  Our own instance answers through the local ql, and an
	//override just pushed shows up in it at the level pushed.
	TEST_F( InstanceTagLevelTests, RunningLevelsComeFromTheInstance ){
		let instanceId = Server::AppClient()->InstancePK();
		ASSERT_TRUE( instanceId ) << "AppStartup registers this process; without its pk there is nothing to ask";
		let restore = FindConfigured( ELogTags::Threads );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["threads"],level:"Critical"}}] ))", instanceId) );
		auto y = RunQL( Ƒ("instanceTagLevels( id: {} ){{ text running }}", instanceId) );
		let& o = y.as_object();
		ASSERT_TRUE( o.contains("running") );
		let& running = o.at( "running" ).as_object();
		ASSERT_TRUE( running.contains("text") );
		EXPECT_FALSE( running.contains("binary") ) << "only the sinks the query names are asked of the instance";
		let& text = running.at( "text" ).as_object();
		EXPECT_EQ( text.at("default").as_string(), ToString(TextLogger().DefaultLevel()) ) << "the running default, not the Information the ui used to assume";
		EXPECT_EQ( text.at("threads").as_string(), "Critical" ) << "the pushed override, as the instance now runs it";
		EXPECT_EQ( o.at("text").as_object().at("Critical").as_array()[0].as_string(), "threads" ) << "and the stored row beside it";

		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["threads"],level:null}}] ))", instanceId) );
		if( restore )
			TextLogger().SetLevel( ELogTags::Threads, *restore );
	}

	//an instance with no session cannot say what it runs with: null, and the rows still come back.
	TEST_F( InstanceTagLevelTests, RunningIsNullForADisconnectedInstance ){
		let instanceId = MintInstance( "offlineRunning" );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "text":[{{tags:["sql"],level:"Debug"}}] ))", instanceId) );
		auto y = RunQL( Ƒ("instanceTagLevels( id: {} ){{ text running }}", instanceId) );
		let& o = y.as_object();
		ASSERT_TRUE( o.contains("running") );
		EXPECT_TRUE( o.at("running").is_null() );
		EXPECT_EQ( o.at("text").as_object().at("Debug").as_array()[0].as_string(), "sql" );
	}

	//the appServer group rides the same table with its own type discriminator.
	TEST_F( InstanceTagLevelTests, AppServerGroup ){
		let instanceId = MintInstance( "appServer" );
		RunQL( Ƒ(R"(mutation updateInstanceTagLevel( "id":{}, "appServer":[{{tags:["default"],level:"Trace"}}] ))", instanceId) );
		let rows = TagRows( instanceId );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( rows[0].GetString(0), "appServer" );

		auto y = RunQL( Ƒ("instanceTagLevels( id: {} ){{ appServer }}", instanceId) );
		let& o = y.as_object();
		ASSERT_TRUE( o.contains("appServer") );
		EXPECT_EQ( o.at("appServer").as_object().at("Trace").as_array()[0].as_string(), "default" );
	}
}
