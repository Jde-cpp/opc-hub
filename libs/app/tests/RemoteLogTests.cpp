//RemoteLog against a mock IAppClient - no socket, no app server.  Until this file the class had none: it is only built
//when /logging/remote is configured, which no config in the tree does, so app-review2 #12 and #14 were fixed blind.
//IAppClient::Connected/Write are virtual for exactly this reason; both answer out of a private _session that cannot
//exist without a connection.
#include <gtest/gtest.h>
#include <thread>
#include <jde/fwk/process/process.h>
#include <jde/ql/types/RequestQL.h>
#include <jde/app/client/IAppClient.h>
#include <jde/app/client/RemoteLog.h>

#define let const auto

namespace Jde::App::Tests{
	struct MockAppClient final : Client::IAppClient{
		α Connected()Ι->bool override{ return _connected; }
		α Write( vector<Logging::Entry>&& entries )ι->bool override{
			if( !_connected )
				return false;//as the real one does with no session.
			lg _{ _mutex };
			_writtenOn = std::this_thread::get_id();
			_batchSizes.push_back( entries.size() );//#14: one call per Transmission - the sizes are how a test sees the chunking.
			for( auto& e : entries )
				Received.push_back( move(e) );
			return true;
		}
		α ClientQuery( QL::RequestQL&&, Jde::UserPK, SL )ε->up<TAwait<jvalue>> override{ throw Exception{"RemoteLog never queries."}; }

		α SetConnected( bool x )ι->void{ _connected = x; }
		α Texts()ι->vector<string>{ lg _{_mutex}; vector<string> y; for( let& e : Received ) y.push_back( string{e.Text()} ); return y; }
		α Count()ι->uint{ lg _{_mutex}; return Received.size(); }
		//Which thread the batch was written on.  asio::post never runs a handler inline, so this is what separates "wrote
		//it here" from "handed it to the executor" - a count alone only says the executor happened to be quick.
		α WrittenOn()ι->std::thread::id{ lg _{_mutex}; return _writtenOn; }
		α BatchSizes()ι->vector<uint>{ lg _{_mutex}; return _batchSizes; }
		vector<Logging::Entry> Received;
	private:
		atomic<bool> _connected{};
		mutex _mutex;
		std::thread::id _writtenOn;
		vector<uint> _batchSizes;
	};

	struct RemoteLogTests : ::testing::Test{
	protected:
		//No hand-written Process::RemoveShutdown anywhere in this file - that was M4's workaround, and since M4 the
		//destructors do it.  Which makes this suite the check: every RemoteLog and mock client built here is destroyed
		//while the process runs on, so a missing unregister leaves Process holding freed pointers and the shutdown at
		//exit calls Shutdown() through them - a heap-use-after-free the debug build's ASan reports.
		α SetUp()->void override{ _client = ms<MockAppClient>(); }
		α TearDown()->void override{ _client = nullptr; }
		//delay PT24H unless a test says otherwise: the timer must not fire, so what reaches the mock is what the test made
		//happen rather than a background round.
		α Make( jobject settings={} )ε->up<Client::RemoteLog>{
			if( !settings.contains("delay") )
				settings["delay"] = "PT24H";
			return mu<Client::RemoteLog>( settings, _client );
		}
		α Entry( sv text )ι->Logging::Entry{ return { SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{text} }; }
		//Shutdown(false) is the only way to make a RemoteLog send without waiting out its delay, and since #14 it writes on
		//this thread - so everything it sent is in the mock by the time this returns, with no sleep.
		α Flush( Client::RemoteLog& log )ι->void{ log.Shutdown( false, SRCE_CUR ); }
		sp<MockAppClient> _client;
	};

	//#14: entries went in whenever the process logged and only came out when the app server was reachable, so with it down
	//nothing bounded the buffer - overnight it grew until OOM.  Cap of 10, 15 entries written: the oldest half goes when
	//the cap is first hit, so what survives is 6..15 rather than 1..10.
	TEST_F( RemoteLogTests, CapDropsTheOldest ){
		auto log = Make( {{"maxEntries", 10}} );
		for( uint i=1; i<=15; ++i )
			log->Write( Entry(Ƒ("entry {}", i)) );
		_client->SetConnected( true );//only now, so every write above was buffered rather than sent.
		Flush( *log );

		let texts = _client->Texts();
		ASSERT_EQ( texts.size(), 10u ) << "the cap did not hold";
		EXPECT_EQ( texts.front(), "entry 6" ) << "the newest are what explain the current state - the oldest are what go";
		EXPECT_EQ( texts.back(), "entry 15" );
	}

	//web-review3 #14: the whole backlog went out as a single Transmission, so after any app-server outage it exceeded the server's
	//websocket read_message_max - the read failed with message_too_big, the socket closed, the gateway reconnected after 5s and
	//resent an even larger backlog.  Remote logging could never re-establish itself after an outage.
	TEST_F( RemoteLogTests, ChunksTheBacklog ){
		auto log = Make( {{"maxBatch", 10}} );
		for( uint i=1; i<=25; ++i )//buffered: the client is not connected yet, exactly as during an outage.
			log->Write( Entry(Ƒ("entry {}", i)) );
		_client->SetConnected( true );
		Flush( *log );

		EXPECT_EQ( 25u, _client->Count() ) << "chunking must not cost entries";
		let batches = _client->BatchSizes();
		ASSERT_EQ( 3u, batches.size() ) << "25 entries at maxBatch=10 is 10+10+5, not one oversized frame";
		EXPECT_EQ( 10u, batches[0] );
		EXPECT_EQ( 10u, batches[1] );
		EXPECT_EQ( 5u, batches[2] );
		EXPECT_EQ( "entry 1", _client->Texts().front() ) << "and order is preserved across the batches";
		EXPECT_EQ( "entry 25", _client->Texts().back() );
	}

	//Under the cap nothing is dropped - the bound must not cost entries in the ordinary case.
	TEST_F( RemoteLogTests, UnderTheCapKeepsEverything ){
		auto log = Make( {{"maxEntries", 10}} );
		for( uint i=1; i<=10; ++i )
			log->Write( Entry(Ƒ("entry {}", i)) );
		_client->SetConnected( true );
		Flush( *log );
		EXPECT_EQ( _client->Count(), 10u );
	}

	//#14, the other half: Shutdown used to Post the batch and return.  At shutdown the executor is being torn down, so the
	//lambda could simply never run - the tail of the log went missing.  Asserted on the *thread*, not on a count: a count
	//passes whenever the executor happens to win the microsecond before the next line, which it usually does.
	TEST_F( RemoteLogTests, ShutdownSendsOnTheCallersThread ){
		auto log = Make();
		_client->SetConnected( true );
		log->Write( Entry("shutdown tail") );
		Flush( *log );
		ASSERT_EQ( _client->Count(), 1u );
		EXPECT_EQ( _client->Texts().front(), "shutdown tail" );
		EXPECT_EQ( _client->WrittenOn(), std::this_thread::get_id() ) << "the batch was handed to the executor rather than written before Shutdown returned";
	}

	//Disconnected, Send must not hand the batch anywhere.  The entries stay buffered for a reconnect that, at shutdown,
	//never comes - but they are not written to a client that cannot take them.
	TEST_F( RemoteLogTests, DisconnectedSendsNothing ){
		auto log = Make();
		log->Write( Entry("no session") );
		Flush( *log );
		EXPECT_EQ( _client->Count(), 0u );
	}

	//Write is the process's log path: an entry tagged with RemoteLog's own tags must not come back round through it.
	//T2/M8: the App entry is the half that was missing.  The guard used to be ExternalLogger|App, and App is the framework's
	//DefaultTag - so it silently dropped the startup lines, every INFOT(ELogTags::App,...) and every AppQL 401, which is most of
	//what a remote log is for.  Asserting only that ExternalLogger is dropped left the suite green whether the guard was narrowed
	//or widened; naming what must get *through* is what pins it.
	TEST_F( RemoteLogTests, OwnTagsAreNotForwarded ){
		auto log = Make();
		_client->SetConnected( true );
		Logging::Entry recursive{ SRCE_CUR, ELogLevel::Information, ELogTags::ExternalLogger, string{"recursive"} };
		log->Write( recursive );
		Logging::Entry standard{ SRCE_CUR, ELogLevel::Information, DefaultTag, string{"default tag"} };
		log->Write( standard );
		log->Write( Entry("ordinary") );
		Flush( *log );
		EXPECT_EQ( _client->Texts(), (vector<string>{"default tag", "ordinary"}) ) << "the guard dropped a DefaultTag entry, or let RemoteLog's own logging back in";
	}

	//A smoke test, and labelled as one because it was measured rather than assumed: it does NOT reproduce #12.  Reverting
	//the fix - the wait, and then the whole loop shape that made #12's window ten lines wide - still leaves this passing
	//under ASan across repeated runs.  The reason is that the destructor polls at 1ms granularity while the coroutine's
	//tail is microseconds, so the destructor essentially always loses that race and never gets to free the object first.
	//#12 is real (a preempted thread changes those odds) but far narrower than the finding reads.  What this does prove is
	//that the wait terminates and that 50 build/write/tear-down cycles are clean under ASan/LSan.
	TEST_F( RemoteLogTests, DestructorOutlastsTheTimerCoroutine ){
		_client->SetConnected( true );
		for( uint i=0; i<50; ++i ){
			auto log = Make( {{"delay", "PT0.001S"}} );
			log->Write( Entry(Ƒ("round {}", i)) );
			std::this_thread::sleep_for( std::chrono::microseconds{i%2 ? 500 : 1500} );//straddle the 1ms timer: sometimes mid-round, sometimes after it fired.
			log = nullptr;//~RemoteLog: unregisters, cancels, then waits for the coroutine.
		}
		SUCCEED() << "no hang and no sanitizer report across 50 teardowns";
	}
}
