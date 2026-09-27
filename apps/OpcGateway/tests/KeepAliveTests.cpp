#include <jde/fwk/utils/Stopwatch.h>
#include "utils/helpers.h"
#include "../src/UAClient.h"
#include "../src/GatewayAppClient.h"
#include "../src/async/Subscriptions.h"

#define let const auto

namespace Jde::Opc::Gateway{ extern Duration _pingInterval; extern Duration _ttl; }//async/AsyncRequest.cpp - read as each loop exits, so a cell may shorten them.
namespace Jde::Opc::Gateway::Tests{
	//The idle ttl against the keep-alive (reviews/m3-closing.md #34):  each idle ping reads the server state, and that read is not user
	//traffic - _lastRequest is stamped when a request is submitted, and never for the ping or the keep-alive (reviews/opc-client-keep-alive.md #5).
	const string KeepAliveSlug{ "opcTestsKeepAlive" };
	class KeepAliveTests : public ::testing::Test{
	protected:
		Ω SetUpTestSuite()->void{
			let url = Settings::FindSV( "/opc/url" ).value_or( "opc.tcp://127.0.0.1:4840" );
			_connection = GetConnection( KeepAliveSlug, string{url}, "" );
			_jwt = BlockAwait<Web::Client::ClientSocketAwait<Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		}
		Ω TearDownTestSuite()->void{
			if( _connection ){
				PurgeServerCnnctn( _connection->Id );
				_connection = nullopt;
			}
		}
		α SetUp()ι->void override{ _ping = _pingInterval; _savedTtl = _ttl; _pingInterval = 500ms; _ttl = 3s; }
		α TearDown()ι->void override{
			_pingInterval = _ping; _ttl = _savedTtl;
			if( auto client = UAClient::Find(KeepAliveSlug, Cred()); client )
				UAClient::RemoveClient( move(client) );
		}
		Ω Cred()ι->Credential{ return Credential{ _jwt->Payload() }; }
		Ω Connect()ε->sp<UAClient>;
		//How long `client` stays registered, from now - nullopt past `limit`.
		Ω Retires( const sp<UAClient>& client, steady_clock::duration limit )ι->optional<steady_clock::duration>;
		static optional<ServerCnnctn> _connection;
		static optional<Web::Jwt> _jwt;
		Duration _ping, _savedTtl;
	};
	optional<ServerCnnctn> KeepAliveTests::_connection;
	optional<Web::Jwt> KeepAliveTests::_jwt;

	Ω connect( Credential cred, sp<UAClient>* y, up<Exception>* exception, atomic_flag* done )ι->ConnectAwait::Task{//not a capturing lambda:  its captures die with the temporary at the first suspension.
		try{
			*y = co_await UAClient::GetClient( KeepAliveSlug, move(cred) );
		}
		catch( Exception& e ){
			*exception = e.Move();
		}
		done->test_and_set();
		done->notify_all();
	}
	α KeepAliveTests::Connect()ε->sp<UAClient>{
		sp<UAClient> y; up<Exception> exception; atomic_flag done;
		connect( Cred(), &y, &exception, &done );
		done.wait( false );
		if( exception )
			exception->Throw();
		return y;
	}
	α KeepAliveTests::Retires( const sp<UAClient>& client, steady_clock::duration limit )ι->optional<steady_clock::duration>{
		let start = steady_clock::now();
		while( UAClient::Find(KeepAliveSlug, Cred())==client ){
			if( steady_clock::now()-start>limit )
				return nullopt;
			std::this_thread::sleep_for( 10ms );
		}
		return steady_clock::now()-start;
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
	struct PushCount final : IDataChange{
		α SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value& )ι->void override{ ++Pushes; }
		α to_string()Ι->string override{ return "KeepAliveTests.PushCount"; }
		atomic<uint> Pushes;
	};
	TEST_F( KeepAliveTests, TheIdleClockStartsAtTheUnsubscribe ){
		auto client = Connect();
		ASSERT_TRUE( client );
		auto listener = ms<PushCount>();
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{client}) );
		ASSERT_NO_THROW( BlockTAwait<FromServer::SubscriptionAck>(DataChangeAwait{{NodeId{4, 6017}}, listener, client}) );
		std::this_thread::sleep_for( 4s );//past the ttl, subscribed.
		ASSERT_EQ( UAClient::Find(KeepAliveSlug, Cred()), client ) << "a subscribed client retired";
		UAClient::Unsubscribe( listener );
		let retired = Retires( client, 10s );
		ASSERT_TRUE( retired ) << "the client never retired after its last item went";
		EXPECT_GE( *retired, 2500ms ) << "retired on the subscription's age, not the unsubscribe's";
	}
}
