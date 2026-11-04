//review3 #34:  the LocalSubscriptions registry had no ql-side test - the only in-tree callers of Subscriptions:: outside the
//library are three access tests that assert id recovery in the payload with one listener, not registry behaviour.  So nothing
//pinned the exact {TableName,Type} key (which #8 fell foul of), StopListen's empty-ids-means-all, the per-listener throw
//swallowing (#55), or the isApplicable filter.  DB-free: a system MutationQL resolves no DBTable, so MutationQL::TableName()
//is "" and a Subscription built with that name matches the same key; findId returns early with no pk, so nothing opens a
//data source; Listen/OnMutation/StopListen are static.
#include <gtest/gtest.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/ql/LocalSubscriptions.h>
#include "NullQL.h"
#include "RecordingListener.h"
#include <jde/ql/types/MutationQL.h>
#include <jde/ql/types/Parser.h>

#define let const auto

namespace Jde::QL::Tests{
	static const vector<sp<DB::AppSchema>> _noSubSchemas;

	//`status` is a system name, so TableQL leaves _dbTable null and the subscription's key is the mutation's: {"",Type}.
	Ω fields( std::initializer_list<sv> columns, jobject args={} )ε->TableQL{
		TableQL y{ "status", move(args), ms<jobject>(), _noSubSchemas, true };
		for( let& c : columns )
			y.Columns.emplace_back( ColumnQL{string{c}} );
		return y;
	}
	Ω listen( sp<RecordingListener> listener, SubscriptionId id, TableQL&& f, string tableName="", EMutationQL type=EMutationQL::Create )ι->void{
		vector<Subscription> subs;
		subs.emplace_back( move(tableName), type, move(f) );
		subs.back().Id = id;
		Subscriptions::Listen( listener, move(subs) );
	}
	Ω mutated( sv command="createStatus", sv args=R"({id:1, name:"x", extra:2})" )ε->MutationQL{
		return MutationQL{ string{command}, Parser::ParseArgs(string{args}), ms<jobject>(), optional<TableQL>{}, true, _noSubSchemas, true };
	}

	struct SubscriptionsTests : ::testing::Test{
		//_serverSubs is process-wide and the other suites in this binary share it - every test leaves it as it found it.
		α TearDown()->void override{
			for( auto& listener : _listeners )
				Subscriptions::StopListen( listener );
			_listeners.clear();
		}
		α Listener( str name )ι->sp<RecordingListener>{ return _listeners.emplace_back( ms<RecordingListener>(name) ); }
	private:
		vector<sp<RecordingListener>> _listeners;
	};

	//The fan-out, and the projection with it:  each subscriber is delivered the columns it asked for, keyed by the table's json
	//name - two subscribers on the same {table,op} get different shapes of the same event.
	TEST_F( SubscriptionsTests, EverySubscriberIsNotifiedWithItsOwnColumns ){
		auto wide = Listener( "wide" );
		auto narrow = Listener( "narrow" );
		listen( wide, 11, fields({"id","name"}) );
		listen( narrow, 12, fields({"id"}) );

		Subscriptions::OnMutation( mutated(), jobject{{"rowCount",1}} );

		ASSERT_EQ( wide->Changes.size(), 1u );
		EXPECT_EQ( wide->Changes[0].first, 11u );
		EXPECT_EQ( wide->Payload(0).at("id").to_number<uint>(), 1u );
		EXPECT_EQ( wide->Payload(0).at("name").as_string(), "x" );
		EXPECT_FALSE( wide->Payload(0).contains("extra") ); //an arg nobody asked for is not delivered.
		ASSERT_EQ( narrow->Changes.size(), 1u );
		EXPECT_EQ( narrow->Changes[0].first, 12u );
		EXPECT_EQ( narrow->Payload(0).size(), 1u ); //id only - TrimColumns is per subscription, not per event.
	}

	//The key is looked up exactly ({TableName,Type} with operator< over both), so the two spellings of a table have to meet -
	//#8 was a subscription keyed `permissions` against a mutation publishing `permission_rights`, and it just went quiet.
	TEST_F( SubscriptionsTests, TheKeyIsExactInBothHalves ){
		auto other = Listener( "otherTable" );
		auto update = Listener( "otherType" );
		auto match = Listener( "match" );
		listen( other, 21, fields({"id"}), "permissions" ); //right op, wrong table.
		listen( update, 22, fields({"id"}), "", EMutationQL::Update ); //right table, wrong op.
		listen( match, 23, fields({"id"}) );

		Subscriptions::OnMutation( mutated(), jobject{} );

		EXPECT_TRUE( other->Changes.empty() );
		EXPECT_TRUE( update->Changes.empty() );
		EXPECT_EQ( match->Changes.size(), 1u );
	}

	//#55: OnChange is ε and a subscriber is arbitrary downstream code, so one that throws must cost only its own notification.
	//The catch is per listener, inside the loop - not the outer one, which would have taken the rest of the fan-out with it.
	//And it is not silent (ql-refactor B7): the listener's name and what it threw are logged.
	TEST_F( SubscriptionsTests, AThrowingListenerDoesNotCostTheNextOneItsNotification ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() ); //captures every level; self-contained, no shared-config change.
		auto& logger = Logging::GetLogger<Logging::MemoryLog>();
		Logging::ClearMemory();
		auto bad = Listener( "bad" );
		auto good = Listener( "good" );
		bad->Throws = true;
		listen( bad, 31, fields({"id"}) );
		listen( good, 32, fields({"id"}) );

		Subscriptions::OnMutation( mutated(), jobject{} );

		EXPECT_EQ( bad->Changes.size(), 1u ); //it was called, and it threw.
		EXPECT_EQ( good->Changes.size(), 1u ); //and the next one still heard about it.
		let warnings = logger.Find( [](let& entry){ return entry.Message().find("listener threw")!=string::npos; } );
		ASSERT_EQ( warnings.size(), 1u ) << "the refusal was swallowed";
		EXPECT_EQ( warnings[0].Level, ELogLevel::Warning );
	}

	//The 3-arg overload's predicate - the caller's scoping, alongside the subscriber's own args since #53.  It is handed the
	//TableQL and decides per subscription, before the payload is built.
	TEST_F( SubscriptionsTests, IsApplicableFiltersPerSubscription ){
		auto wanted = Listener( "wanted" );
		auto skipped = Listener( "skipped" );
		listen( wanted, 41, fields({"id","name"}) );
		listen( skipped, 42, fields({"id"}) );

		Subscriptions::OnMutation( mutated(), jobject{}, []( TableQL& f ){ return f.FindColumn("name")!=nullptr; } );

		EXPECT_EQ( wanted->Changes.size(), 1u );
		EXPECT_TRUE( skipped->Changes.empty() );
	}

	//StopListen's two modes, and #56's premise:  an id list removes exactly those, an empty list removes all of that listener's.
	//Both return what they removed, and neither touches another listener's registrations.
	TEST_F( SubscriptionsTests, StopListenWithIdsRemovesOnlyThose ){
		auto listener = Listener( "two" );
		auto other = Listener( "other" );
		listen( listener, 51, fields({"id"}) );
		listen( listener, 52, fields({"id"}) );
		listen( other, 53, fields({"id"}) );

		let removed = Subscriptions::StopListen( listener, {51} );
		ASSERT_EQ( removed.size(), 1u );
		EXPECT_EQ( removed[0].to_number<uint>(), 51u );

		Subscriptions::OnMutation( mutated(), jobject{} );
		ASSERT_EQ( listener->Changes.size(), 1u );
		EXPECT_EQ( listener->Changes[0].first, 52u ); //51 is gone, 52 is not.
		EXPECT_EQ( other->Changes.size(), 1u );
	}
	TEST_F( SubscriptionsTests, StopListenWithNoIdsRemovesAllOfThatListenersOnly ){
		auto listener = Listener( "all" );
		auto other = Listener( "other" );
		listen( listener, 61, fields({"id"}) );
		listen( listener, 62, fields({"id"}), "", EMutationQL::Update ); //a second key, to prove the sweep is over the whole map.
		listen( other, 63, fields({"id"}) );

		EXPECT_EQ( Subscriptions::StopListen(listener).size(), 2u );
		EXPECT_TRUE( Subscriptions::StopListen(listener).empty() ); //idempotent - a second unsubscribe is not an error.

		Subscriptions::OnMutation( mutated(), jobject{} );
		Subscriptions::OnMutation( mutated("updateStatus"), jobject{} );
		EXPECT_TRUE( listener->Changes.empty() );
		EXPECT_EQ( other->Changes.size(), 1u ); //still registered, and still keyed {"" ,Create}.
	}

	//Nobody registered for the key: OnMutation returns before it builds a payload or looks an id up.  This is every mutation in
	//the process that has no subscriber, so it has to be free of side effects rather than merely quiet.
	TEST_F( SubscriptionsTests, NoSubscriberIsNotAnError ){
		auto listener = Listener( "unrelated" );
		listen( listener, 71, fields({"id"}), "providers" );
		EXPECT_NO_THROW( Subscriptions::OnMutation(mutated(), jobject{}) );
		EXPECT_TRUE( listener->Changes.empty() );
	}

	//review3 #53: a subscription's own arguments are a predicate, and the generic fan-out never evaluated one - only the log path
	//did.  So `resourcesCreated(schemaName:"opc")` was delivered every createResource and the args the parser keeps were
	//projection-only decoration.  The args here are on the subscription; the values they are tested against come from the
	//mutation (`{id:1, name:"x", extra:2}`).
	TEST_F( SubscriptionsTests, AnArgIsAPredicateNotDecoration ){
		auto match = Listener( "match" );
		auto miss = Listener( "miss" );
		listen( match, 81, fields({"id"}, jobject{{"name","x"}}) );
		listen( miss, 82, fields({"id"}, jobject{{"name","nomatch"}}) );

		Subscriptions::OnMutation( mutated(), jobject{} );
		EXPECT_EQ( match->Changes.size(), 1u );
		EXPECT_TRUE( miss->Changes.empty() ) << "the subscriber asked for a subset and got everything";
	}
	//the operator form, and both directions of it - a filter is not just equality.
	TEST_F( SubscriptionsTests, OperatorFiltersAreEvaluatedToo ){
		auto low = Listener( "low" );
		auto high = Listener( "high" );
		listen( low, 83, fields({"id"}, Parser::ParseArgs("{id:{lte:5}}")) );
		listen( high, 84, fields({"id"}, Parser::ParseArgs("{id:{gte:5}}")) );

		Subscriptions::OnMutation( mutated(), jobject{} );//id is 1.
		EXPECT_EQ( low->Changes.size(), 1u );
		EXPECT_TRUE( high->Changes.empty() );
	}
	//a column the mutation says nothing about is unknowable, not a mismatch:  dropping the notification there would be a worse
	//bug than the one this fixes, because the payload only ever carries the mutation's own args plus its result.
	TEST_F( SubscriptionsTests, AFilterOnAColumnTheMutationDoesNotMentionStillDelivers ){
		auto listener = Listener( "unknowable" );
		listen( listener, 85, fields({"id"}, jobject{{"absent","y"}}) );
		Subscriptions::OnMutation( mutated(), jobject{} );
		EXPECT_EQ( listener->Changes.size(), 1u );
	}
	//and paging keys are not predicates - `limit` in the args must not be compared against anything.
	TEST_F( SubscriptionsTests, PagingArgsAreNotPredicates ){
		auto listener = Listener( "paging" );
		listen( listener, 86, fields({"id"}, Parser::ParseArgs("{limit:10, offset:5}")) );
		Subscriptions::OnMutation( mutated(), jobject{} );
		EXPECT_EQ( listener->Changes.size(), 1u );
	}

	//review3 #54: QL::Combine keeps the *first* argument's value on a scalar collision, and the args were first - so a
	//client-supplied `id` masked the one the database assigned and every subscriber was told the client's.  On the AppServer
	//AccessListener would have cached it (`userCreated` -> Authorizer().CreateUser(pk)).  The result is the base now.
	TEST_F( SubscriptionsTests, TheResultWinsOverAClientSuppliedArg ){
		auto listener = Listener( "idClash" );
		listen( listener, 91, fields({"id","name"}) );

		Subscriptions::OnMutation( mutated("createStatus", R"({id:99, name:"x"})"), jobject{{"id",7},{"rowCount",1}} );

		ASSERT_EQ( listener->Changes.size(), 1u );
		EXPECT_EQ( listener->Payload(0).at("id").to_number<uint>(), 7u ) << "the client's id masked the database's";
		EXPECT_EQ( listener->Payload(0).at("name").as_string(), "x" );//an arg the result says nothing about still fills in.
	}
	//and the ordinary case, which is every mutation in the tree:  no collision, so the args are the whole payload.
	TEST_F( SubscriptionsTests, ArgsStillFillInWhatTheResultDoesNotCarry ){
		auto listener = Listener( "noClash" );
		listen( listener, 92, fields({"id","name"}) );
		Subscriptions::OnMutation( mutated(), jobject{{"rowCount",1}} );
		ASSERT_EQ( listener->Changes.size(), 1u );
		EXPECT_EQ( listener->Payload(0).at("id").to_number<uint>(), 1u );
		EXPECT_EQ( listener->Payload(0).at("name").as_string(), "x" );
	}

	//review3 #55: the swallow above stays silent - the catches at LocalSubscriptions.cpp were left empty at the author's
	//direction, so there is nothing to assert about the log here.  What is pinned is the part that did not change and is what
	//makes the silence survivable: the throw costs that listener its notification and nobody else's.  See
	//AThrowingListenerDoesNotCostTheNextOneItsNotification above;  the access half of #55 is
	//SubscriptionTests.AnIdLessNotificationDoesNotThrowOutOfTheListener, which removes the only in-tree source of these throws.

	//review3 #56: `IListener::Ids` was a public mutable set that nothing in ql ever wrote, and its one reader - AccessListener::
	//Shutdown - passed it straight back to IQL::Unsubscribe.  Always empty, which is what made the shutdown path look broken;  it
	//is also, since #30, exactly right.  The member is gone and the call says `{}` where the reader can see it, so what is pinned
	//here is the contract that makes that safe: the default IQL::Unsubscribe with no ids drops every subscription the listener
	//holds, across every key, and nobody else's.
	TEST_F( SubscriptionsTests, UnsubscribeWithNoIdsDropsAllOfThatListenersSubscriptions ){
		auto ql = ms<NullQL>();//the default IQL::Unsubscribe - no override.
		auto listener = Listener( "shutdown" );
		auto other = Listener( "other" );
		listen( listener, 111, fields({"id"}) );
		listen( listener, 112, fields({"id"}), "", EMutationQL::Update );//a second key - the sweep is over the whole map.
		listen( other, 113, fields({"id"}) );

		ql->Unsubscribe( listener, {} );

		Subscriptions::OnMutation( mutated(), jobject{} );
		Subscriptions::OnMutation( mutated("updateStatus"), jobject{} );
		EXPECT_TRUE( listener->Changes.empty() );
		EXPECT_EQ( other->Changes.size(), 1u );//still subscribed - a shutdown is one listener's, not the registry's.
	}
}