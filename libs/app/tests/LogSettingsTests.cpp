//Ported from apps/OpcGateway/tests/LogSettingTests.cpp - the same "read the log settings, flip a default, read it back"
//round trip, driven against the awaitables in Jde.App.Shared instead of a running gateway.  LogSettingsAwait resolves in
//await_ready and LogSettingsMAwait's Resume/ResumeExp are synchronous, so BlockAwait runs both on this thread.
#include <gtest/gtest.h>
#include <jde/fwk/log/SpdLog.h>
#include <jde/ql/types/RequestQL.h>
#include <jde/app/IApp.h>
#include <jde/app/log/LogSettingsAwait.h>
#include <jde/app/log/ProtoLog.h>
#include "helpers.h"

#define let const auto

namespace Jde::App::Tests{
	//Resolves in await_ready, so the mutation's forward to the app server never leaves this thread.
	struct ValueAwait final : TAwait<jvalue>{
		ValueAwait( jvalue value, SRCE )ι:TAwait<jvalue>{sl}, _value{move(value)}{}
		α await_ready()ι->bool override{ return true; }
		α Suspend()ι->void override{ ASSERT(false); }
		α await_resume()ε->jvalue override{ return move(_value); }
	private:
		jvalue _value;
	};

	//Only the query hand-off is reachable from here;  every other IApp member is a hard error if the mutation ever calls it.
	struct AppStub final : IApp{
		α PublicKey()Ι->const Crypto::PublicKey& override{ static const Crypto::PublicKey y; return y; }
		α SessionInfoAwait( SessionPK, SL )ε->up<TAwait<Web::FromServer::SessionInfo>> override{ throw Exception{"AppStub::SessionInfoAwait"}; }
		α Login( Web::Jwt&&, SL )ε->Web::Client::ClientSocketAwait<Web::FromServer::SessionInfo> override{ throw Exception{"AppStub::Login"}; }
		α ClientQuery( QL::RequestQL&&, UserPK, SL )ε->up<TAwait<jvalue>> override{ throw Exception{"AppStub::ClientQuery"}; }

		string Query;      //what the mutation forwarded to the app server.
		jobject Variables;
		uint Queries{};
	private:
		α QueryArray( string&&, jobject, bool, SL )ε->up<TAwait<jarray>> override{ throw Exception{"AppStub::QueryArray"}; }
		α QueryObject( string&&, jobject, bool, SL )ε->up<TAwait<jobject>> override{ throw Exception{"AppStub::QueryObject"}; }
		α QueryValue( string&& q, jobject variables, bool, SL )ε->up<TAwait<jvalue>> override{
			Query = move( q );
			Variables = move( variables );
			++Queries;
			return mu<ValueAwait>( jvalue{true} );
		}
	};

	//logSetting{ text binary tags }
	Ω logSettings( std::initializer_list<sv> columns={"text","binary","tags"} )ε->jobject{
		auto ql = table( "logSetting" );
		addColumns( ql, columns );
		let y = BlockAwait<LogSettingsAwait,jvalue>( LogSettingsAwait{move(ql)} );
		return y.as_object();
	}
	Ω defaultLevel( sv logger )ε->ELogLevel{ return ToLogLevel( logSettings().at(logger).at("default").as_string() ); }

	//updateLogSetting( text:{default: $default} )
	Ω updateLogSettings( sv args, jobject variables, sp<AppStub> app )ε->jvalue{
		static const vector<sp<DB::AppSchema>> noSchemas;
		QL::MutationQL m{ "updateLogSetting", QL::Parser::ParseArgs(string{args}), ms<jobject>(move(variables)), optional<QL::TableQL>{}, true, noSchemas, true };
		return BlockAwait<LogSettingsMAwait,jvalue>( LogSettingsMAwait{move(m), app, UserPK{}} );
	}

	//T8: the per-tag overrides, which are as process-wide as the default level and were not being put back - `ql` at Critical
	//outlived UpdateSetsATagLevel and every later suite ran under it.  Snapshotted rather than remembered by name: any test
	//here can set any tag, and one that is added later would otherwise leak silently.
	Ω tagLevels( const LogTags& logger )ι->flat_map<ELogTags,ELogLevel>{
		flat_map<ELogTags,ELogLevel> y;
		logger.ConfiguredTags().cvisit_all( [&](let& kv){ y.emplace(kv.first, kv.second); } );
		return y;
	}

	struct LogSettingsTests : ::testing::Test{
	protected:
		α SetUp()->void override{
			_text = Logging::FindLogger<Logging::SpdLog>();
			_binary = Logging::FindLogger<App::ProtoLog>();
			ASSERT_TRUE( _text );
			ASSERT_TRUE( _binary ); //config/App.Tests.jsonnet configures /logging/proto - without it the binary half is vacuous.
			_textDefault = _text->DefaultLevel();
			_binaryDefault = _binary->DefaultLevel();
			_textTags = tagLevels( *_text );
			_binaryTags = tagLevels( *_binary );
		}
		α TearDown()->void override{ //process-wide loggers - put back what the other suites' log output depends on.
			_text->SetDefaultLevel( _textDefault );
			_binary->SetDefaultLevel( _binaryDefault );
			restore( *_text, _textTags );
			restore( *_binary, _binaryTags );
			Logging::UpdateCumulative( Logging::Loggers() );//half of T8: SetDefaultLevel restores the logger, but the cumulative filter is computed across all of them and nothing else recomputes it, so later suites ran under whatever the last mutation left.
		}
		//Both directions: a tag the test added has to go, and one it overwrote has to go back to what it was.
		Ω restore( LogTags& logger, const flat_map<ELogTags,ELogLevel>& original )ι->void{
			for( let& [tag, _] : tagLevels(logger) ){
				if( !original.contains(tag) )
					logger.ClearLevel( tag );
			}
			for( let& [tag, level] : original )
				logger.SetLevel( tag, level );
		}
		Logging::SpdLog* _text{};
		App::ProtoLog* _binary{};
		ELogLevel _textDefault{};
		ELogLevel _binaryDefault{};
		flat_map<ELogTags,ELogLevel> _textTags, _binaryTags;
	};

	TEST_F( LogSettingsTests, QueryReturnsEachRequestedLogger ){
		let y = logSettings();
		ASSERT_TRUE( y.contains("text") );
		ASSERT_TRUE( y.contains("binary") );
		ASSERT_TRUE( y.contains("tags") );
		EXPECT_EQ( ToLogLevel(y.at("text").as_object().at("default").as_string()), _text->DefaultLevel() );
		EXPECT_EQ( ToLogLevel(y.at("binary").as_object().at("default").as_string()), _binary->DefaultLevel() );
	}

	//Column-driven, like every other QL result:  what was not asked for is not in the object.
	TEST_F( LogSettingsTests, QueryOmitsUnrequestedColumns ){
		let y = logSettings( {"text"} );
		EXPECT_TRUE( y.contains("text") );
		EXPECT_FALSE( y.contains("binary") );
		EXPECT_FALSE( y.contains("tags") );
		EXPECT_TRUE( logSettings({}).empty() );
	}

	//Each logger reports the per-tag levels it was configured with, spelled the way the settings spell them.
	TEST_F( LogSettingsTests, QueryReportsConfiguredTagLevels ){
		let text = logSettings( {"text"} ).at( "text" ).as_object();
		EXPECT_EQ( text.at("app").as_string(), "Trace" );        //logging.spd.tags in App.Tests.jsonnet…
		EXPECT_EQ( text.at("settings").as_string(), "Debug" );
		let binary = logSettings( {"binary"} ).at( "binary" ).as_object();
		EXPECT_EQ( binary.at("externalLogger").as_string(), "None" ); //…and logging.proto.tags.
	}

	//`tags` is the name->bit map the settings UI builds its tag list from, not a per-logger level.  The query asks for
	//the *user* catalog - Tags(true) - which adds the composite tags (http.server.read = Http|Server|Read &c) on top of
	//the single-bit ones, so assert against that overload, not the bare Tags().
	TEST_F( LogSettingsTests, QueryReturnsTheTagCatalog ){
		let tags = logSettings( {"tags"} ).at( "tags" ).as_object();
		EXPECT_EQ( tags.size(), Logging::Tags(true).size() );
		EXPECT_GT( Logging::Tags(true).size(), Logging::Tags().size() ) << "the user catalog is a superset of the single-bit tags";
		EXPECT_EQ( tags.at("app").to_number<uint>(), (uint)ELogTags::App );
		EXPECT_EQ( tags.at("test").to_number<uint>(), (uint)ELogTags::Test );
		//a catalogue name the ui cannot save is worse than no name: it must be the spelling ToLogTags reads and the one
		//the logSetting/instanceTagLevel answers carry, which is ToString's - bit order, not the config's reading order.
		let composite = Jde::ToString( ELogTags::HttpServerRead, false );
		EXPECT_EQ( tags.at(composite).to_number<uint>(), (uint)ELogTags::HttpServerRead ) << "composite tags have to reach the UI";
		EXPECT_EQ( ToLogTags(sv{composite}), ELogTags::HttpServerRead ) << "and have to come back";
		for( let& [name, id] : tags )
			EXPECT_EQ( underlying(ToLogTags(sv{name})), id.to_number<uint>() ) << "every catalogue name round-trips: " << name;
	}

	//The ported round trip:  flip the default, read it back through the query, for each logger in turn - and then the gate, not just
	//the stored field (T1).  MinLevel memoizes per tag into ExtrapolatedTags and consults _defaultLevel only on a miss, so reading
	//the value back says nothing about whether any entry is admitted differently:  that is exactly how #6 stayed green while the Log
	//Settings page's default control was inert for every tag already logged.  Sql is configured on neither logger
	//([`App.Tests.jsonnet`] sets app/exception/test/settings/externalLogger), so it always falls through to the default.
	TEST_F( LogSettingsTests, UpdateDefault ){
		auto app = ms<AppStub>();
		app->SetAppPKs( 42, 1 );
		let setDefault = [&]( sv logger, ELogLevel level ){
			updateLogSettings( Ƒ("{{{}: {{default: $default}}}}", logger), jobject{{"default", ToString(level)}}, app );
		};
		let testUpdate = [&]( sv name, LogTags& logger, ELogLevel current ){
			let updated = current==ELogLevel::Information ? ELogLevel::Warning : ELogLevel::Information;
			setDefault( name, updated );
			EXPECT_EQ( defaultLevel(name), updated ) << name;
			//Fill the memo at a level the tag passes, then drop the default below it.  Pre-fix the second ShouldLog answered from
			//the stale entry and stayed true - any unrelated per-tag edit in the same session masked it, which is why nobody saw it.
			setDefault( name, ELogLevel::Information );
			ASSERT_TRUE( logger.ShouldLog(ELogLevel::Information, ELogTags::Sql) ) << name;
			setDefault( name, ELogLevel::Critical );
			EXPECT_FALSE( logger.ShouldLog(ELogLevel::Information, ELogTags::Sql) ) << name << ": the default level is stored but the gate still answers from the per-tag memo";
		};
		testUpdate( "text", *_text, _text->DefaultLevel() );
		testUpdate( "binary", *_binary, _binary->DefaultLevel() );
	}

	TEST_F( LogSettingsTests, UpdateSetsATagLevel ){
		auto app = ms<AppStub>();
		app->SetAppPKs( 42, 1 );
		updateLogSettings( R"({text: {ql: "Critical"}})", {}, app );
		EXPECT_EQ( logSettings({"text"}).at("text").as_object().at("ql").as_string(), "Critical" );
		EXPECT_EQ( _text->DefaultLevel(), _textDefault ); //a tag level is not the default.
	}

	//install-issues #21: a null level deletes the override, and the tag has to go back to what the settings configured -
	//`settings` is Debug in App.Tests.jsonnet - not fall to the default (or, for the default itself, stay where the override
	//left it) until a restart.
	TEST_F( LogSettingsTests, UpdateNullRestoresTheConfiguredLevel ){
		auto app = ms<AppStub>();
		app->SetAppPKs( 42, 1 );
		updateLogSettings( R"({text: {settings: "Critical", default: "Critical"}})", {}, app );
		ASSERT_EQ( logSettings({"text"}).at("text").as_object().at("settings").as_string(), "Critical" );
		ASSERT_EQ( defaultLevel("text"), ELogLevel::Critical );
		ASSERT_FALSE( _text->ShouldLog(ELogLevel::Debug, ELogTags::Settings) );

		updateLogSettings( R"({text: {settings: null, default: null}})", {}, app );
		EXPECT_EQ( logSettings({"text"}).at("text").as_object().at("settings").as_string(), "Debug" ) << "the configured level, not the default and not the cleared override";
		EXPECT_EQ( defaultLevel("text"), _textDefault ) << "the settings' default";
		EXPECT_TRUE( _text->ShouldLog(ELogLevel::Debug, ELogTags::Settings) ) << "and the gate answers from it";
	}

	//The runtime update and the app-server hand-off are two halves of one mutation:  the levels change here, and the same
	//args are forwarded as the instance's stored tag levels so they survive a restart.
	TEST_F( LogSettingsTests, UpdateForwardsToTheAppServer ){
		auto app = ms<AppStub>();
		app->SetAppPKs( 42, 1 );
		updateLogSettings( R"({text: {default: "Warning"}})", {}, app );
		ASSERT_EQ( app->Queries, 1u );
		EXPECT_NE( app->Query.find("updateInstanceTagLevel"), string::npos ); //re-addressed from updateLogSetting…
		EXPECT_NE( app->Query.find("\"id\":42"), string::npos );              //…at this instance's pk.
		EXPECT_NE( app->Query.find("Warning"), string::npos );
	}

	//No instance pk means nothing to store against, so the mutation fails - but only after the in-process levels are set,
	//which is what a gateway that has not finished connecting still needs.
	TEST_F( LogSettingsTests, UpdateWithoutAnInstanceStillSetsTheRuntimeLevels ){
		auto app = ms<AppStub>(); //no SetAppPKs - InstancePK() is 0.
		EXPECT_THROW( updateLogSettings(R"({text: {default: "Critical"}})", {}, app), Exception );
		EXPECT_EQ( app->Queries, 0u );
		EXPECT_EQ( defaultLevel("text"), ELogLevel::Critical );
	}

	//L9: an unknown name was not rejected, it was dropped - ToLogTags warns and returns what it recognised, ELogTags::None here.
	//SetLevel wrote a None row that MinLevel can never match, ToJson reported it back as a "none" tag, and the typo was forwarded
	//to updateInstanceTagLevel to be persisted as tag 0 - the row "default" writes.  A typo is a request the caller has to hear about.
	TEST_F( LogSettingsTests, UpdateRejectsAnUnknownTag ){
		auto app = ms<AppStub>();
		app->SetAppPKs( 42, 1 );
		EXPECT_THROW( updateLogSettings(R"({text: {sqll: "Critical"}})", {}, app), Exception );
		EXPECT_EQ( app->Queries, 0u ) << "the unknown tag was forwarded to the app server to be persisted";
		EXPECT_FALSE( logSettings({"text"}).at("text").as_object().contains("none") ) << "an unmatchable None override was written, and is reported back as a tag";
	}

	//A combined key has to be checked a part at a time:  ToLogTags splits on TagSeparator and ORs what it knows, so an unknown
	//part is dropped and the rest still applies - "socket.bogus" set a plain socket override, wider than what was asked for.
	TEST_F( LogSettingsTests, UpdateRejectsAnUnknownTagInACombinedKey ){
		auto app = ms<AppStub>();
		app->SetAppPKs( 42, 1 );
		EXPECT_THROW( updateLogSettings(R"({text: {"socket.bogus": "Critical"}})", {}, app), Exception );
		EXPECT_FALSE( logSettings({"text"}).at("text").as_object().contains("socket") ) << "the recognised half of the key was applied on its own";
		//…and the composite spellings the catalogue does offer still go through - by ToString, which is the spelling it offers.
		let composite = Jde::ToString( ELogTags::SocketClientRead, false );
		updateLogSettings( Ƒ(R"({{text: {{"{}": "Critical"}}}})", composite), {}, app );
		EXPECT_EQ( logSettings({"text"}).at("text").as_object().at(composite).as_string(), "Critical" );
	}

	//The mutation is one unit:  a bad key anywhere in it must not leave an earlier group applied.
	TEST_F( LogSettingsTests, UpdateAppliesNothingWhenAnyGroupHasAnUnknownTag ){
		auto app = ms<AppStub>();
		app->SetAppPKs( 42, 1 );
		EXPECT_THROW( updateLogSettings(R"({text: {ql: "Trace"}, binary: {sqll: "Critical"}})", {}, app), Exception );
		//if_contains, not at: with T8's restore in place `ql` is configured on neither logger, and reading it as a key that
		//has to exist was this test borrowing the override UpdateSetsATagLevel used to leak into it.
		let text = logSettings( {"text"} ).at( "text" ).as_object();//held: if_contains returns a pointer into it, and the query result is a temporary.
		let ql = text.if_contains( "ql" );
		EXPECT_TRUE( !ql || ql->as_string()!="Trace" ) << "the good group was applied before the bad one was read";
	}

	TEST_F( LogSettingsTests, IsApplicable ){
		static const vector<sp<DB::AppSchema>> noSchemas;
		let mutation = []( sv command )ε{ return QL::MutationQL{ string{command}, jobject{}, ms<jobject>(), optional<QL::TableQL>{}, true, noSchemas, true }; };
		EXPECT_TRUE( LogSettingsMAwait::IsApplicable(mutation("updateLogSetting")) );
		EXPECT_TRUE( LogSettingsMAwait::IsApplicable(mutation("updateLogSettings")) );
		EXPECT_FALSE( LogSettingsMAwait::IsApplicable(mutation("updateLogLevel")) );
		EXPECT_FALSE( LogSettingsMAwait::IsApplicable(mutation("updateUser")) );
	}

	//T8: declared last, so what it reads is whatever the tests above it left on the process-wide logger.  Overrides are as
	//shared as the default level was, and were not being put back: `ql` at Critical outlived UpdateSetsATagLevel and every
	//suite after this one ran under it - and `Critical` on a tag is close enough to silence to hide a diagnostic.
	TEST_F( LogSettingsTests, TearDownRestoresTagLevels ){
		let text = logSettings( {"text"} ).at( "text" ).as_object();
		EXPECT_FALSE( text.contains("ql") ) << "UpdateSetsATagLevel's override outlived its test";
		EXPECT_FALSE( text.contains(Jde::ToString(ELogTags::SocketClientRead, false)) ) << "UpdateRejectsAnUnknownTagInACombinedKey's override outlived its test";
		EXPECT_EQ( text.at("app").as_string(), "Trace" ) << "…and what the settings did configure is still configured";
		EXPECT_EQ( text.at("settings").as_string(), "Debug" );
	}
}
