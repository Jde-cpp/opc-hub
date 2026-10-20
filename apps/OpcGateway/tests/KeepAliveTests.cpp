#include <jde/fwk/utils/Stopwatch.h>
#include "utils/ClientFixture.h"
#include "utils/Subscribing.h"
#include "../src/UAClient.h"
#include "../src/GatewayAppClient.h"

#define let const auto

namespace Jde::Opc::Gateway{ extern Duration _pingInterval; extern Duration _ttl; }//async/AsyncRequest.cpp - read as each loop exits, so a cell may shorten them.
namespace Jde::Opc::Gateway::Tests{
	//The idle ttl against the keep-alive (reviews/m3-closing.md #34):  each idle ping reads the server state, and that read is not user
	//traffic - _lastRequest is stamped when a request is submitted, and never for the ping or the keep-alive (reviews/opc-client-keep-alive.md #5).
	const string KeepAliveSlug{ "opcTestsKeepAlive" };
	class KeepAliveTests : public ::testing::Test{
	protected:
		Ω SetUpTestSuite()->void{
			_connection = GetConnection( KeepAliveSlug, OpcUrl(), "" );
		}
		Ω TearDownTestSuite()->void{
			PurgeServerCnnctn( KeepAliveSlug );
			_connection = nullopt;
		}
		α SetUp()ι->void override{ _ping = _pingInterval; _savedTtl = _ttl; _pingInterval = 500ms; _ttl = 3s; }
		α TearDown()ι->void override{
			_pingInterval = _ping; _ttl = _savedTtl;
			if( auto client = UAClient::Find(KeepAliveSlug, SessionCredential()); client )
				UAClient::RemoveClient( move(client) );
		}
		Ω Connect()ε->sp<UAClient>;
		//How long `client` stays registered, from now - nullopt past `limit`.
		Ω Retires( const sp<UAClient>& client, steady_clock::duration limit )ι->optional<steady_clock::duration>;
		static optional<ServerCnnctn> _connection;
		Duration _ping, _savedTtl;
	};
	optional<ServerCnnctn> KeepAliveTests::_connection;

	α KeepAliveTests::Connect()ε->sp<UAClient>{
		auto y = TryConnect( KeepAliveSlug, SessionCredential() );
		if( !y )
			y.error()->Throw();
		return *y;
	}
	α KeepAliveTests::Retires( const sp<UAClient>& client, steady_clock::duration limit )ι->optional<steady_clock::duration>{
		let start = steady_clock::now();
		return WaitUntil( [&]{ return UAClient::Find(KeepAliveSlug, SessionCredential())!=client; }, duration_cast<Duration>(limit) ) ? optional{ steady_clock::now()-start } : nullopt;
	}

	//Six keep-alives go out in the 3 s ttl.  Were they traffic, the client would never retire.
	TEST_F( KeepAliveTests, KeepAlivesLeaveTheTtlAlone ){
		let client = Connect();
		ASSERT_TRUE( client );
		let retired = Retires( client, 10s );
		ASSERT_TRUE( retired ) << "an idle client with keep-alives going out never retired";
		EXPECT_GE( *retired, 2500ms ) << "retired before its ttl";
	}

	//SubscriptionRequestId stays queued for as long as the subscription is processed, so its submission says nothing about idleness.
	//The unsubscribe's own delete request is what restarts the idle clock - were it not stamped, a client whose subscription outlived
	//the ttl would retire the moment its last item went.
	TEST_F( KeepAliveTests, TheIdleClockStartsAtTheUnsubscribe ){
		auto client = Connect();
		ASSERT_TRUE( client );
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( SubscribeOn(client, listener, {Intensity}) );
		std::this_thread::sleep_for( 4s );//past the ttl, subscribed.
		ASSERT_EQ( UAClient::Find(KeepAliveSlug, SessionCredential()), client ) << "a subscribed client retired";
		UAClient::Unsubscribe( listener );
		let retired = Retires( client, 10s );
		ASSERT_TRUE( retired ) << "the client never retired after its last item went";
		EXPECT_GE( *retired, 2500ms ) << "retired on the subscription's age, not the unsubscribe's";
	}
}
