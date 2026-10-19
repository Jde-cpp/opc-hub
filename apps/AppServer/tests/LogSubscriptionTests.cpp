//325-logs-resource review #8:  SubscribeLog::Write, on whatever thread logged, only matches and queues;  a thread of its own runs
//the subscriber's gate and writes the line to the session.  A `logs` subscription over the socket still gets its line.
#include "helpers.h"

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
		BlockVoidAwait( client->Close(true, SRCE_CUR) );
	}
}