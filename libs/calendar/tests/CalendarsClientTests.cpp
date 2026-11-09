//Jde.Calendar.Tests' run of the gateway's cache against a scripted QL:  what a failed subscribe or read leaves, and that it is
//tried again.  One test, since Calendars is process-wide and configured once.
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/IQL.h>
#include <jde/ql/ql.h>
#include <jde/ql/types/Subscription.h>
#include <jde/calendar/client/Calendars.h>
#include <jde/tests/WaitUntil.h>
#include "helpers.h"

namespace Jde::Calendar::Tests{
	namespace Calendars = Client::Calendars;
	constexpr uint32 Id{ 7 };

	struct ScriptedQL final : QL::IQL{
		template<class T> Ω fail( EHttpStatus status, SL sl )ι->up<TAwait<T>>{ return mu<ExceptionAwait<T>>( mu<Exception>("scripted", ExceptionArgs{ELogTags::Test, 0, status}, sl), sl ); }
		template<class T> Ω unused( SL sl )ι->up<TAwait<T>>{ return fail<T>( EHttpStatus::InternalServerError, sl ); }
		α Authorizer()ε->Access::Authorize& override{ throw Exception{ "unused" }; }
		α AuthorizerPtr()ε->sp<Access::Authorize> override{ return nullptr; }
		α CustomQuery( QL::TableQL&, QL::Creds, SL sl )ι->up<TAwait<jvalue>> override{ return unused<jvalue>( sl ); }
		α CustomMutation( QL::MutationQL&, QL::Creds, SL sl )ι->up<TAwait<jvalue>> override{ return unused<jvalue>( sl ); }
		α LogQuery( QL::TableQL&&, QL::Creds, SL sl )ε->up<TAwait<jvalue>> override{ return unused<jvalue>( sl ); }
		α LogSettingsQuery( QL::TableQL&&, QL::Creds, SL sl )ε->up<TAwait<jvalue>> override{ return unused<jvalue>( sl ); }
		α StatusQuery( QL::TableQL&&, QL::Creds, SL )ε->jobject override{ return {}; }
		α QueryObject( string, jobject, UserPK, bool, SL sl )ε->up<TAwait<jobject>> override{ return unused<jobject>( sl ); }
		α QueryArray( string, jobject, UserPK, bool, SL sl )ε->up<TAwait<jarray>> override{ return unused<jarray>( sl ); }
		α Upsert( string, jobject, UserPK )ε->jarray override{ return {}; }
		α Schemas()Ι->const vector<sp<DB::AppSchema>>& override{ return NoSchemas(); }
		Ω NoSchemas()ι->const vector<sp<DB::AppSchema>>&{ static const vector<sp<DB::AppSchema>> none; return none; }

		α Query( string, jobject, UserPK, bool, SL sl )ε->up<TAwait<jvalue>> override{
			lg _{ Mutex };
			++Reads;
			if( ReadFailure )
				return fail<jvalue>( *ReadFailure, sl );
			return mu<CompletedAwait<jvalue>>( []()->jvalue{
				return jobject{ {"calendars", jarray{CalendarRow(Id, "line")}}, {"calendarPatterns", jarray{PatternRow(1, Id)}}, {"calendarShifts", jarray{}}, {"calendarExceptions", jarray{}} };
			}, sl );
		}
		α Subscribe( string&& query, jobject vars, sp<QL::IListener> listener, UserPK, SL sl )ε->up<TAwait<vector<QL::SubscriptionId>>> override{
			lg _{ Mutex };
			++Subscribes;
			Heard = move( listener );
			if( SubscribeFailure )
				return fail<vector<QL::SubscriptionId>>( *SubscribeFailure, sl );
			for( let& s : QL::ParseRemoteSubscriptions(move(query), move(vars), NoSchemas()) )//what the server's parse takes.
				Acked.push_back( s.Id );
			return mu<CompletedAwait<vector<QL::SubscriptionId>>>( [ids=Acked]{ return ids; }, sl );
		}
		α Unsubscribe( sp<QL::IListener>, vector<QL::SubscriptionId> ids, SL )ι->void override{ lg _{ Mutex }; Unsubscribed = move( ids ); }

		α Event()ε->void{ sp<QL::IListener> listener; { lg _{ Mutex }; listener = Heard; } listener->OnChange( jvalue{}, {} ); }
		α Script( optional<EHttpStatus> read, optional<EHttpStatus> subscribe )ι->void{ lg _{ Mutex }; ReadFailure = read; SubscribeFailure = subscribe; }
		α Counts()ι->std::pair<uint,uint>{ lg _{ Mutex }; return { Subscribes, Reads }; }
		std::mutex Mutex;
		uint Subscribes{}, Reads{};
		optional<EHttpStatus> ReadFailure, SubscribeFailure;
		sp<QL::IListener> Heard;
		vector<QL::SubscriptionId> Acked, Unsubscribed;
	};

	TEST( CalendarsClientTests, WhatFailedIsTriedAgain ){
		auto ql = ms<ScriptedQL>();
		let held = []{ return Calendars::Find( Id )!=nullptr; };
		let settled = [&]{ std::this_thread::sleep_for( 600ms ); return ql->Counts(); };//past a debounce and a retry.

		ql->Script( EHttpStatus::InternalServerError, EHttpStatus::Forbidden );
		BlockAwait( Calendars::Configure(ql, UserPK{UserPK::System}) );
		EXPECT_FALSE( held() ) << "the first load failed";
		EXPECT_TRUE( WaitUntil([&]{ let [subscribes, reads] = ql->Counts(); return subscribes>=2 && reads>=2; }) ) << "a refused subscribe and a failed read are tried again";
		ql->Script( nullopt, nullopt );
		EXPECT_TRUE( WaitUntil([&]{ lg _{ ql->Mutex }; return held() && ql->Acked.size()==18; }) ) << "the five events of three tables and the three of calendarShifts, each taken";
		EXPECT_EQ( flat_set<QL::SubscriptionId>(ql->Acked.begin(), ql->Acked.end()).size(), 18u ) << "each with its own id";

		auto before = settled();
		EXPECT_EQ( settled(), before ) << "nothing is left to retry";

		before = ql->Counts();
		ql->Script( EHttpStatus::InternalServerError, nullopt );
		ql->Event();
		EXPECT_TRUE( WaitUntil([&]{ return ql->Counts().second>=before.second+2; }) ) << "a failed reload is tried again";
		EXPECT_TRUE( held() ) << "a failed read keeps what the last load read";

		ql->Script( EHttpStatus::Forbidden, nullopt );
		EXPECT_TRUE( WaitUntil([&]{ return !held(); }) ) << "a refused read holds nothing";
		ql->Script( nullopt, nullopt );
		EXPECT_TRUE( WaitUntil(held) ) << "the retry refills it, with no event";
		EXPECT_EQ( ql->Counts().first, before.first ) << "a subscribe taken is not issued again";

		Calendars::Shutdown( false );
		lg _{ ql->Mutex };
		EXPECT_EQ( ql->Unsubscribed, ql->Acked ) << "stopped by id:  a socket's QL can't stop all of a listener's";
	}
}