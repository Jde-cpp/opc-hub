//325-logs-resource review #8:  SubscribeLog::Write, on whatever thread logged, only matches and queues;  a thread of its own runs
//the subscriber's gate and writes the line to the session.  A `logs` subscription over the socket still gets its line.
#include "helpers.h"
#include <jde/fwk/co/BlockAwait.h>

#define let const auto
namespace Jde::App::Server::Tests{
	TEST( LogSubscriptionTests, ASubscriberGetsTheLineFromTheDeliveryThread ){
		constexpr sv marker{ "LogSubscriptionTests delivery marker" };
		auto client = Connect();
		RegisterInstance( *client, "Tests.LogSubscription", "subscriber", "logsub-host", 0 );//adopts a session - a subscription needs one.
		let requestId = Send( *client, [&](auto& m){ m.mutable_subscription()->set_text( Ƒ(R"(subscription LogCreated{{ logCreated(level:"Information", text:"{}"){{ id }} }})", marker) ); } );
		let ack = client->WaitFor( [=](const FromServerMessage& r){ return r.request_id()==requestId && (r.value_case()==FromServerMessage::kSubscriptionAck || r.value_case()==FromServerMessage::kException); } );
		ASSERT_TRUE( ack );
		ASSERT_EQ( ack->value_case(), FromServerMessage::kSubscriptionAck ) << ack->exception().what();

		INFOT( ELogTags::Test, "LogSubscriptionTests delivery marker" );//the filter's text:  the template, as Write tests it.
		let line = client->WaitFor( [=](const FromServerMessage& r){ return r.value_case()==FromServerMessage::kTraces && r.traces().values_size() && r.traces().values(0).id()==requestId; } );
		EXPECT_TRUE( line ) << "no line for the subscription";
		BlockAwait( client->Close(true, SRCE_CUR) );
	}
	//reviews-issue/log-pass2.md #5:  an args filter passes a line if any of its arguments matches, as the archive's does, and
	//no line without arguments.  ANDing passed every argument-less line in the process and dropped the one asked for.
	TEST( LogSubscriptionTests, AnArgsFilterMatchesAnyArgument ){
		constexpr sv wanted{ "LogSubscriptionTests timeout" };
		auto client = Connect();
		RegisterInstance( *client, "Tests.LogSubscription", "args", "logsub-host", 0 );
		let requestId = Send( *client, [&](auto& m){ m.mutable_subscription()->set_text( Ƒ(R"(subscription LogCreated{{ logCreated(level:"Information", args:"{}"){{ id }} }})", wanted) ); } );
		let ack = client->WaitFor( [=](const FromServerMessage& r){ return r.request_id()==requestId && (r.value_case()==FromServerMessage::kSubscriptionAck || r.value_case()==FromServerMessage::kException); } );
		ASSERT_TRUE( ack );
		ASSERT_EQ( ack->value_case(), FromServerMessage::kSubscriptionAck ) << ack->exception().what();

		INFOT( ELogTags::Test, "LogSubscriptionTests args none" );
		INFOT( ELogTags::Test, "LogSubscriptionTests args {}", "other" );
		INFOT( ELogTags::Test, "LogSubscriptionTests args {} {}", string{wanted}, "conn 3" );
		let line = client->WaitFor( [=](const FromServerMessage& r){ return r.value_case()==FromServerMessage::kTraces && r.traces().values_size() && r.traces().values(0).id()==requestId; } );
		ASSERT_TRUE( line ) << "no line for the subscription";
		let& first = line->traces().values( 0 );
		ASSERT_EQ( first.args_size(), 2 ) << "the first line delivered isn't the one with the argument";
		EXPECT_EQ( first.args(0), wanted );
		EXPECT_EQ( first.args(1), "conn 3" );
		BlockAwait( client->Close(true, SRCE_CUR) );
	}
}