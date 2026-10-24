//TablesAwait hands the table to the status/log handlers by rvalue, so the routing decision has to be made before the call and
//the moving branches have to be terminal - review #5 (use-after-move).  NullQL (NullQL.h) is the pathological handler the old code
//trusted not to exist: it consumes the table and *still* returns null.  No data source is involved: the tables are `system`,
//so TableQL leaves _dbTable null, and Resume/ResumeExp are synchronous, so BlockAwait drives the coroutine on this thread.
#include <gtest/gtest.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/ops/TablesAwait.h>
#include "NullQL.h"

#define let const auto

namespace Jde::QL::Tests{
	Ω tables( sv jsonName )ε->vector<TableQL>{
		const vector<sp<DB::AppSchema>> noSchemas;
		vector<TableQL> y;
		y.emplace_back( string{jsonName}, jobject{}, ms<jobject>(), noSchemas, true );
		return y;
	}
	Ω execute( sv jsonName, sp<NullQL> ql )ε->jvalue{
		return BlockAwait<TablesAwait,jvalue>( TablesAwait{tables(jsonName), {}, Creds{UserPK{UserPK::System}}, sp<IQL>{ql}, SRCE_CUR} );
	}

	//A handler that consumed the table and returned null used to fall through to the next branch/the generic select with a
	//gutted TableQL; now it is an error naming the table it was asked for.
	TEST( TablesAwaitTests, LogQueryReturningNullThrowsNamingTheTable ){
		auto ql = ms<NullQL>();
		try{
			execute( "logs", ql );
			FAIL() << "expected a throw";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("logs"), string::npos ); //not the moved-from (empty) name.
		}
		EXPECT_EQ( ql->LogQueryCount, 1u );
		EXPECT_EQ( ql->CustomQueryCount, 0u ); //no fallback ran on the moved-from table.
		ASSERT_TRUE( ql->Consumed );
		EXPECT_EQ( ql->Consumed->JsonName, "logs" );
	}

	//"logSettings" starts with "log", so the routing has to be settled before LogSettingsQuery takes ownership - reading
	//table.JsonName afterwards decided the LogQuery branch on a moved-from string.
	TEST( TablesAwaitTests, LogSettingsQueryReturningNullDoesNotRetryAsLogQuery ){
		auto ql = ms<NullQL>();
		EXPECT_THROW( execute("logSettings", ql), Exception );
		EXPECT_EQ( ql->LogSettingsQueryCount, 1u );
		EXPECT_EQ( ql->LogQueryCount, 0u );
		EXPECT_EQ( ql->CustomQueryCount, 0u );
	}

	//logLevels is a real table:  the log routing must not swallow it, so it reaches CustomQuery with the table intact.  It used
	//to need its own hand-written exclusion from the `log` prefix (#44);  now it is simply not one of the four names.
	TEST( TablesAwaitTests, LogLevelsIsNotALogQuery ){
		auto ql = ms<NullQL>();
		EXPECT_THROW( execute("logLevels", ql), Exception ); //no schema -> the select fallback has no table.
		EXPECT_EQ( ql->LogQueryCount, 0u );
		EXPECT_EQ( ql->LogSettingsQueryCount, 0u );
		EXPECT_EQ( ql->CustomQueryCount, 1u );
		EXPECT_FALSE( ql->Consumed );
	}

	//StatusQuery returns by value, so its result is always set - the select fallback can never see the table it moved.
	//#6: the three log/status routes were dispatched with no credentials at all - TablesAwait held _creds and passed them only
	//to CustomQuery and the select fallback, so no implementation could tell an anonymous caller from anyone else.
	TEST( TablesAwaitTests, LogAndStatusRoutesReceiveTheCredentials ){
		auto ql = ms<NullQL>();
		EXPECT_THROW( execute("logs", ql), Exception ); //null handler, as above - the creds are recorded before that.
		EXPECT_EQ( ql->Received, UserPK{UserPK::System} );

		auto settings = ms<NullQL>();
		EXPECT_THROW( execute("logSettings", settings), Exception );
		EXPECT_EQ( settings->Received, UserPK{UserPK::System} );

		auto status = ms<NullQL>();
		execute( "status", status );
		EXPECT_EQ( status->Received, UserPK{UserPK::System} );
	}

	TEST( TablesAwaitTests, StatusQueryResultShortCircuitsTheSelect ){
		auto ql = ms<NullQL>();
		let y = execute( "status", ql );
		EXPECT_EQ( Json::AsBool(y.get_object(),"up"), true );
		EXPECT_EQ( ql->CustomQueryCount, 0u );
		ASSERT_TRUE( ql->Consumed );
		EXPECT_EQ( ql->Consumed->JsonName, "status" );
	}

	//#44: the prefix test was an exact-match set spelled as a prefix, and it had already been patched once - for logLevels.  Any
	//future table whose json name starts with `log` would have gone to LogQuery, which takes the table by rvalue, so there is
	//nothing to fall back on afterwards.  None of these is one of the four names, so each reaches CustomQuery with its table.
	TEST( TablesAwaitTests, ATableThatMerelyStartsWithLogIsNotALogQuery ){
		for( let jsonName : {"logbook", "logArchives", "logSettingHistory", "logSettingsHistory", "logins", "logLevel"} ){
			auto ql = ms<NullQL>();
			EXPECT_THROW( execute(jsonName, ql), Exception ) << jsonName; //no schema -> the select fallback has no table.
			EXPECT_EQ( ql->LogQueryCount, 0u ) << jsonName;
			EXPECT_EQ( ql->LogSettingsQueryCount, 0u ) << jsonName;
			EXPECT_EQ( ql->CustomQueryCount, 1u ) << jsonName;
			EXPECT_FALSE( ql->Consumed ) << jsonName; //not moved from - a misroute is unrecoverable.
		}
	}
	//and the four that are:  both spellings of each, routed to their own handler and nowhere else.
	TEST( TablesAwaitTests, TheFourLogNamesStillRoute ){
		for( let jsonName : {"log", "logs"} ){
			auto ql = ms<NullQL>();
			EXPECT_THROW( execute(jsonName, ql), Exception ) << jsonName;
			EXPECT_EQ( ql->LogQueryCount, 1u ) << jsonName;
			EXPECT_EQ( ql->LogSettingsQueryCount, 0u ) << jsonName;
		}
		for( let jsonName : {"logSetting", "logSettings"} ){
			auto ql = ms<NullQL>();
			EXPECT_THROW( execute(jsonName, ql), Exception ) << jsonName;
			EXPECT_EQ( ql->LogSettingsQueryCount, 1u ) << jsonName;
			EXPECT_EQ( ql->LogQueryCount, 0u ) << jsonName;
		}
	}
}
