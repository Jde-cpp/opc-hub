//Historian 5d (#225):  historian subscriptions (spec *Subscriptions*) over the gateway's socket - the history from a time to
//the subscription in HistSnapshot pages, each node's bound first, then every record stored as it is stored and each edit once
//durable, the ack's status per node, the refusals, and a HistUnsubscribe.
#include <jde/fwk/process/execution.h>
#include "utils/HistGroupFixture.h"
#include "utils/GatewayClientSocket.h"

namespace Jde::Opc::Gateway::Tests{
	using Web::Client::ClientSocketAwait;
	//What the socket delivered for one subscription, in order, under its own lock:  the pages, then the pushes.
	struct Heard final : IHistListener{
		α OnSnapshot( const FromServer::HistSnapshot& page )ι->void override{ lg _{ _mutex }; _pages.push_back( page ); }
		α OnRecords( const FromServer::HistRecords& records )ι->void override{ lg _{ _mutex }; _pushes.push_back( records ); }
		α OnAck( const FromServer::HistSubscribeAck& ack )ι->void override{ lg _{ _mutex }; _acks.push_back( ack ); }
		α OnError( string what )ι->void override{ lg _{ _mutex }; _errors.push_back( move(what) ); }
		α Errors()Ι->vector<string>{ lg _{ _mutex }; return _errors; }
		α Acks()Ι->vector<FromServer::HistSubscribeAck>{ lg _{ _mutex }; return _acks; }
		α Complete()Ι->bool{ lg _{ _mutex }; return !_pages.empty() && !_pages.back().more(); }
		α Values()Ι->vector<FromServer::HistValue>{//the snapshot's.
			lg _{ _mutex };
			vector<FromServer::HistValue> y;
			for( let& page : _pages )
				for( let& v : page.values() )
					y.push_back( v );
			return y;
		}
		α Pushed()Ι->vector<FromServer::HistValue>{
			lg _{ _mutex };
			vector<FromServer::HistValue> y;
			for( let& push : _pushes )
				for( let& v : push.values() )
					y.push_back( v );
			return y;
		}
		α Modifications()Ι->vector<FromServer::HistModification>{
			lg _{ _mutex };
			vector<FromServer::HistModification> y;
			for( let& push : _pushes )
				for( let& m : push.modifications() )
					y.push_back( m );
			return y;
		}
	private:
		mutable std::mutex _mutex;
		vector<FromServer::HistSnapshot> _pages;
		vector<FromServer::HistRecords> _pushes;
		vector<FromServer::HistSubscribeAck> _acks;
		vector<string> _errors;
	};

	//Over the suite's own socket (Socket()), the one the opc subscriptions ride:  a historian subscription is a message of it, not a
	//second socket.
	struct HistSubscribeTests : HistGroupFixture{
		Ω Subscribe( uint32 group, const vector<ExNodeId>& nodes, optional<TimePoint> from, sp<Heard> heard )ε->vector<StatusCode>{
			return BlockAwait<ClientSocketAwait<vector<StatusCode>>,vector<StatusCode>>( Socket().HistSubscribe(group, nodes, from, move(heard)) );
		}
		Ω Unsubscribe( RequestId subscription )ε->bool{ return BlockAwait<ClientSocketAwait<bool>,bool>( Socket().HistUnsubscribe(subscription) ); }
		Ω Last()ι->RequestId{ return Socket().LastHistSubscription(); }
		Ω WaitComplete( const Heard& heard )ε->void{
			Stopwatch sw;
			while( !heard.Complete() )
				sw.CheckTimeout( 15s, 10ms );
		}
		Ω WaitPushed( const Heard& heard, uint values, uint modifications=0 )ε->void{
			Stopwatch sw;
			while( heard.Pushed().size()<values || heard.Modifications().size()<modifications )
				sw.CheckTimeout( 15s, 10ms );
		}
		Ω Time( const google::protobuf::Timestamp& t )ι->TimePoint{ return UADateTime{ t }.Time(); }
		Ω Ex( UA_UInt32 i )ι->ExNodeId{ return ExNodeId{ HistGroupFixture::Ex(i) }; }
	};

	//The snapshot from a time:  the node's value before it as the bound, then the records from it, complete in one page here;
	//then each value stored streams, and none after the Unsubscribe.
	TEST_F( HistSubscribeTests, SnapshotThenStream ){
		let [group, rpm, host, _] = OneNodeGroup{ "subscribe", RpmManual };
		let t0 = Since();
		let base = BaseRpm();
		for( uint i=0; i<3; ++i )
			WriteRpm( base+i*100, t0+milliseconds{10*i} );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 3) );

		auto heard = ms<Heard>();
		EXPECT_EQ( Subscribe(group, {Ex(RpmManual)}, t0+5ms, heard), vector<StatusCode>{UA_STATUSCODE_GOOD} );
		let subscription = Last();
		ASSERT_NO_THROW( WaitComplete(*heard) );
		let values = heard->Values();
		ASSERT_EQ( values.size(), 3u ) << "the bound, then the two from t0+5ms";
		EXPECT_TRUE( values[0].bound() );
		EXPECT_EQ( values[0].value().double_value(), base );
		EXPECT_EQ( Time(values[0].source()), t0 );
		EXPECT_FALSE( values[1].bound() );
		EXPECT_EQ( values[1].value().double_value(), base+100 );
		EXPECT_EQ( Time(values[1].source()), t0+10ms );
		EXPECT_EQ( values[2].value().double_value(), base+200 );
		for( let& v : values ){
			EXPECT_EQ( v.node(), 0u );
			EXPECT_EQ( v.sc(), UA_STATUSCODE_GOOD );
			EXPECT_TRUE( v.has_server() );
			EXPECT_FALSE( v.heartbeat() );
		}
		EXPECT_TRUE( heard->Pushed().empty() );

		WriteRpm( base+300, t0+30ms );
		ASSERT_NO_THROW( WaitPushed(*heard, 1) );
		let pushed = heard->Pushed();
		ASSERT_EQ( pushed.size(), 1u );
		EXPECT_EQ( pushed[0].value().double_value(), base+300 );
		EXPECT_EQ( Time(pushed[0].source()), t0+30ms );
		EXPECT_EQ( pushed[0].node(), 0u );
		EXPECT_FALSE( pushed[0].bound() );
		EXPECT_EQ( heard->Values().size(), 3u ) << "a push isn't a page";

		let strays = Socket().HistStrays();
		EXPECT_TRUE( Unsubscribe(subscription) );
		WriteRpm( base+400, t0+40ms );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 5) );
		std::this_thread::sleep_for( 200ms );
		EXPECT_EQ( heard->Pushed().size(), 1u ) << "nothing after the unsubscribe";
		EXPECT_EQ( Socket().HistStrays(), strays );
		EXPECT_THROW( Unsubscribe(subscription), GatewayErrorResponse ) << "ended already";
		EXPECT_THROW( Unsubscribe(999'999), GatewayErrorResponse );
	}

	//The ack's statuses:  Good for a member, Bad_NodeIdUnknown for a node the group doesn't hold, which the snapshot then
	//leaves out;  a Subscribe without a time snapshots each node's value now, as its bound;  and the refusals - a group the
	//gateway doesn't host, no nodes, a member named twice.
	TEST_F( HistSubscribeTests, StatusesAndRefusals ){
		let [group, _, _, _] = OneNodeGroup{ "statuses", RpmManual };
		auto heard = ms<Heard>();
		EXPECT_EQ( Subscribe(group, {Ex(Rpm2), Ex(RpmManual)}, nullopt, heard), (vector<StatusCode>{UA_STATUSCODE_BADNODEIDUNKNOWN, UA_STATUSCODE_GOOD}) );
		ASSERT_NO_THROW( WaitComplete(*heard) );
		let values = heard->Values();
		ASSERT_EQ( values.size(), 1u ) << "the member's value now, as the bound";
		EXPECT_TRUE( values[0].bound() );
		EXPECT_EQ( values[0].node(), 1u ) << "its place in the Subscribe's nodes";
		EXPECT_TRUE( Unsubscribe(Last()) );

		auto none = ms<Heard>();
		EXPECT_EQ( Subscribe(group, {Ex(Rpm2)}, nullopt, none), vector<StatusCode>{UA_STATUSCODE_BADNODEIDUNKNOWN} );
		ASSERT_NO_THROW( WaitComplete(*none) );
		EXPECT_TRUE( none->Values().empty() );
		EXPECT_TRUE( Unsubscribe(Last()) );

		EXPECT_THROW( Subscribe(999'999, {Ex(RpmManual)}, nullopt, ms<Heard>()), GatewayErrorResponse ) << "no such group";
		EXPECT_THROW( Subscribe(group, {}, nullopt, ms<Heard>()), GatewayErrorResponse ) << "no nodes";
		EXPECT_THROW( Subscribe(group, {Ex(RpmManual), ExNodeId{Node(RpmManual)}}, nullopt, ms<Heard>()), GatewayErrorResponse ) << "one member by two spellings";
		//A socket that bound no session may not subscribe.
		auto unbound = GatewayClientSocket::Open( "localhost", GatewayPort(), nullopt );
		auto unboundSubscribe = [&]{ return BlockAwait<ClientSocketAwait<vector<StatusCode>>,vector<StatusCode>>( unbound->HistSubscribe(group, {Ex(RpmManual)}, nullopt, ms<Heard>()) ); };
		EXPECT_THROW( unboundSubscribe(), GatewayErrorResponse );
		BlockVoidAwait( unbound->Close(false, SRCE_CUR) );
	}

	//Two HistSubscribe under one request id, back to back:  the one that claims the id streams, and the other is refused without
	//touching it, however the two registrations finish.
	TEST_F( HistSubscribeTests, ADuplicateIdIsRefusedAlone ){
		let [group, _, _, _] = OneNodeGroup{ "duplicate", RpmManual };
		let refusals = Socket().HistRefusals();
		auto heard = ms<Heard>();
		EXPECT_EQ( (BlockAwait<ClientSocketAwait<vector<StatusCode>>,vector<StatusCode>>(Socket().HistSubscribeTwice(group, {Ex(RpmManual)}, nullopt, heard))), vector<StatusCode>{UA_STATUSCODE_GOOD} );
		ASSERT_NO_THROW( WaitComplete(*heard) );
		Stopwatch sw;
		while( Socket().HistRefusals()==refusals )
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		EXPECT_EQ( Socket().HistRefusals(), refusals+1 );
		EXPECT_EQ( Socket().LastHistRefusal(), Ƒ("Subscription {} is live:  a HistSubscribe's request id names it.", hex(Last())) );
		EXPECT_EQ( heard->Values().size(), 1u ) << "one snapshot:  the refused copy wrote no pages";

		let t0 = Since();
		let base = BaseRpm();
		WriteRpm( base, t0 );
		ASSERT_NO_THROW( WaitPushed(*heard, 1) );
		EXPECT_EQ( heard->Pushed().back().value().double_value(), base );
		EXPECT_TRUE( Unsubscribe(Last()) );
	}

	//A HistUnsubscribe that lands while the subscription registers ends it:  the unsubscribe completes, the HistSubscribe is
	//still answered with its ack, and the group keeps no registration and pushes nothing.  The pages are left alone:  the
	//rare registration that beats the unsubscribe writes one before it.
	TEST_F( HistSubscribeTests, UnsubscribedWhileRegistering ){
		let [group, rpm, host, _] = OneNodeGroup{ "unsubscribed", RpmManual };
		auto heard = ms<Heard>();
		EXPECT_TRUE( (BlockAwait<ClientSocketAwait<bool>,bool>(Socket().HistSubscribeUnsubscribe(group, {Ex(RpmManual)}, nullopt, heard))) );
		Stopwatch sw;
		while( heard->Acks().empty() )
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		let acks = heard->Acks();
		ASSERT_EQ( acks.size(), 1u );
		ASSERT_EQ( acks[0].statuses_size(), 1 );
		EXPECT_EQ( acks[0].statuses(0), UA_STATUSCODE_GOOD );
		EXPECT_EQ( host->Group()->Subscribers(), 0u ) << "the registration, made after the end, ended with it";

		let t0 = Since();
		WriteRpm( BaseRpm(), t0 );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 1) );
		std::this_thread::sleep_for( 200ms );
		EXPECT_TRUE( heard->Pushed().empty() );
	}

	//No page or push of a subscription arrives after its unsubscribe's ack, though it lands while the snapshot pages:  10'001
	//values are two pages of readLimit, and the unsubscribe goes as the ack arrives, while the first page is read.  They are
	//inserted a thousand an edit, under the socket's 1 MB message cap.
	TEST_F( HistSubscribeTests, NothingFollowsTheUnsubscribe ){
		let [group, _, _, _] = OneNodeGroup{ "paging", RpmManual };
		auto open = EnforcedHistGroups();
		let t0 = Since()-1h;
		constexpr uint count{ Hist::Settings::DefaultReadLimit+1 };
		for( uint i=0; i<count; ){
			vector<Sample> samples;
			for( ; i<count && samples.size()<1000; ++i )
				samples.push_back( {RpmManual, t0+milliseconds{i}, (double)i} );
			let inserted = Update( "createHistory", group, samples ).Values;
			ASSERT_EQ( std::ranges::count(inserted, UA_STATUSCODE_GOODENTRYINSERTED), (long)samples.size() );
		}

		let strays = Socket().HistStrays();
		auto heard = ms<Heard>();
		EXPECT_EQ( Subscribe(group, {Ex(RpmManual)}, t0, heard), vector<StatusCode>{UA_STATUSCODE_GOOD} );
		EXPECT_TRUE( Unsubscribe(Last()) );
		let paged = heard->Values().size();
		std::this_thread::sleep_for( 500ms );
		EXPECT_EQ( Socket().HistStrays(), strays );
		INFOT( ELogTags::Test, "Values paged before the unsubscribe's ack:  {} of {}.", paged, count+1 );
	}

	//Removing the group ends its subscription:  the client hears an exception under the subscription's id, and the id is
	//free, so a HistUnsubscribe of it is refused.
	TEST_F( HistSubscribeTests, ARemovedGroupEndsItsSubscription ){
		let [group, _, _, _] = OneNodeGroup{ "removed", RpmManual };
		auto heard = ms<Heard>();
		EXPECT_EQ( Subscribe(group, {Ex(RpmManual)}, nullopt, heard), vector<StatusCode>{UA_STATUSCODE_GOOD} );
		let subscription = Last();
		ASSERT_NO_THROW( WaitComplete(*heard) );
		Mutate( "deleteHistGroup( id:$id )", {{"id", group}} );
		Stopwatch sw;
		while( heard->Errors().empty() )
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		let errors = heard->Errors();
		ASSERT_EQ( errors.size(), 1u );
		EXPECT_NE( errors[0].find("removed"), string::npos ) << errors[0];
		EXPECT_THROW( Unsubscribe(subscription), GatewayErrorResponse ) << "the id is free";
	}

	//An edit's Modification records are pushed to a subscription of the node once they are durable, with the value put there.
	TEST_F( HistSubscribeTests, ModificationsArePushed ){
		let [group, rpm, host, _] = OneNodeGroup{ "modifications", RpmManual };
		let t0 = Since();
		let base = BaseRpm();
		WriteRpm( base, t0 );
		WriteRpm( base+100, t0+20ms );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 2) );
		auto heard = ms<Heard>();
		EXPECT_EQ( Subscribe(group, {Ex(RpmManual)}, t0, heard), vector<StatusCode>{UA_STATUSCODE_GOOD} );
		ASSERT_NO_THROW( WaitComplete(*heard) );
		EXPECT_EQ( heard->Values().size(), 2u );

		auto open = EnforcedHistGroups();
		EXPECT_EQ( Update("createHistory", group, {{RpmManual, t0+10ms, 7}}).Values, vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED} );
		EXPECT_EQ( Update("updateHistory", group, {{RpmManual, t0+20ms, 8}}).Values, vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED} );
		ASSERT_NO_THROW( WaitPushed(*heard, 0, 2) );
		let mods = heard->Modifications();
		ASSERT_EQ( mods.size(), 2u );
		EXPECT_EQ( mods[0].type(), 1u ) << "insert";
		EXPECT_EQ( Time(mods[0].target()), t0+10ms );
		EXPECT_EQ( mods[0].value().value().double_value(), 7 );
		EXPECT_EQ( mods[1].type(), 2u ) << "replace";
		EXPECT_EQ( Time(mods[1].target()), t0+20ms );
		EXPECT_EQ( mods[1].value().value().double_value(), 8 );
		for( let& m : mods ){
			EXPECT_EQ( m.node(), 0u );
			EXPECT_FALSE( m.user().empty() );
			EXPECT_TRUE( m.has_time() );
		}
		EXPECT_TRUE( heard->Pushed().empty() ) << "an edit stores no value";
		EXPECT_EQ( PurgeAt(group, RpmManual, {t0+10ms}).Values, vector<StatusCode>{UA_STATUSCODE_GOOD} );
		ASSERT_NO_THROW( WaitPushed(*heard, 0, 3) );
		EXPECT_EQ( heard->Modifications()[2].type(), 4u ) << "delete";
		EXPECT_FALSE( heard->Modifications()[2].has_value() );
		EXPECT_TRUE( Unsubscribe(Last()) );
	}
}