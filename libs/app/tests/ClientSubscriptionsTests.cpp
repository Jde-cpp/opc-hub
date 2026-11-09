//The registry app-review2 #15 turns on: what the process asked to be subscribed to, as opposed to what the current
//socket happens to be carrying.  A close drops the second - server-side subscriptions really do die with the socket - so
//the first has to outlive it or a reconnect has nothing to put back.  Since M1 the second lives on the session that owns
//it (AppClientSocketSession::ListenRemote/ClearSubscriptions), which is what the session-scoped cases below pin.
//Replay() itself needs a live session, so it is not driven here; what is, is the bookkeeping it reads.
#include <gtest/gtest.h>
#include <jde/fwk/process/execution.h>
#include <jde/ql/types/Subscription.h>
#include <jde/web/client/ClientSsl.h>
#include <jde/app/client/clientSubscriptions.h>
#include <jde/app/client/AppClientSocketSession.h>
#include "helpers.h"//table(): TableQL has no default ctor.
#include <thread>

#define let const auto

namespace Jde::App::Tests{

	struct Listener final : QL::IListener{
		Listener( str name )ι:IListener{name}{}
		α OnChange( const jvalue&, QL::SubscriptionId )ε->void override{ ++Changes; }
		atomic<uint> Changes{};
	};

	struct ClientSubscriptionsTests : ::testing::Test{
	protected:
		//_requests is a file-global, so each test starts by emptying it of whatever the last one left.
		α SetUp()->void override{ Forget(); _a = ms<Listener>("a"); _b = ms<Listener>("b"); }
		α TearDown()->void override{ Forget(); }
		α Forget()ι->void{
			for( let& r : Client::Subscriptions::Remembered() )
				Client::Subscriptions::Forget( r.Listener, {} );
		}
		α Remember( sp<Listener> listener, sv query, flat_set<QL::SubscriptionId> ids )ι->void{
			Client::Subscriptions::Remember( nullptr, string{query}, jobject{{"id", 1}}, move(listener), ids );
		}
		α Queries()ι->vector<string>{
			vector<string> y;
			for( let& r : Client::Subscriptions::Remembered() )
				y.push_back( r.Query );
			return y;
		}
		//A socket-less session is enough for the subscription map: it needs an executor and an empty ssl context to construct.
		α NewSession()ι->sp<Client::AppClientSocketSession>{ return ms<Client::AppClientSocketSession>( Executor(), optional<ssl::context>{}, nullptr, nullptr ); }
		Ω subscription( QL::SubscriptionId id )ε->QL::Subscription{
			QL::Subscription sub{ "tests", QL::EMutationQL::Create, table("tests") };
			sub.Id = id;
			return sub;
		}
		sp<Listener> _a, _b;
	};

	TEST_F( ClientSubscriptionsTests, RemembersPerRequest ){
		Remember( _a, "subscription A{ a{id} }", {1} );
		Remember( _b, "subscription B{ b{id} }", {2} );
		EXPECT_EQ( Queries().size(), 2u );
	}

	//The reason a replay is safe to run repeatedly: it re-issues the same query, gets fresh ids, and must update the entry
	//rather than add a second one - which would then be replayed twice, and deliver every event twice.
	TEST_F( ClientSubscriptionsTests, ReplayingUpdatesIdsInPlace ){
		Remember( _a, "subscription A{ a{id} }", {1} );
		Remember( _a, "subscription A{ a{id} }", {99} );//same request, new session's ids.
		let remembered = Client::Subscriptions::Remembered();
		ASSERT_EQ( remembered.size(), 1u ) << "a replayed request was recorded twice";
		EXPECT_EQ( remembered.front().Ids, (flat_set<QL::SubscriptionId>{99}) );
	}

	//Same query text, different listener, is a different subscription - both have to come back.
	TEST_F( ClientSubscriptionsTests, SameQueryDifferentListenersAreDistinct ){
		Remember( _a, "subscription A{ a{id} }", {1} );
		Remember( _b, "subscription A{ a{id} }", {2} );
		EXPECT_EQ( Client::Subscriptions::Remembered().size(), 2u );
	}

	//#15's core invariant: a close is about the live socket's ids, not about what was asked for.
	TEST_F( ClientSubscriptionsTests, ClearDoesNotForgetRequests ){
		Remember( _a, "subscription A{ a{id} }", {1} );
		NewSession()->ClearSubscriptions();//what CloseTasks does on every socket close.
		EXPECT_EQ( Client::Subscriptions::Remembered().size(), 1u ) << "the close dropped the request, so a reconnect has nothing to replay";
	}

	TEST_F( ClientSubscriptionsTests, ASecondSessionsCloseLeavesTheLiveSessionsSubscriptions ){
		auto live = NewSession();
		live->ListenRemote( _a, subscription(4242) );
		NewSession()->ClearSubscriptions();
		live->OnSubscription( jobject{{"x",1}}, 4242 );
		EXPECT_EQ( _a->Changes.load(), 1u ) << "another session's close emptied this one's subscription map";
	}

	//...and the same isolation the other way: two sessions minting the same id are two different subscriptions (C6 - client and
	//server mint into one id space from independent counters).
	TEST_F( ClientSubscriptionsTests, SessionsDoNotShareAnIdSpace ){
		auto first = NewSession(); auto second = NewSession();
		first->ListenRemote( _a, subscription(7) );
		second->ListenRemote( _b, subscription(7) );
		first->OnSubscription( jobject{{"x",1}}, 7 );
		EXPECT_EQ( _a->Changes.load(), 1u );
		EXPECT_EQ( _b->Changes.load(), 0u ) << "a push on one session reached the other session's listener";
	}

	TEST_F( ClientSubscriptionsTests, ForgetByListenerDropsAll ){
		Remember( _a, "subscription A{ a{id} }", {1} );
		Remember( _a, "subscription B{ b{id} }", {2} );
		Remember( _b, "subscription C{ c{id} }", {3} );
		Client::Subscriptions::Forget( _a, {} );
		EXPECT_EQ( Queries(), (vector<string>{"subscription C{ c{id} }"}) );
	}

	//An unsubscribe must take the request with it, or the next reconnect puts back what was just cancelled.  A request
	//only goes when every id it produced is gone - one query can ack into several.
	TEST_F( ClientSubscriptionsTests, ForgetByIdNeedsEveryIdOfTheRequest ){
		Remember( _a, "subscription A{ a{id} }", {1,2} );
		Client::Subscriptions::Forget( _a, {1} );
		ASSERT_EQ( Client::Subscriptions::Remembered().size(), 1u ) << "dropped while one of its subscriptions was still live";
		EXPECT_EQ( Client::Subscriptions::Remembered().front().Ids, (flat_set<QL::SubscriptionId>{2}) );
		Client::Subscriptions::Forget( _a, {2} );
		EXPECT_TRUE( Client::Subscriptions::Remembered().empty() );
	}

	//M8: OnSubscription (and OnTraces, since removed) invoked their listeners while holding the shared_lock, so a listener that unsubscribes
	//in response to an event re-entered StopListenRemote - which takes the same mutex exclusively - and hung.
	//The re-entrant call runs on another thread, not this one, for two reasons: a same-thread relock of a shared_mutex is
	//UB rather than a guaranteed hang, and this way a regression fails on the deadline instead of wedging the suite.
	//T3: what is asserted is SawUnsubscribe, taken here inside the callback, not Unsubscribed read after the call returns.
	//Under the regression the detached thread is blocked on the lock for the whole of the spin below and is released the
	//instant OnSubscription returns - the same instant the assert would run - so reading it out there is a race the test
	//can lose, and losing it reports green.  Read from inside, the deadline is the answer and nothing races it.
	struct UnsubscribingListener final : QL::IListener{
		UnsubscribingListener()ι:IListener{"unsubscribes-on-change"}{}
		α OnChange( const jvalue&, QL::SubscriptionId )ε->void override{
			std::thread{ [this]{
				if( auto self = Self.lock(); self )//weak, not shared: a listener holding itself is a cycle, and LSan says so.
					Session->StopListenRemote( self, {} );
				Unsubscribed = true;//last touch of `this` - the test waits for it before letting the listener go.
			} }.detach();
			for( uint i=0; i<200 && !Unsubscribed; ++i )
				std::this_thread::sleep_for( 10ms );
			SawUnsubscribe = Unsubscribed.load();
		}
		wp<QL::IListener> Self;//what StopListenRemote matches on.
		sp<Client::AppClientSocketSession> Session;//the session carrying the id - the map is per socket since M1.
		atomic<bool> Unsubscribed{};
		atomic<bool> SawUnsubscribe{};//Unsubscribed as of the end of the callback, which is what the invariant is about.
	};

	TEST_F( ClientSubscriptionsTests, AListenerMayUnsubscribeFromItsOwnCallback ){
		auto session = NewSession();
		auto listener = ms<UnsubscribingListener>();
		listener->Self = listener;
		listener->Session = session;
		session->ListenRemote( listener, subscription(4242) );
		session->OnSubscription( jobject{{"x",1}}, 4242 );
		EXPECT_TRUE( listener->SawUnsubscribe.load() ) << "the callback ran with _subsMutex held, so the unsubscribe could not take it";
		//T3: and the thread's store is its last touch of the listener, so it has to land while the listener is still held.
		//On the passing path it already has - the spin only ends when it does.  On the failing one the thread is unblocked
		//by the return above and finishes here, instead of writing through a listener the end of this test has freed.
		for( let expiration = steady_clock::now()+std::chrono::seconds{5}; !listener->Unsubscribed && steady_clock::now()<expiration; )
			std::this_thread::sleep_for( std::chrono::milliseconds{1} );
	}

	//L5: a request the server refuses on replay was re-issued on every reconnect forever, one WARN at a time.  The header's "must
	//not be replayed forever" only ever covered the *initial* request.
	TEST_F( ClientSubscriptionsTests, ARefusedReplayIsDroppedEventually ){
		Remember( _a, "subscription A{ a{id} }", {1} );
		let request = [&]{ return Client::Subscriptions::Remembered().front(); };
		EXPECT_FALSE( Client::Subscriptions::NoteReplayFailure(request()) ) << "dropped on the first refusal - a session that died mid-replay refuses nothing";
		EXPECT_FALSE( Client::Subscriptions::NoteReplayFailure(request()) );
		EXPECT_TRUE( Client::Subscriptions::NoteReplayFailure(request()) ) << "still being replayed after three refusals";
		EXPECT_TRUE( Client::Subscriptions::Remembered().empty() );
	}

	//...and the count is consecutive, not cumulative: Remember runs on the ack, so a replay the server accepts starts it over.
	TEST_F( ClientSubscriptionsTests, AnAcceptedReplayResetsTheFailureCount ){
		Remember( _a, "subscription A{ a{id} }", {1} );
		let request = [&]{ return Client::Subscriptions::Remembered().front(); };
		Client::Subscriptions::NoteReplayFailure( request() );
		Client::Subscriptions::NoteReplayFailure( request() );
		Remember( _a, "subscription A{ a{id} }", {99} );//the ack of a replay that worked.
		EXPECT_EQ( Client::Subscriptions::Remembered().front().Failures, 0u );
		EXPECT_FALSE( Client::Subscriptions::NoteReplayFailure(request()) );
		EXPECT_FALSE( Client::Subscriptions::NoteReplayFailure(request()) ) << "the count carried over from before the accepted replay";
		EXPECT_FALSE( Client::Subscriptions::Remembered().empty() );
	}

	//Forgetting one listener's ids must not touch another's request that happens to share an id value.
	TEST_F( ClientSubscriptionsTests, ForgetIsScopedToItsListener ){
		Remember( _a, "subscription A{ a{id} }", {1} );
		Remember( _b, "subscription B{ b{id} }", {1} );
		Client::Subscriptions::Forget( _a, {1} );
		EXPECT_EQ( Queries(), (vector<string>{"subscription B{ b{id} }"}) );
	}
}