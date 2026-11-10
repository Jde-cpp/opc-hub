//OEE phase 5 (#403):  live OEE over the gateway's socket (docs/oee/spec.md *Live*, *Authorization*) - an OeeSubscribe's ack
//with the machine's status, a push at once on a state change, the shift's figures every `live` at most, an OeeUnsubscribe,
//the refusals, the socket dropped and the subscription ended with its machine.  On the suite's manual clock:  a feed's `live`
//timer runs only when a test moves it.
#include "utils/OeeFixture.h"
#include "utils/GatewayClientSocket.h"
#include "../src/oee/OeeFeed.h"

namespace Jde::Opc::Gateway::Tests{
	using Web::Client::ClientSocketAwait;
	//What the socket delivered for one subscription after its ack, in order.
	struct OeeHeard final : IOeeListener{
		α OnStatus( const FromServer::OeeStatus& status )ι->void override{ lg _{ _mutex }; _statuses.push_back( status ); }
		α OnError( string what )ι->void override{ lg _{ _mutex }; _errors.push_back( move(what) ); }
		α Count()Ι->uint{ lg _{ _mutex }; return _statuses.size(); }
		α Last()Ι->FromServer::OeeStatus{ lg _{ _mutex }; return _statuses.empty() ? FromServer::OeeStatus{} : _statuses.back(); }
		α Errors()Ι->vector<string>{ lg _{ _mutex }; return _errors; }
	private:
		mutable std::mutex _mutex;
		vector<FromServer::OeeStatus> _statuses;
		vector<string> _errors;
	};

	struct OeeSubscribeTests : OeeFixture{
		Ω Subscribe( GatewayClientSocket& socket, uint32 machine, sp<OeeHeard> heard )ε->FromServer::OeeStatus{
			return BlockAwait<ClientSocketAwait<FromServer::OeeStatus>,FromServer::OeeStatus>( socket.OeeSubscribe(machine, move(heard)) );
		}
		Ω Subscribe( uint32 machine, sp<OeeHeard> heard )ε->FromServer::OeeStatus{ return Subscribe( Socket(), machine, move(heard) ); }
		Ω Unsubscribe( RequestId subscription )ε->bool{ return BlockAwait<ClientSocketAwait<bool>,bool>( Socket().OeeUnsubscribe(subscription) ); }
		Ω Last()ι->RequestId{ return Socket().LastOeeSubscription(); }
		Ω Time( const google::protobuf::Timestamp& t )ι->TimePoint{ return UADateTime{ t }.Time(); }
		//Every status the feed has pushed has arrived:  what the clock's advance just pushed is counted before the next step.
		Ω WaitHeard( const OeeFeed& feed, const OeeHeard& heard )ε->uint{
			THROW_IF( !WaitUntil([&]{ return heard.Count()==feed.Pushed(); }, 15s), "{} of the feed's {} pushes arrived.", heard.Count(), feed.Pushed() );
			return heard.Count();
		}
		Ω WaitFor( const OeeHeard& heard, function<bool( const FromServer::OeeStatus& )> is, sv what )ε->FromServer::OeeStatus{
			THROW_IF( !WaitUntil([&]{ return heard.Count() && is( heard.Last() ); }, 15s), "No push {}.", what );
			return heard.Last();
		}
	};

	//The ack carries the state, the shift and its figures.  A count alone pushes nothing until `live` has passed, then the
	//shift's figures with it;  `status` written off is pushed at once, with the stop in progress, and on again the same;  and
	//nothing follows an OeeUnsubscribe.
	TEST_F( OeeSubscribeTests, TheAckAStateChangeAtOnceAndTheFiguresEveryLive ){
		EarlyInTheMinute();
		let from = floor<seconds>( Now() )-5s;
		Pump1Machine pump{ "oeeLive", from-2s };
		let shiftStart = floor<minutes>( from )-10min;
		let calendar = ShiftCalendar( "oeeLive", shiftStart, 120 );
		absl::Cleanup purgeCalendar = [=]{ PurgeCalendar( calendar ); };
		let id = pump.Create( from, {ERole::State, ERole::Total, ERole::Reject, ERole::Reason}, {{"smallStop", 3000}, {"idealCycle", 3000.0}, {"calendarId", calendar}} );
		auto engine = WaitLive( id );
		let feed = Oee().Feed( id );
		ASSERT_TRUE( feed );
		EXPECT_EQ( feed->Subscribers(), 0u );

		auto heard = ms<OeeHeard>();
		let ack = Subscribe( id, heard );
		let subscription = Last();
		EXPECT_EQ( ack.state(), FromServer::OeeRunning );
		EXPECT_TRUE( ack.has_since() );
		EXPECT_FALSE( ack.has_stop() );
		EXPECT_TRUE( ack.planned() );
		EXPECT_EQ( ack.shift().name(), "Written" );
		EXPECT_EQ( Time(ack.shift().start()), shiftStart );
		EXPECT_EQ( Time(ack.shift().end()), shiftStart+120min );
		ASSERT_TRUE( ack.has_figures() );
		EXPECT_EQ( Time(ack.figures().start()), shiftStart );
		EXPECT_EQ( ack.figures().total(), 0 );
		EXPECT_TRUE( ack.figures().partial() ) << "the machine's rows start inside the shift";
		EXPECT_EQ( feed->Subscribers(), 1u );

		//A count alone:  nothing until the clock passes `live`, then the shift's figures with it.
		let counted = Now();
		let before = WaitHeard( *feed, *heard );
		WriteTotal( 101, counted );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), counted, 1 );
		ASSERT_TRUE( WaitUntil([&]{ auto s = engine->State(); return s && s->LastCount && *s->LastCount==counted; }, 15s) );
		EXPECT_EQ( feed->Pushed(), before ) << "a count is no state change";
		_clock->Advance( Oee().Live() );
		let moved = WaitFor( *heard, []( let& s ){ return s.figures().total()==1; }, "of the count" );
		EXPECT_EQ( moved.figures().good(), 1 );
		EXPECT_EQ( moved.figures().measured(), 7u ) << "availability, performance and quality, once a unit is counted";
		EXPECT_EQ( Time(moved.last_count()), counted );
		EXPECT_EQ( moved.state(), FromServer::OeeRunning );
		EXPECT_GT( Time(moved.figures().end()), Time(ack.figures().end()) );

		//`status` written off:  pushed at once, the clock where it was, the stop's reason the one its code names.
		let jam = HistGroupFixture::Id( Mutate("createOeeReason( name:$name, slug:$name, category:$category, code:7 ){ id }", {{"name", "oeeLiveJam"}, {"category", "Unplanned"}}) );
		absl::Cleanup purgeReason = [=]{ PurgeReason( jam ); };
		WriteReason( 7, Now() );
		let stopped = Now();
		WaitHeard( *feed, *heard );
		WriteStatus( false, stopped );
		let stop = WaitFor( *heard, []( let& s ){ return s.state()==FromServer::OeeStopped && s.stop().has_reason(); }, "of the stop and its reason" );
		ASSERT_TRUE( stop.has_stop() );
		EXPECT_EQ( Time(stop.stop().start()), stopped );
		EXPECT_EQ( Time(stop.since()), stopped );
		EXPECT_TRUE( stop.stop().small() ) << "shorter than smallStop so far";
		EXPECT_EQ( stop.stop().code(), 7 );
		EXPECT_EQ( stop.stop().reason().id(), jam );
		EXPECT_EQ( stop.stop().reason().name(), "oeeLiveJam" );
		EXPECT_EQ( stop.stop().reason().category(), "Unplanned" );
		EXPECT_EQ( stop.figures().total(), 1 );
		EXPECT_EQ( stop.figures().unplanned(), 0 ) << "a small stop is run time";
		_clock->Advance( 3500ms );//past smallStop:  pushed as the engine's timer finds it.
		let grown = WaitFor( *heard, []( let& s ){ return s.has_stop() && !s.stop().small(); }, "of the stop passing smallStop" );
		EXPECT_GT( grown.figures().unplanned(), 0 );
		let running = Now();
		WaitHeard( *feed, *heard );
		WriteStatus( true, running );
		let ran = WaitFor( *heard, []( let& s ){ return s.state()==FromServer::OeeRunning; }, "of the start" );
		EXPECT_FALSE( ran.has_stop() );
		EXPECT_EQ( Time(ran.since()), running );

		//Unsubscribed:  out of the feed, and neither a state change nor the clock reaches the socket.
		WaitHeard( *feed, *heard );
		let strays = Socket().OeeStrays();
		EXPECT_TRUE( Unsubscribe(subscription) );
		EXPECT_EQ( feed->Subscribers(), 0u );
		let pushed = feed->Pushed();
		let count = heard->Count();
		let off = Now();
		WriteStatus( false, off );
		ASSERT_TRUE( WaitUntil([&]{ auto s = engine->State(); return s && s->State==Oee::EState::Stopped; }, 15s) );
		_clock->Advance( Oee().Live()*2 );
		std::this_thread::sleep_for( 200ms );
		EXPECT_EQ( feed->Pushed(), pushed );
		EXPECT_EQ( heard->Count(), count );
		EXPECT_EQ( Socket().OeeStrays(), strays );
		EXPECT_THROW( Unsubscribe(subscription), GatewayErrorResponse ) << "ended already";
		EXPECT_THROW( Unsubscribe(999'999), GatewayErrorResponse );
	}

	//A shift's break over now:  the ack's `planned` is false inside the shift, and its figures plan none of the break.
	TEST_F( OeeSubscribeTests, PlannedIsFalseInABreak ){
		let from = floor<seconds>( Now() )-5s;
		Pump1Machine pump{ "oeeLiveBreak", from-2s };
		let shiftStart = floor<minutes>( from )-10min;
		let calendar = ShiftCalendar( "oeeLiveBreak", shiftStart, 120, {jobject{{"start", 5}, {"minutes", 110}}} );
		absl::Cleanup purgeCalendar = [=]{ PurgeCalendar( calendar ); };
		let id = pump.Create( from, {ERole::State, ERole::Total, ERole::Reject, ERole::Reason}, {{"calendarId", calendar}} );
		WaitLive( id );
		let ack = Subscribe( id, ms<OeeHeard>() );
		EXPECT_EQ( ack.state(), FromServer::OeeRunning );
		EXPECT_FALSE( ack.planned() );
		EXPECT_EQ( ack.shift().name(), "Written" );
		ASSERT_TRUE( ack.has_figures() );
		EXPECT_EQ( ack.figures().planned(), 0 ) << "the machine's rows start in the break";
		EXPECT_TRUE( Unsubscribe(Last()) );
	}

	//The refusals - no machine, no id, a socket that bound no session, a caller without Read on the group, an id a
	//subscription holds;  a machine with no calendar, whose figures are the UTC day's;  the socket dropped, which takes the
	//subscription out of the feed;  and the machine deleted, which ends it with an exception under its id and refuses another.
	TEST_F( OeeSubscribeTests, TheRefusalsADroppedSocketAndADeletedMachine ){
		let from = floor<seconds>( Now() )-5s;
		Pump1Machine pump{ "oeeLiveEnds", from-2s };
		let id = pump.Create( from );
		WaitLive( id );
		let feed = Oee().Feed( id );
		ASSERT_TRUE( feed );

		EXPECT_EQ( Status([&]{ Subscribe( 999'999, ms<OeeHeard>() ); }), EHttpStatus::NotFound );
		EXPECT_EQ( Status([&]{ Subscribe( 0, ms<OeeHeard>() ); }), EHttpStatus::BadRequest );
		auto unbound = GatewayClientSocket::Open( "localhost", GatewayPort(), nullopt );
		EXPECT_THROW( Subscribe(*unbound, id, ms<OeeHeard>()), GatewayErrorResponse );
		BlockAwait( unbound->Close(false, SRCE_CUR) );
		{//the group's own row grants the suite's user no Read.
			auto enforced = EnforcedHistGroups();
			ScopeHistGroup( pump.Group, Access::ERights::None );
			absl::Cleanup unscope = [&]{ try{ UnscopeHistGroup( pump.Group ); }catch( const std::exception& e ){ ADD_FAILURE() << "the group's resource wasn't deleted:  " << e.what(); } };
			EXPECT_EQ( Status([&]{ Subscribe( id, ms<OeeHeard>() ); }), EHttpStatus::Forbidden );
		}
		EXPECT_EQ( feed->Subscribers(), 0u ) << "none joined past a refusal";

		auto dropped = GatewayClientSocket::Open( "localhost", GatewayPort(), AppClient()->SessionId() );
		let day = Subscribe( *dropped, id, ms<OeeHeard>() );
		EXPECT_EQ( day.state(), FromServer::OeeRunning );
		EXPECT_TRUE( day.planned() ) << "no calendar plans every minute";
		EXPECT_FALSE( day.has_shift() );
		ASSERT_TRUE( day.has_figures() );
		EXPECT_EQ( Time(day.figures().start()), TimePoint{floor<days>(from)} ) << "the UTC day's";
		EXPECT_EQ( feed->Subscribers(), 1u );
		BlockAwait( dropped->Close(false, SRCE_CUR) );
		EXPECT_TRUE( WaitUntil([&]{ return feed->Subscribers()==0; }, 15s) ) << "the socket's close ends its subscriptions";

		auto heard = ms<OeeHeard>();
		Subscribe( id, heard );
		let subscription = Last();
		EXPECT_EQ( feed->Subscribers(), 1u );
		Mutate( "deleteOeeMachine( id:$id )", {{"id", id}} );
		ASSERT_TRUE( WaitUntil([&]{ return !heard->Errors().empty(); }, 15s) );
		let errors = heard->Errors();
		ASSERT_EQ( errors.size(), 1u );
		EXPECT_NE( errors[0].find("deleted"), string::npos ) << errors[0];
		EXPECT_EQ( feed->Subscribers(), 0u );
		EXPECT_THROW( Unsubscribe(subscription), GatewayErrorResponse ) << "the id is free";
		EXPECT_EQ( Status([&]{ Subscribe( id, ms<OeeHeard>() ); }), EHttpStatus::NotFound ) << "a deleted machine has no live feed";
	}
}