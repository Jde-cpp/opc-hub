#include "utils/helpers.h"
#include "../src/UAClient.h"
#include "../src/async/BlockingPool.h"
#include "../src/async/TestConnectAwait.h"
#include "../src/auth/OpcServerSession.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	//#353:  a client's build and connect - the slug's certificate issued, trustedCertDirs scanned, the host resolved and probed - ran
	//on the executor thread that awaited it, which also runs every client strand and every HTTP handler, so a connect to a host with
	//slow DNS or no route stalled them all.  Both connects go to the blocking pool now, and the proof is the issue's own:  with every
	//executor thread held, the blocking work still runs, and only the resume waits for a thread.
	struct BlockingPoolTests : ::testing::Test{};

	//The detached awaiter:  `result` or `error` set, then `done` raised.  Pointers, as Tests::Connect - captures would die with the
	//temporary at the first suspension.
	Ω await( absl::AnyInvocable<int()> work, optional<int>* result, up<Exception>* error, atomic_flag* done )ι->VoidTask{
		try{
			*result = co_await BlockingAwait<int>{ std::move(work) };
		}
		catch( Exception& e ){
			*error = e.Move();
		}
		done->test_and_set();
		done->notify_all();
	}
	Ω testConnect( ServerCnnctn cnnctn, Credential cred, sp<UAClient>* client, up<Exception>* error, atomic_flag* done )ι->VoidTask{
		try{
			*client = co_await TestConnectAwait{ move(cnnctn), move(cred) };
		}
		catch( Exception& e ){
			*error = e.Move();
		}
		done->test_and_set();
		done->notify_all();
	}

	TEST_F( BlockingPoolTests, WorkRunsWithEveryExecutorThreadHeld ){
		ExecutorHold hold;
		ASSERT_TRUE( hold.Held() ) << "every executor thread held:  " << hold.Holding() << " of " << hold.Threads();
		atomic_flag ran; optional<int> result; up<Exception> error; atomic_flag done;
		await( [&]{ ran.test_and_set(); return 42; }, &result, &error, &done );
		EXPECT_TRUE( WaitUntil([&]{ return ran.test(); }, 2s) ) << "the work ran with no executor thread free";
		EXPECT_FALSE( done.test() ) << "the awaiter resumes on the executor, which is held";
		hold.Release();
		ASSERT_TRUE( WaitUntil([&]{ return done.test(); }, 5s) ) << "the awaiter resumed once a thread was free";
		EXPECT_FALSE( error ) << error->what();
		EXPECT_EQ( result, 42 );
	}

	TEST_F( BlockingPoolTests, WhatTheWorkThrowsReachesTheAwaiter ){
		optional<int> result; up<Exception> error; atomic_flag done;
		await( []()->int{ throw Exception{ SRCE_CUR, {EHttpStatus::BadRequest}, "refused on the pool" }; }, &result, &error, &done );
		ASSERT_TRUE( WaitUntil([&]{ return done.test(); }, 5s) );
		ASSERT_TRUE( error ) << "an Exception, as its own type";
		EXPECT_EQ( error->HttpStatus(), EHttpStatus::BadRequest );
		EXPECT_TRUE( sv{error->what()}.contains("refused on the pool") ) << error->what();
		EXPECT_FALSE( result );

		up<Exception> wrapped; atomic_flag done2;
		await( []()->int{ throw std::runtime_error{ "a plain runtime_error" }; }, &result, &wrapped, &done2 );
		ASSERT_TRUE( WaitUntil([&]{ return done2.test(); }, 5s) );
		ASSERT_TRUE( wrapped ) << "any other runtime_error, wrapped";
		EXPECT_TRUE( sv{wrapped->what()}.contains("a plain runtime_error") ) << wrapped->what();
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

		sp<UAClient> client; up<Exception> error; atomic_flag done;
		absl::Cleanup stop = [&]{ if( client ) client->StopProcessing(); };
		ExecutorHold hold;
		ASSERT_TRUE( hold.Held() ) << "every executor thread held:  " << hold.Holding() << " of " << hold.Threads();
		testConnect( move(cnnctn), cred, &client, &error, &done );
		EXPECT_TRUE( WaitUntil([&]{ return fs::exists(settings.Certificate.Path); }, 10s) ) << "the certificate was issued with no executor thread free";
		EXPECT_FALSE( done.test() ) << "the session needs the client's strand, which is held";
		hold.Release();
		ASSERT_TRUE( WaitUntil([&]{ return done.test(); }, 20s) ) << "the connect finished once a thread was free";
		ASSERT_FALSE( error ) << error->what();
		ASSERT_TRUE( client );
		EXPECT_TRUE( Negotiated(client).ends_with("/SignAndEncrypt)") ) << Negotiated( client );
		EXPECT_FALSE( UAClient::ConnectionCounts().contains(slug) ) << "a test connect leaves no client registered";
		EXPECT_FALSE( UAClient::ConnectErrors().contains(slug) );
	}
}