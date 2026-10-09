//app-review3 #1:  the AppServer forwards any socket's bytes to a gateway, and the gateway used to run the kClientQuery inside
//them as whatever executer_pk the payload named - UserPK::System included, which Authorize grants ERights::All.  The identity a
//forwarded transmission runs under must come from the envelope the AppServer stamped, and nesting must be bounded (C9).
//No socket is involved:  the session is constructed with an executor and an empty ssl context, and ProcessTransmission is driven
//directly through the TESTS seam.
#include <gtest/gtest.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/process/execution.h>
#include <jde/web/client/ClientSsl.h>
#include <jde/app/client/IAppClient.h>

#define let const auto

namespace Jde::App::Tests{
	using Client::AppClientSocketSession;
	using Trans = Proto::FromServer::Transmission;

	//How many ClientQuery coroutines have run to completion.  Bumped after Resume returns, which is after the whole body - including
	//the Write of the result - has run;  the fixture waits on it before letting go of the session the body writes on.
	static std::atomic<uint> _completed{};//static: internal linkage, per L12 of the review.

	//Resumed through the executor, not inline:  Resume runs ClientQuery to completion, which would destroy the frame this awaitable
	//lives in from inside its own await_suspend.
	struct QueryAwait final : TAwait<jvalue>{
		QueryAwait( SL sl )ι:TAwait<jvalue>{ sl }{}
		α Suspend()ι->void override{ Post( [this]{ Resume(jvalue{}); ++_completed; } ); }//nothing touches `this` after Resume - it dies with the frame it lives in.
	};

	//The only member AppClientSocketSession reaches for on the query path;  everything else IAppClient needs is already concrete.
	struct RecordingAppClient final : Client::IAppClient{
		α ClientQuery( QL::RequestQL&&, Jde::UserPK executer, SL sl )ε->up<TAwait<jvalue>> override{
			Executers.push_back( executer );
			return mu<QueryAwait>( sl );
		}
		vector<Jde::UserPK> Executers;
	};

	struct AppClientSocketSessionTests : ::testing::Test{
	protected:
		α SetUp()->void override{
			_completed = 0;
			_client = ms<RecordingAppClient>();
			_session = ms<AppClientSocketSession>( Executor(), optional<ssl::context>{}, nullptr, _client );
		}
		//ClientQuery's continuation runs on the executor and writes its result on the session, which nothing but this fixture holds -
		//in production the pending async_read does.  Let it land before dropping the last reference.
		α TearDown()->void override{
			for( let expiration = steady_clock::now()+std::chrono::seconds{5}; _completed.load()<Executers().size() && steady_clock::now()<expiration; )
				std::this_thread::sleep_for( std::chrono::milliseconds{1} );
			_session = nullptr; _client = nullptr;
		}

		Ω clientQuery( uint32 executerPK )ι->Trans{
			Trans t;
			auto& q = *t.add_messages()->mutable_client_query();
			q.set_query( "{logs(limit:1){entries{level}}}" );//a system table:  QL::Parse resolves it without an AppSchema, which the session has none of.
			q.set_executer_pk( executerPK );
			q.set_raw( true );
			return t;
		}
		//What ForwardExecutionAwait writes for a kForwardExecutionAnonymous:  `levels` bytes-in-bytes envelopes with no identity.
		Ω anonymousEnvelopes( Trans inner, uint levels )ι->Trans{
			for( uint i=0; i<levels; ++i ){
				Trans outer;
				outer.add_messages()->set_execute_anonymous( Protobuf::ToString(inner) );
				inner = move( outer );
			}
			return inner;
		}
		//...and for a kForwardExecution:  one envelope naming the user the AppServer authenticated.
		Ω envelope( Trans inner, uint32 userPK )ι->Trans{
			Trans outer;
			auto& e = *outer.add_messages()->mutable_execute();
			e.set_user_pk( userPK );
			*e.mutable_transmission() = Protobuf::ToString( inner );
			return outer;
		}
		//ClientQuery is eager (suspend_never), so _appClient->ClientQuery has already been called when this returns.
		α Process( Trans t )ι->void{ _session->ProcessTransmissionTest( move(t), nullopt, nullopt, 0 ); }
		α Executers()Ι->const vector<Jde::UserPK>&{ return _client->Executers; }

		sp<RecordingAppClient> _client;
		sp<AppClientSocketSession> _session;
	};

	//Depth 0 is the AppServer speaking on this socket - QueryClient's executer is the user it is acting for, and it still stands.
	TEST_F( AppClientSocketSessionTests, ADirectClientQueryKeepsItsExecuter ){
		Process( clientQuery(9) );
		ASSERT_EQ( Executers().size(), 1u );
		EXPECT_EQ( Executers().front(), Jde::UserPK{9} );
	}

	//The attack:  no login at the AppServer, kForwardExecutionAnonymous, executer_pk=System in the payload.
	TEST_F( AppClientSocketSessionTests, AForwardedQueryMayNotNameAnExecuter ){
		Process( anonymousEnvelopes(clientQuery(Jde::UserPK::System), 1) );
		EXPECT_TRUE( Executers().empty() ) << "a forwarded payload named its own executer";
	}

	TEST_F( AppClientSocketSessionTests, AForwardedAnonymousQueryRunsAnonymous ){
		Process( anonymousEnvelopes(clientQuery(0), 1) );
		ASSERT_EQ( Executers().size(), 1u );
		EXPECT_EQ( Executers().front(), Jde::UserPK{0} );
	}

	//kForwardExecution: the AppServer stamped user 7 on the envelope, so 7 is what the payload runs as.
	TEST_F( AppClientSocketSessionTests, AForwardedQueryRunsAsTheEnvelopesUser ){
		Process( envelope(clientQuery(0), 7) );
		ASSERT_EQ( Executers().size(), 1u );
		EXPECT_EQ( Executers().front(), Jde::UserPK{7} );
	}

	//Identity may only narrow:  a nested kExecute is part of the forwarded payload, so its user_pk is the caller's to choose.
	TEST_F( AppClientSocketSessionTests, ANestedExecuteCannotWidenTheIdentity ){
		Process( anonymousEnvelopes(envelope(clientQuery(0), Jde::UserPK::System), 1) );
		ASSERT_EQ( Executers().size(), 1u );
		EXPECT_EQ( Executers().front(), Jde::UserPK{0} ) << "a nested Execute raised the identity the envelope set";
	}

	//_maxExecuteDepth==4:  four envelopes still reach the innermost message, five do not - without the cap a payload nested a few
	//thousand deep overflows the stack, since each level is a fresh Deserialize protobuf's own recursion guard never sees.
	TEST_F( AppClientSocketSessionTests, ExecuteDepthAtCapIsProcessed ){
		Process( anonymousEnvelopes(clientQuery(0), 4) );
		EXPECT_EQ( Executers().size(), 1u );
	}
	TEST_F( AppClientSocketSessionTests, ExecuteDepthOverCapIsRefused ){
		Process( anonymousEnvelopes(clientQuery(0), 5) );
		EXPECT_TRUE( Executers().empty() );
	}
}
