#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/web/server/Sessions.h>
#include "../../src/auth/PasswordAwait.h"
#include "Auth.h"


#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };

	class PasswordTests : public Auth{
	protected:
		PasswordTests()ι:Auth{ETokenType::Username}{}
		~PasswordTests()override{}

		//Ω SetUpTestCase()ε->void;
		//Ω CheckPasswordsAllowed()ε->void;
		//α SetUp()ι->void override;
		α TearDown()ι->void override{}
		Ω TearDownTestSuite();
	};

	α PasswordTests::TearDownTestSuite(){
		Auth::TearDownTestSuite();
	}
	//A counter and a predicate, not a bare wait:  the second and third logins hit the AuthCache and complete synchronously
	//(PasswordAwait::await_resume), so their notify landed before the test thread reached its wait and the suite hung there
	//- unseen for as long as the fixture skipped these tests (the embedded server offered no username token until
	//install-issues #1 gave it a login list).
	static std::condition_variable cv;
	static std::mutex mtx;
	static vector<SessionPK> _sessionIds;
	static up<Exception> _exception;
	static uint _completed{};
	const string _password = "0123456789ABCD";
	//sessionId: the web session the login is made from (HttpRequestAwait::Login's _request.SessionInfo) - a live one, minted
	//in-process on the embedded AppServer:  a cache hit is stored under it (AuthCache), and pruneDeadSessions drops any id no
	//web session owns, which is what happened to the literal `1` this used to pass - the third login's credential was gone by
	//the time the test read it back.
	Ω session()ι->SessionPK{ return Web::Server::Sessions::Add( Jde::UserPK{}, "localhost", false )->SessionId; }
	α AuthenticateTest( ServerCnnctnNK opcId, SessionPK sessionId, bool badPassword=false )ι->TAwait<optional<Web::FromServer::SessionInfo>>::Task{
		optional<Web::FromServer::SessionInfo> sessionInfo; up<Exception> exception;
		try{
			sessionInfo = co_await PasswordAwait{ "user1", badPassword ? "xyz" : _password, move(opcId), "localhost", false, sessionId };
		}
		catch( Exception& e ){
			exception = e.Move();
		}
		{
			std::lock_guard l{ mtx };
			if( sessionInfo )
				_sessionIds.push_back( sessionInfo->session_id() );
			if( exception )
				_exception = move( exception );
			++_completed;
		}
		cv.notify_all();
	}
	Ω waitFor( uint completed )ε->void{
		std::unique_lock l{ mtx };
		THROW_IF( !cv.wait_for(l, 30s, [completed]{ return _completed>=completed; }), "Timed out waiting for login {} of {}.", _completed, completed );
	}

	TEST_F( PasswordTests, Authenticate ){
		INFO( "PasswordTests.Authenticate" );
		string opcId{ Connection->Slug };
		{ std::lock_guard l{ mtx }; _completed = 0; _sessionIds.clear(); _exception = nullptr; }
		AuthenticateTest( opcId, session() );//the UA login; AddSession mints the session the credential is stored under
		waitFor( 1 );
		AuthenticateTest( opcId, session() );//AuthCache hits - the first login's credential vouches, stored under each caller's session
		AuthenticateTest( opcId, session() );
		waitFor( 3 );
		THROW_IF( _sessionIds.size()!=3, "Expected 3 sessions, found {}.", _sessionIds.size() );
		let creds = GetCredential( _sessionIds[2], opcId );
		ASSERT_TRUE( creds );
		EXPECT_EQ( "user1", creds->LoginName() );
		EXPECT_EQ( _password, creds->Password() );
		EXPECT_TRUE( creds->UserPK() ) << "the stored credential should carry the identity AppServer resolved for the login (also through the AuthCache copy).";
		let counts = SessionCounts(); //opcSessions source: one (connection,type,user) row covering all 3 logins.
		auto count = find_if( counts, [&](let& c){ return c.Connection==opcId && c.Type==ETokenType::Username && c.UserPK==creds->UserPK(); } );
		ASSERT_NE( count, counts.end() );
		EXPECT_GE( count->Count, 3u );
		EXPECT_NE( _sessionIds[0], _sessionIds[1] );
		EXPECT_NE( _sessionIds[0], _sessionIds[2] );
		EXPECT_NE( _sessionIds[1], _sessionIds[2] );
		EXPECT_TRUE( find(_sessionIds, 0)==_sessionIds.end() );
		INFO( "~PasswordTests.Authenticate" );
	}

	TEST_F( PasswordTests, Authenticate_BadPassword ){
		INFO( "PasswordTests.Authenticate_BadPassword" );
		{ std::lock_guard l{ mtx }; _completed = 0; _exception = nullptr; }
		AuthenticateTest( Connection->Slug, session(), true );
		waitFor( 1 );
		EXPECT_TRUE( _exception );
		EXPECT_TRUE( _exception && string{_exception->what()}.contains("BadUserAccessDenied") );
		DBG( "{}", _exception ? _exception->what() : "Error no exception." );
		INFO( "~PasswordTests.Authenticate_BadPassword" );
	}
}