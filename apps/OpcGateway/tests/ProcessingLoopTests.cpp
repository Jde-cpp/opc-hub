#include <thread>
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/log/MemoryLog.h>
#include "utils/helpers.h"
#include "utils/ITest.h"
#include "utils/Subscribing.h"
#include "../src/UAClient.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	//#526's acceptance:  the processing loop calls run_iterate for an event - a response, a Publish, the loop's own housekeeping
	//timer - and not on a timer of its own.  Counted from the loop's trace line, which the memory logger takes at Trace for the
	//loop's tag alone.
	class ProcessingLoopTests : public ITest{
	protected:
		α SetUp()ι->void override{
			if( !Logging::FindLogger<Logging::MemoryLog>() )
				Logging::AddLogger( mu<Logging::MemoryLog>() );
			auto& memory = Logging::GetLogger<Logging::MemoryLog>();
			_memoryDefault = memory.DefaultLevel();
			memory.SetDefaultLevel( ELogLevel::Information );
			memory.SetLevel( (ELogTags)EOpcLogTags::ProcessingLoop, ELogLevel::Trace );
		}
		α TearDown()ι->void override{
			auto& memory = Logging::GetLogger<Logging::MemoryLog>();
			memory.ClearLevel( (ELogTags)EOpcLogTags::ProcessingLoop );
			memory.SetDefaultLevel( _memoryDefault );
		}
		//The suite client's run_iterate calls logged so far.
		Ω Iterations()ι->uint{
			let prefix = Ƒ( "[{:x}]run_iterate: requestCount", _client->Handle() );
			return Logging::Find( [&](const Logging::Entry& e){ return e.Message().starts_with(prefix); } ).size();
		}
		Ω Clear()ι->void{ Logging::GetLogger<Logging::MemoryLog>().Clear(); }
		ELogLevel _memoryDefault;
	};

	//A subscribed client with nothing else outstanding:  the server's keep-alive Publish responses - one a second at the suite's
	//100 ms publishing interval and open62541's ten-count - and the client's own 1 s housekeeping are the events of three idle
	//seconds.  Polling the subscription's interval made it about thirty.
	TEST_F( ProcessingLoopTests, AnIdleSubscriptionIsPumpedForItsEventsAlone ){
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeCounting(_client, listener, {Intensity}) );
		absl::Cleanup unsubscribe = [&]{
			UAClient::Unsubscribe( listener );
			std::this_thread::sleep_for( 1500ms );//the delete-monitoring pass is 1 s after the last item goes.
		};
		std::this_thread::sleep_for( 500ms );//the create's own traffic settles.
		Clear();
		std::this_thread::sleep_for( 3s );
		let iterations = Iterations();
		INFO( "{} run_iterate calls in 3 s, subscribed and idle", iterations );
		EXPECT_LE( iterations, 10u );
		EXPECT_GE( iterations, 1u ) << "the housekeeping timer alone should have woken the loop";
	}

	//A read's round trip:  the loop runs for the submission and for the response, a handful of calls where it polled a thousand
	//a second for as long as the server took.
	TEST_F( ProcessingLoopTests, ARoundTripIsAHandfulOfIterations ){
		ASSERT_NO_THROW( ReadValue(_client, Intensity) );//warm:  the first read may reconnect.
		std::this_thread::sleep_for( 100ms );
		Clear();
		ASSERT_NO_THROW( ReadValue(_client, Intensity) );
		std::this_thread::sleep_for( 100ms );
		let iterations = Iterations();
		INFO( "{} run_iterate calls for a read", iterations );
		EXPECT_LE( iterations, 6u );
		EXPECT_GE( iterations, 1u );
	}
}