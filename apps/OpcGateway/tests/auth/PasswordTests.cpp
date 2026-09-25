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
	//A counter and a predicate, not a bare wait:  a login that hits the AuthCache completes synchronously
	//(PasswordAwait::await_resume), so its notify landed before the test thread reached its wait and the suite hung there
	//- unseen for as long as the fixture skipped these tests (the embedded server offered no username token until
	//install-issues #1 gave it a login list).
	static std::condition_variable cv;
	static std::mutex mtx;
	static vector<SessionPK> _sessionIds;
	static up<Exception> _exception;
	static uint _completed{};
	const string _password = "0123456789ABCD";
	//sessionId: the web session the login is made from (HttpRequestAwait::Login's _request.SessionInfo) - a live one, minted
	//in-process on the embedded AppServer, as the web server gives a login page's request:  anonymous, no user behind it.
	//pruneDeadSessions drops any id no web session owns, which is what happened to the literal `1` this used to pass.
	Ω session()ι->SessionPK{ return Web::Server::Sessions::Add( Jde::UserPK{}, "localhost", false )->SessionId; }
	//The bookkeeping in a plain function, not in the coroutine:  with the lock and the notify in the body after the try/catch,
	//clang 22.1's optimizer (-O3, the release preset) crashed in jump-threading on the resume function - the tag build of
	//2026.09.01.  Out of line, the coroutine is one await and one call.
	[[gnu::noinline]] Ω completed( optional<Web::FromServer::SessionInfo>&& sessionInfo, up<Exception>&& exception )ι->void{
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
	α AuthenticateTest( ServerCnnctnNK opcId, SessionPK sessionId, bool badPassword=false )ι->TAwait<optional<Web::FromServer::SessionInfo>>::Task{
		optional<Web::FromServer::SessionInfo> sessionInfo; up<Exception> exception;
		try{
			sessionInfo = co_await PasswordAwait{ "user1", badPassword ? "xyz" : _password, move(opcId), "localhost", false, sessionId };
		}
		catch( Exception& e ){
			exception = e.Move();
		}
		completed( move(sessionInfo), move(exception) );
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
		AuthenticateTest( opcId, session() );//an anonymous session each:  the cache does not vouch, so each mints its own (install-issues #47)
		waitFor( 2 );
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

	//install-issues #47:  a login page with no session sends 0.  With user1's credential already cached, the AuthCache used to
	//vouch for that 0 and hand it back as the session - the user signed in as nobody.  Each must get a session of its own.
	TEST_F( PasswordTests, Authenticate_Sessionless ){
		string opcId{ Connection->Slug };
		{ std::lock_guard l{ mtx }; _completed = 0; _sessionIds.clear(); _exception = nullptr; }
		AuthenticateTest( opcId, session() );//cached, whatever ran before
		waitFor( 1 );
		AuthenticateTest( opcId, 0 );
		waitFor( 2 );
		AuthenticateTest( opcId, 0 );
		waitFor( 3 );
		ASSERT_FALSE( _exception ) << _exception->what();
		ASSERT_EQ( _sessionIds.size(), 3u );
		EXPECT_NE( _sessionIds[1], 0u );
		EXPECT_NE( _sessionIds[2], 0u );
		EXPECT_NE( _sessionIds[1], _sessionIds[2] );
		for( let sessionId : {_sessionIds[1], _sessionIds[2]} ){
			let creds = GetCredential( sessionId, opcId );
			ASSERT_TRUE( creds ) << hex( sessionId );
			EXPECT_EQ( "user1", creds->LoginName() );
		}
		EXPECT_FALSE( GetCredential(0, opcId) );
	}

	//install-issues #47, as the walk met it:  a login page's session is live but anonymous - the web server gives every request
	//without an Authorization header a fresh one.  The first fix vouched for any live session, so the cache still answered, the
	//page kept its anonymous session, and every request after it was (401).
	TEST_F( PasswordTests, Authenticate_AnonymousSession ){
		string opcId{ Connection->Slug };
		{ std::lock_guard l{ mtx }; _completed = 0; _sessionIds.clear(); _exception = nullptr; }
		AuthenticateTest( opcId, session() );//cached, whatever ran before
		waitFor( 1 );
		let anonymous = session();
		AuthenticateTest( opcId, anonymous );
		waitFor( 2 );
		ASSERT_FALSE( _exception ) << _exception->what();
		ASSERT_EQ( _sessionIds.size(), 2u );
		let minted = _sessionIds[1];
		EXPECT_NE( minted, 0u );
		EXPECT_NE( minted, anonymous ) << "the login kept the page's anonymous session";
		let web = Web::Server::Sessions::Find( minted );
		ASSERT_TRUE( web ) << hex( minted );
		EXPECT_TRUE( web->UserPK ) << "the minted session has no user";
		let creds = GetCredential( minted, opcId );
		ASSERT_TRUE( creds );
		EXPECT_EQ( "user1", creds->LoginName() );
		EXPECT_FALSE( GetCredential(anonymous, opcId) );
	}

	//A signed-in session's re-auth is what the cache is for:  it hits, and the session keeps its id.
	TEST_F( PasswordTests, Authenticate_SignedInReauth ){
		string opcId{ Connection->Slug };
		{ std::lock_guard l{ mtx }; _completed = 0; _sessionIds.clear(); _exception = nullptr; }
		AuthenticateTest( opcId, session() );
		waitFor( 1 );
		ASSERT_EQ( _sessionIds.size(), 1u );
		let user = GetCredential( _sessionIds[0], opcId )->UserPK();
		ASSERT_TRUE( user );
		let signedIn = Web::Server::Sessions::Add( user, "localhost", false )->SessionId;
		AuthenticateTest( opcId, signedIn );
		waitFor( 2 );
		ASSERT_FALSE( _exception ) << _exception->what();
		ASSERT_EQ( _sessionIds.size(), 2u );
		EXPECT_EQ( _sessionIds[1], signedIn ) << "a signed-in re-auth should keep its session";
		let creds = GetCredential( signedIn, opcId );
		ASSERT_TRUE( creds );
		EXPECT_EQ( user, creds->UserPK() );
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