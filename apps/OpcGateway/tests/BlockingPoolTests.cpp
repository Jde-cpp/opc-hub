#include "utils/ClientFixture.h"
#include "../src/async/BlockingPool.h"
#include "../src/async/TestConnectAwait.h"
#include "../src/ql/GatewayQL.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	//#353:  a client's build and connect - the slug's certificate issued, trustedCertDirs scanned, the host resolved and probed - ran
	//on the executor thread that awaited it, which also runs every client strand and every HTTP handler, so a connect to a host with
	//slow DNS or no route stalled them all.  Both connects go to the blocking pool now, and the proof is the issue's own:  with every
	//executor thread held, the blocking work still runs, and only the resume waits for a thread.
	struct BlockingPoolTests : ::testing::Test{};

	//The detached awaiters, as Tests::Connect.
	Ω awaiting( absl::AnyInvocable<int()> work, sp<Awaited<optional<int>>> state )ι->VoidTask{
		try{
			state->Result = co_await BlockingAwait<int>{ std::move(work) };
		}
		catch( Exception& e ){
			state->Error = e.Move();
		}
		state->Done.test_and_set();
	}
	Ω await( absl::AnyInvocable<int()> work )ι->sp<Awaited<optional<int>>>{
		auto state = ms<Awaited<optional<int>>>();
		awaiting( std::move(work), state );
		return state;
	}
	Ω testConnecting( ServerCnnctn cnnctn, Credential cred, sp<Connecting> state )ι->VoidTask{
		try{
			state->Result = co_await TestConnectAwait{ move(cnnctn), move(cred) };
		}
		catch( Exception& e ){
			state->Error = e.Move();
		}
		state->Done.test_and_set();
	}
	Ω testConnect( ServerCnnctn cnnctn, Credential cred )ι->sp<Connecting>{
		auto state = ms<Connecting>();
		testConnecting( move(cnnctn), move(cred), state );
		return state;
	}

	//A listener that accepts one connection and never answers its HEL:  open62541 gives the handshake no deadline of its own, so a
	//connect to it waits for its session until something else ends it.  Polled by the test's thread.
	struct SilentListener final{
		SilentListener()ι{
			_acceptor.async_accept( _peer, [this]( const boost::system::error_code& ec ){
				if( ec )
					return;
				_accepted.test_and_set();
				Read();
			});
		}
		α Url()Ι->string{ return Ƒ( "opc.tcp://127.0.0.1:{}", _acceptor.local_endpoint().port() ); }
		α Accepted()ι->bool{ return Polled( [this]{ return _accepted.test(); }, 10s ); }
		α Closed()ι->bool{ return Polled( [this]{ return _closed.test(); }, 5s ); }//the client's disconnect closed the socket.
		α Close()ι->void{ _peer.close(); _acceptor.close(); }//and a reconnect to the port is refused.
	private:
		α Polled( function<bool()> pred, Duration timeout )ι->bool{ return WaitUntil( [&]{ _ctx.poll(); return pred(); }, timeout ); }
		α Read()ι->void{
			_peer.async_read_some( boost::asio::buffer(_buffer), [this]( const boost::system::error_code& ec, std::size_t ){
				if( ec )
					_closed.test_and_set();
				else
					Read();
			});
		}
		boost::asio::io_context _ctx;
		boost::asio::ip::tcp::acceptor _acceptor{ _ctx, {boost::asio::ip::make_address("127.0.0.1"), 0} };
		boost::asio::ip::tcp::socket _peer{ _ctx };
		std::array<char,1024> _buffer;
		atomic_flag _accepted, _closed;
	};

	TEST_F( BlockingPoolTests, WorkRunsWithEveryExecutorThreadHeld ){
		ExecutorHold hold;
		ASSERT_TRUE( hold.Held() ) << "every executor thread held:  " << hold.Holding() << " of " << hold.Threads();
		auto ran = ms<atomic_flag>();//the work's own:  it runs on the pool, whatever this test has given up on.
		let awaited = await( [ran]{ ran->test_and_set(); return 42; } );
		EXPECT_TRUE( WaitUntil([&]{ return ran->test(); }, 2s) ) << "the work ran with no executor thread free";
		EXPECT_FALSE( awaited->Done.test() ) << "the awaiter resumes on the executor, which is held";
		hold.Release();
		ASSERT_TRUE( awaited->Wait(5s) ) << "the awaiter resumed once a thread was free";
		EXPECT_FALSE( awaited->Error ) << awaited->Error->what();
		EXPECT_EQ( awaited->Result, 42 );
	}

	TEST_F( BlockingPoolTests, WhatTheWorkThrowsReachesTheAwaiter ){
		let thrown = await( []()->int{ throw Exception{ SRCE_CUR, {EHttpStatus::BadRequest}, "refused on the pool" }; } );
		ASSERT_TRUE( thrown->Wait(5s) );
		ASSERT_TRUE( thrown->Error ) << "an Exception, as its own type";
		EXPECT_EQ( thrown->Error->HttpStatus(), EHttpStatus::BadRequest );
		EXPECT_TRUE( sv{thrown->Error->what()}.contains("refused on the pool") ) << thrown->Error->what();
		EXPECT_FALSE( thrown->Result );

		let wrapped = await( []()->int{ throw std::runtime_error{ "a plain runtime_error" }; } );
		ASSERT_TRUE( wrapped->Wait(5s) );
		ASSERT_TRUE( wrapped->Error ) << "any other runtime_error, wrapped";
		EXPECT_TRUE( sv{wrapped->Error->what()}.contains("a plain runtime_error") ) << wrapped->Error->what();
	}

	//The issue's own test, in its hermetic form:  a test connect to the embedded OpcServer for a slug with no certificate yet.  The
	//issue - the slug's certificate signed and written to /gateway/issuedCerts, under Crypto::EnsureKeyCertificate's lock - is the
	//connect's first blocking step, and the file's arrival while every executor thread is held is the proof the build ran elsewhere.
	//Only the certificate goes:  the key is the gateway's one, shared by every slug and its own web identity.  Released, the session
	//comes up as it always did, and the test client leaves nothing registered.
	TEST_F( BlockingPoolTests, ATestConnectIsBuiltWithEveryExecutorThreadHeld ){
		const string slug{ "blockingPoolHeld" };
		let settings = UAClient::CryptoSettings( slug );
		let remove = [&]{ fs::remove( settings.Certificate.Path ); };
		remove();
		absl::Cleanup removed = remove;
		let cred = SessionCredential();//before the hold:  the jwt is fetched over the socket, which needs the executor.
		ServerCnnctn cnnctn{ OpcUrl() };
		cnnctn.Slug = slug;
		cnnctn.Name = slug;
		cnnctn.CertificateUri = OpcUrn();

		ExecutorHold hold;
		ASSERT_TRUE( hold.Held() ) << "every executor thread held:  " << hold.Holding() << " of " << hold.Threads();
		let connect = testConnect( move(cnnctn), cred );
		absl::Cleanup stop = [&]{ if( connect->Done.test() && connect->Result ) connect->Result->StopProcessing(); };
		EXPECT_TRUE( WaitUntil([&]{ return fs::exists(settings.Certificate.Path); }, 10s) ) << "the certificate was issued with no executor thread free";
		EXPECT_FALSE( connect->Done.test() ) << "the session needs the client's strand, which is held";
		hold.Release();
		ASSERT_TRUE( connect->Wait() ) << "the connect finished once a thread was free";
		ASSERT_FALSE( connect->Error ) << connect->Error->what();
		let& client = connect->Result;
		ASSERT_TRUE( client );
		EXPECT_TRUE( Negotiated(client).ends_with("/SignAndEncrypt)") ) << Negotiated( client );
		EXPECT_FALSE( UAClient::ConnectionCounts().contains(slug) ) << "a test connect leaves no client registered";
		EXPECT_FALSE( UAClient::ConnectErrors().contains(slug) );
	}

	//#353 review #1:  BlockingPool::Stop lets a connect still building finish, and the client it builds waits for its session in
	//_awaitingActivation - in no registry Shutdown's snapshot read, and registered by its activation after it.  AbandonConnects, which
	//Shutdown runs before the snapshot, takes it out:  the client stopped, its waiter failed BadShutdown.
	TEST_F( BlockingPoolTests, AbandonConnectsStopsAConnectAwaitingItsSession ){
		SilentListener listener;
		const string slug{ "blockingPoolPending" };
		PurgeServerCnnctn( slug );//a row left by an earlier run names another port.
		GetConnection( slug, listener.Url(), {} );
		absl::Cleanup purge = [&]{ PurgeServerCnnctn( slug ); };

		let connect = Connect( slug, SessionCredential() );
		ASSERT_TRUE( listener.Accepted() ) << "the connect reached the listener";
		EXPECT_FALSE( connect->Done.test() ) << "nothing answers the HEL, so the connect waits for its session";
		EXPECT_GE( UAClient::AbandonConnects(), 1u );
		ASSERT_TRUE( connect->Wait(5s) ) << "the waiter is answered";
		EXPECT_FALSE( connect->Result );
		let& error = connect->Error;
		ASSERT_TRUE( error );
		EXPECT_EQ( error->Code(), UA_STATUSCODE_BADSHUTDOWN ) << error->what();
		EXPECT_TRUE( sv{error->what()}.contains("The gateway is shutting down.") ) << error->what();
		EXPECT_TRUE( listener.Closed() ) << "the client is stopped:  its disconnect closes the socket";
		EXPECT_FALSE( UAClient::ConnectionCounts().contains(slug) ) << "nothing registered it";
		EXPECT_FALSE( UAClient::ConnectErrors().contains(slug) ) << "a shutdown is not the slug's error";
	}

	//#353 review #5:  an edit reaches only registered clients, and a connect is in none until its session activates - so a client built
	//from its row before an edit registered on the old url and settings, and one built before a delete on a row that was gone.  The
	//client carries the row's edit count now, and once answered it is stopped unregistered where the row was edited since, and the
	//connect runs again on the row as it is.  Here the url moves from a listener that never answers to the embedded OpcServer while
	//the client waits on it, and the listener's refusal of open62541's reconnect is that client's answer.
	TEST_F( BlockingPoolTests, AConnectEditedUnderItConnectsOnTheNewRow ){
		SilentListener listener;
		const string slug{ "blockingPoolEdited" };
		PurgeServerCnnctn( slug );
		let row = GetConnection( slug, listener.Url(), {} );
		absl::Cleanup purge = [&]{ PurgeServerCnnctn( slug ); };//closes the client too.

		let connect = Connect( slug, SessionCredential() );
		ASSERT_TRUE( listener.Accepted() ) << "the connect reached the listener";
		QL().QuerySync<jvalue>( Ƒ(R"(mutation updateServerConnection( id:{}, url:"{}" ))", row.Id, OpcUrl()), {}, {UserPK::System} );
		EXPECT_FALSE( connect->Done.test() ) << "the client is still waiting on the listener";
		listener.Close();
		ASSERT_TRUE( connect->Wait() ) << "the waiter is answered";
		ASSERT_FALSE( connect->Error ) << connect->Error->what();
		let& client = connect->Result;
		ASSERT_TRUE( client );
		EXPECT_EQ( client->Url(), OpcUrl() );
		EXPECT_EQ( UAClient::Find(slug, client->Credential()), client );
		EXPECT_FALSE( UAClient::ConnectErrors().contains(slug) ) << "the old url's refusal is not the slug's error";
	}
}