//review3 #6:  TablesAwait routes logs/logSettings/status on the json name, around SelectAwait's Authorize, and the three IQL
//overloads it routes to took no credentials at all - so the log archive, the live log configuration and the status document
//answered an anonymous /graphql request.  They now take Creds like CustomQuery does, and AppQL::RequireAuthenticated is the
//gate.  Nothing here starts a server or reads an archive:  LogQLAwait's constructor only stores the query.
#include <gtest/gtest.h>
#include <jde/app/AppQL.h>
#include <jde/access/Authorize.h>
#include "helpers.h"

#define let const auto

namespace Jde::App::Tests{
	//AppQL is abstract (CustomQuery/CustomMutation stay pure);  CustomMutation asks LogSettingsMutation first, as the four apps do.
	struct QLStub final : AppQL{
		QLStub()ι:AppQL{ {}, {}, {} }{}
		QLStub( sp<Access::Authorize> authorizer, string logSchema )ι:AppQL{ {}, move(authorizer), move(logSchema) }{}
		α CustomQuery( QL::TableQL&, QL::Creds, SL )ι->up<TAwait<jvalue>> override{ return nullptr; }
		α CustomMutation( QL::MutationQL& m, QL::Creds creds, SL sl )ι->up<TAwait<jvalue>> override{ return LogSettingsMutation( m, creds, nullptr, sl ); }
	};
	Ω mutation( string command )ι->QL::MutationQL{
		static const vector<sp<DB::AppSchema>> noSchemas;
		return QL::MutationQL{ move(command), jobject{}, ms<jobject>(), optional<QL::TableQL>{}, true, noSchemas, true };
	}
	Ω anonymous()ι->QL::Creds{ return QL::Creds{ UserPK{} }; } //what Sessions::CreateSession hands a request with no Authorization header.
	Ω user()ι->QL::Creds{ return QL::Creds{ UserPK{7} }; }

	//the UI's own logs shape, as an anonymous GET /graphql would arrive, to a process with no logs resource (a standalone gateway) -
	//as are the other QLStub() anonymous refusals below.
	TEST( AppQLTests, LogQueryRefusesAnAnonymousCaller ){
		auto ql = ms<QLStub>();
		auto logs = table( "logs", "{limit:1}" );
		addColumns( addTable(logs, "entries", {}), {"level"} );
		try{
			ql->LogQuery( move(logs), anonymous(), SRCE_CUR );
			FAIL() << "expected a throw";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::Unauthorized ); //401, not a 500 - the status rides the base to the client.
			EXPECT_NE( string{e.what()}.find("logs"), string::npos ) << e.what();
		}
	}
	TEST( AppQLTests, LogQueryStillAnswersAnAuthenticatedCaller ){
		auto ql = ms<QLStub>();
		EXPECT_TRUE( ql->LogQuery(table("logs","{limit:1}"), user(), SRCE_CUR) ); //non-null LogQLAwait, nothing read yet.
	}

	TEST( AppQLTests, StatusQueryRefusesAnAnonymousCaller ){
		auto ql = ms<QLStub>();
		EXPECT_THROW( ql->StatusQuery(table("status"), anonymous(), SRCE_CUR), Exception );
	}
	//LogSettingsMutation:  every app's CustomMutation asks it first, so the anonymous refusal OpcQL alone had now covers all four.
	TEST( AppQLTests, LogSettingsMutationRefusesAnAnonymousCaller ){
		auto ql = ms<QLStub>();
		for( let command : {"updateLogSetting", "updateLogSettings"} ){
			auto m = mutation( command );
			auto y = ql->CustomMutation( m, anonymous(), SRCE_CUR );
			ASSERT_TRUE( y ) << command;
			ASSERT_TRUE( y->await_ready() ) << "a refusal answers ready - CustomMutation is noexcept, so it cannot throw";
			try{
				y->await_resume();
				FAIL() << "expected a throw";
			}
			catch( const Exception& e ){
				EXPECT_EQ( e.HttpStatus(), EHttpStatus::Unauthorized ) << command;
			}
		}
	}
	TEST( AppQLTests, LogSettingsMutationRoutesAnAuthenticatedCaller ){
		auto m = mutation( "updateLogSetting" );
		auto y = ms<QLStub>()->CustomMutation( m, user(), SRCE_CUR );
		ASSERT_TRUE( y );
		EXPECT_FALSE( y->await_ready() ) << "LogSettingsMAwait suspends; only a refusal answers ready";
	}
	TEST( AppQLTests, LogSettingsMutationLeavesOtherMutations ){
		auto m = mutation( "updateUser" );
		EXPECT_FALSE( ms<QLStub>()->CustomMutation(m, anonymous(), SRCE_CUR) );
	}
	TEST( AppQLTests, LogSettingsQueryRefusesAnAnonymousCaller ){
		auto ql = ms<QLStub>();
		EXPECT_THROW( ql->LogSettingsQuery(table("logSettings"), anonymous(), SRCE_CUR), Exception );
		EXPECT_NO_THROW( ql->LogSettingsQuery(table("logSettings"), user(), SRCE_CUR) );
	}

	//The app's own <logSchema>/logs, enforced:  Read for the log and its settings, Update to change the settings - after the user.
	struct LogsAuthorize final : Access::Authorize{
		LogsAuthorize()ι:Authorize{ "AppQLTests" }{
			CreateResource( Access::Resource{_logs, jobject{{"schemaName","app"},{"slug","logs"}}} );
			AddResource( _logs, "app", "logs", {} );
			CreateUser( user().UserPK(), "user" );
		}
		α Grant( Access::ERights rights )ι->void{ AddAcl( user().UserPK().Value, {Access::PermissionPK{10}, _logs, rights, Access::ERights::None} ); }
		constexpr static Access::ResourcePK _logs{ 1 };
	};
	Ω status( auto&& f )->EHttpStatus{
		try{
			f();
		}
		catch( const Exception& e ){
			return e.HttpStatus();
		}
		return EHttpStatus::None;
	}
	Ω mutationStatus( QLStub& ql, QL::Creds creds )->EHttpStatus{//None when it routes - only a refusal answers ready.
		auto m = mutation( "updateLogSetting" );
		auto y = ql.CustomMutation( m, creds, SRCE_CUR );
		return y && y->await_ready() ? status( [&]{ y->await_resume(); } ) : EHttpStatus::None;
	}
	TEST( AppQLTests, AnEnforcedLogsResourceGatesTheLogRoutes ){
		auto auth = ms<LogsAuthorize>();
		auto ql = ms<QLStub>( auth, "app" );
		EXPECT_EQ( status([&]{ ql->LogQuery(table("logs","{limit:1}"), anonymous(), SRCE_CUR); }), EHttpStatus::Unauthorized ) << "who is asking comes first";
		EXPECT_EQ( status([&]{ ql->LogSettingsQuery(table("logSettings"), anonymous(), SRCE_CUR); }), EHttpStatus::Unauthorized );
		EXPECT_EQ( mutationStatus(*ql, anonymous()), EHttpStatus::Unauthorized );
		EXPECT_EQ( status([&]{ ql->LogQuery(table("logs","{limit:1}"), user(), SRCE_CUR); }), EHttpStatus::Forbidden );
		EXPECT_EQ( status([&]{ ql->LogSettingsQuery(table("logSettings"), user(), SRCE_CUR); }), EHttpStatus::Forbidden );
		EXPECT_EQ( mutationStatus(*ql, user()), EHttpStatus::Forbidden );

		auth->Grant( Access::ERights::Read );
		EXPECT_TRUE( ql->LogQuery(table("logs","{limit:1}"), user(), SRCE_CUR) );
		EXPECT_NO_THROW( ql->LogSettingsQuery(table("logSettings"), user(), SRCE_CUR) );
		EXPECT_EQ( mutationStatus(*ql, user()), EHttpStatus::Forbidden ) << "changing the levels is Update, not Read";
	}
	TEST( AppQLTests, UpdateOnTheLogsResourceRoutesTheSettingsMutation ){
		auto auth = ms<LogsAuthorize>();
		auth->Grant( Access::ERights::Update );
		EXPECT_EQ( mutationStatus(*ms<QLStub>(auth, "app"), user()), EHttpStatus::None );
	}
	//Another app's logs:  an OpcServer's QL tests opc.install/logs, so app/logs enforced does not reach it.
	TEST( AppQLTests, TheLogsResourceIsTheAppsOwn ){
		auto ql = ms<QLStub>( ms<LogsAuthorize>(), "opc.install" );
		EXPECT_EQ( status([&]{ ql->LogQuery(table("logs","{limit:1}"), user(), SRCE_CUR); }), EHttpStatus::None );
	}
	//No logs row - the install default:  the log and its settings are anyone's, signed in or not.
	TEST( AppQLTests, AnUnenforcedLogsResourceIsOpen ){
		auto ql = ms<QLStub>( ms<Access::Authorize>("AppQLTests"), "app" );
		for( let& creds : {user(), anonymous()} ){
			EXPECT_EQ( status([&]{ ql->LogQuery(table("logs","{limit:1}"), creds, SRCE_CUR); }), EHttpStatus::None );
			EXPECT_EQ( status([&]{ ql->LogSettingsQuery(table("logSettings"), creds, SRCE_CUR); }), EHttpStatus::None );
			EXPECT_EQ( mutationStatus(*ql, creds), EHttpStatus::None );
		}
	}
}
