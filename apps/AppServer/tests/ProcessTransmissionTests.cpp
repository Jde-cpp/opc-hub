#include <jde/fwk/log/MemoryLog.h>
#include <jde/access/usings.h>
#include <jde/ql/ql.h>
#include <jde/ql/QLAwait.h>
#include "helpers.h"
#include "../src/LocalClient.h"
#include "../src/appStartup.h"
#include <thread>
#define let const auto

//ServerSocketSession::ProcessTransmission hardening: hostile or malformed frames on an (unauthenticated) socket must
//produce exception replies or logs - never a crash, a stack overflow, or a wedged session.
namespace Jde::App::Server::Tests{

	struct ProcessTransmissionTests : ::testing::Test{
		α SetUp()->void override{ _session = Connect(); }
		α TearDown()->void override{
			if( _session )
				BlockVoidAwait( _session->Close(true, SRCE_CUR) );
			_session = nullptr;
		}
		//sends kSessionId 0 and expects its exception reply - proves the socket still round-trips after a hostile frame.
		α AssertAlive()->void{
			let requestId = Send( *_session, [](auto& m){ m.set_session_id( 0 ); } );
			auto reply = _session->WaitForException( requestId );
			ASSERT_TRUE( reply ) << "session no longer answers requests";
			EXPECT_TRUE( reply->exception().what().contains("SessionId not set") ) << reply->exception().what();
		}
		Ω ArgumentContains( const Logging::Entry& m, sv text )->bool{
			return std::ranges::any_of( m.Arguments, [&](let& a){ return a.contains(text); } );
		}
		Ω Logged( sv text )->bool{ return WaitUntil( [=]{ return !Logging::Find([=](let& m){ return ArgumentContains(m, text); }).empty(); } ); }
		//wraps inner in `levels` kExecuteAnonymous envelopes, all carrying requestId.
		Ω Nest( FromClientTrans inner, uint levels, RequestId requestId )->FromClientTrans{
			for( uint i=0; i<levels; ++i )
				inner = OneMessage( requestId, [&](auto& m){ m.set_execute_anonymous( Protobuf::ToString(inner) ); } );
			return inner;
		}
		//a kForwardExecution addressed at a registered instance; the payload is never parsed by the target here.
		Ω Forward( const RegisteredInstance& target, RequestId requestId )->FromClientTrans{
			return OneMessage( requestId, [&](auto& m){
				auto& f = *m.mutable_forward_execution();
				f.set_app_pk( target.Program );
				f.set_app_instance_pk( target.Instance );
				*f.mutable_execution_transmission() = string{"payload"};
			});
		}
		sp<RawClientSession> _session;
	};

	//_maxExecuteDepth==4: four envelopes still reach the innermost message - its own "SessionId not set" reply proves
	//the whole chain was processed.
	TEST_F( ProcessTransmissionTests, ExecuteDepthAtCap ){
		let requestId = _session->NextRequestId();
		_session->Write( Nest(OneMessage(requestId, [](auto& m){ m.set_session_id( 0 ); }), 4, requestId) );
		auto reply = _session->WaitForException( requestId );
		ASSERT_TRUE( reply );
		EXPECT_TRUE( reply->exception().what().contains("SessionId not set") ) << reply->exception().what();
	}

	//five envelopes exceed the cap - the guard against one frame driving ProcessTransmission deep enough to overflow
	//the stack (each level is a fresh ParseFromString, so protobuf's own recursion limit never sees it).
	TEST_F( ProcessTransmissionTests, ExecuteDepthOverCap ){
		let requestId = _session->NextRequestId();
		_session->Write( Nest(OneMessage(requestId, [](auto& m){ m.set_session_id( 0 ); }), 5, requestId) );
		auto reply = _session->WaitForException( requestId );
		ASSERT_TRUE( reply );
		EXPECT_TRUE( reply->exception().what().contains("nesting depth") ) << reply->exception().what();
		AssertAlive();
	}

	TEST_F( ProcessTransmissionTests, MalformedExecutePayload ){
		let requestId = Send( *_session, [](auto& m){ m.set_execute_anonymous( "ABCDEFG" ); } );//not a Transmission - Deserialize must throw, not crash.
		ASSERT_TRUE( _session->WaitForException(requestId) );
		AssertAlive();
	}

	TEST_F( ProcessTransmissionTests, BadQueryVariables ){
		let requestId = Send( *_session, [](auto& m){
			auto& query = *m.mutable_query();
			query.set_text( "connections{ id }" );
			query.set_variables( "not json" );
		});
		ASSERT_TRUE( _session->WaitForException(requestId) );
		AssertAlive();
	}

	TEST_F( ProcessTransmissionTests, EmptyTransmission ){
		Logging::ClearMemory();
		_session->Write( FromClientTrans{} );
		EXPECT_TRUE( Logged("No messages in transmission.") );
		AssertAlive();
	}

	TEST_F( ProcessTransmissionTests, UnknownMessageIgnored ){
		Logging::ClearMemory();
		Send( *_session, [](auto&){} );//no value set - VALUE_NOT_SET reaches the default branch.
		EXPECT_TRUE( Logged("Unknown message type") );
		AssertAlive();
	}

	//a kException reply that matches no pending QueryClient call and no forward is a critical - it means a response was
	//dropped on the floor.
	TEST_F( ProcessTransmissionTests, UnmatchedExceptionLogged ){
		Logging::ClearMemory();
		Send( *_session, 55, [](auto& m){ m.mutable_exception()->set_what( "boom" ); } );
		EXPECT_TRUE( Logged("Exception not handled") );
		AssertAlive();
	}

	//request_id 0 is fire-and-forget: logged at Debug, resolved against nothing.
	TEST_F( ProcessTransmissionTests, ExceptionWithoutRequestIdIgnored ){
		Logging::ClearMemory();
		Send( *_session, 0, [](auto& m){ m.mutable_exception()->set_what( "boom" ); } );
		AssertAlive();
		let logs = Logging::Find( [](let& m){ return ArgumentContains(m, "Exception not handled"); } );
		EXPECT_TRUE( logs.empty() );
	}

	//a 32-bit session id is brute-forceable; _maxFailedAdoptions==5 consecutive failures close an unauthenticated socket.
	TEST_F( ProcessTransmissionTests, FailedAdoptionsCloseSocket ){
		for( uint i=0; i<5; ++i ){
			let requestId = Send( *_session, [i](auto& m){ m.set_session_id( 0xBAD5E550+i ); } );
			ASSERT_TRUE( _session->WaitForException(requestId) ) << "adoption " << i;
		}
		EXPECT_TRUE( _session->WaitForClose() ) << "5th failed adoption must close the socket";
		//#17: and close it properly.  OnClose() only unregistered the session and nulled the stream, so the peer saw the transport
		//vanish - no close frame - and the exception above raced the teardown.  websocket::error::closed is what a client's
		//read reports when the server closed the handshake; a dropped transport gives end_of_stream/connection_reset instead.
		EXPECT_EQ( _session->CloseCode(), boost::beast::websocket::error::closed ) << _session->CloseCode().message();
	}

	//an authenticated socket (kInstance sets _userPK) survives bad adoptions - only anonymous guessing accumulates.
	TEST_F( ProcessTransmissionTests, AuthenticatedSocketSurvivesFailedAdoptions ){
		RegisterInstance( *_session, "Tests.Adoption", "survives", "adoption-host", 0 );
		for( uint i=0; i<6; ++i ){
			let requestId = Send( *_session, [i](auto& m){ m.set_session_id( 0xBAD5E550+i ); } );
			ASSERT_TRUE( _session->WaitForException(requestId) ) << "adoption " << i;
		}
		EXPECT_EQ( _session->CloseCount(), 0u );
		AssertAlive();
	}
	//auth_result comes back only when the auth_resource arm ran at all:  an instance that named no resource never reaches
	//TestSchemaAdmin, so it is not installed as anyone's authorizer.  ASchemaAdminIsDelegated covers the arm itself.
	TEST_F( ProcessTransmissionTests, AnInstanceThatAuthorizesNothingIsNotDelegated ){
		let none = RegisterInstance( *_session, "Tests.AuthResource", "plain", "auth-host", 0, 1234 );
		EXPECT_FALSE( none.AuthResult ) << "an instance that asked to authorize nothing must not be registered";
	}
	//The positive half:  the schema's root resource is active and the registrant administers it.  The rows are made on the
	//server's own QL, so the registration also proves the AppServer's cache takes another schema's events (appserver-review3 #13:
	//its subscriptions were filtered to access/app, and neither the resource nor the acl would have reached it).
	TEST_F( ProcessTransmissionTests, ASchemaAdminIsDelegated ){
		constexpr sv schema{ "opc.probe" };
		let admin = User( "probe-admin" ), nobody = User( "probe-nobody" );
		if( RunQL(Ƒ(R"(resources( schemaName:"{}", slug:"nodeIds", deleted:null ){{id}})", schema)).as_array().empty() )
			RunQL( Ƒ(R"(mutation createResource( schemaName:"{}", name:"probe nodes", slug:"nodeIds", allowed:255 ))", schema) );
		RunQL( Ƒ(R"(mutation createAcl( identity:{{id:{}}}, permissionRight:{{ allowed:{}, denied:0, resource:{{schemaName:"{}", slug:"nodeIds"}} }} ))", admin.Value, underlying(Access::ERights::Administer), schema) );

		let registered = RegisterInstance( *_session, "Tests.AuthResource", "authorizer", "auth-host", 0, 1234, string{schema}, admin );
		EXPECT_TRUE( registered.AuthResult ) << "the registrant administers the schema's root resource";
		auto other = Connect();
		let denied = RegisterInstance( *other, "Tests.AuthResource", "pretender", "auth-host", 0, 1235, string{schema}, nobody );
		EXPECT_FALSE( denied.AuthResult ) << "a user without Administer on the root may not stand in for the schema";
		BlockVoidAwait( other->Close(true, SRCE_CUR) );
	}

	//The delegated admin check end to end:  granting a role rights on ANOTHER schema's resource asks that schema's registered
	//instance, over the socket, whether the executer may grant it (appserver-review3 #13).  The AppServer parses the `adminCheck`
	//query HERE before writing it, so the name must be one of Configure's system tables - it owns no view, and without that
	//registration the mutation died at its own parse with "Could not find view 'admin_checks'", never reaching the instance.
	TEST_F( ProcessTransmissionTests, ADelegatedAdminCheckReachesTheInstance ){
		constexpr sv schema{ "opc.delegate" };
		let admin = User( "delegate-admin" );
		if( RunQL(Ƒ(R"(resources( schemaName:"{}", slug:"nodeIds", deleted:null ){{id}})", schema)).as_array().empty() )
			RunQL( Ƒ(R"(mutation createResource( schemaName:"{}", name:"delegate nodes", slug:"nodeIds", allowed:255 ))", schema) );
		RunQL( Ƒ(R"(mutation createAcl( identity:{{id:{}}}, permissionRight:{{ allowed:{}, denied:0, resource:{{schemaName:"{}", slug:"nodeIds"}} }} ))", admin.Value, underlying(Access::ERights::Administer), schema) );
		let registered = RegisterInstance( *_session, "Tests.Delegate", "delegate", "auth-host", 0, 1234, string{schema}, admin );
		ASSERT_TRUE( registered.AuthResult ) << "the registrant administers the schema's root resource";

		let grant = Ƒ( R"(addRole( id:{}, permissionRight:{{ allowed:{}, denied:0, resource:{{ schemaName:"{}", slug:"nodeIds" }} }} ))", FindOrCreate("role", "delegate-role"), underlying(Access::ERights::Read), schema );
		AskInstance( *_session, grant, "adminCheck", [this](let& asked){
			Send( *_session, asked.request_id(), [](auto& m){ m.set_query_result( R"({"adminCheck":{"isAdmin":true}})" ); } );
		});
	}

	//#16:  kSessionId recorded the session *id* and nothing else, so a socket that had adopted a session went on acting as
	//user 0 - it passed the forward guard (which tests the id) and the request went out as kExecuteAnonymous with the
	//caller's identity silently dropped.  Identity now comes from the SessionInfo the adoption installs.
	TEST_F( ProcessTransmissionTests, AnAdoptedSessionIsTheSocketsIdentity ){
		constexpr Jde::UserPK user{ 7 };//no row needed - the adoption reads the minted session, and the forward only carries the pk.
		auto target = Connect();
		let app = RegisterInstance( *target, "Tests.Adopt", "adopt-target", "adopt-host", 0 );
		{//with no session at all the forward is refused outright.
			let requestId = _session->NextRequestId();
			_session->Write( Forward(app, requestId) );
			auto reply = _session->WaitForException( requestId );
			ASSERT_TRUE( reply );
			EXPECT_TRUE( reply->exception().what().contains("requires an authenticated session") ) << reply->exception().what();
		}
		{//adopt a session minted for `user`; the reply is a bare Complete.
			let requestId = Send( *_session, [=](auto& m){ m.set_session_id( MintSession(user) ); } );
			auto reply = _session->WaitFor( [requestId](let& m){ return m.request_id()==requestId; } );
			ASSERT_TRUE( reply );
			ASSERT_NE( reply->value_case(), FromServerMessage::kException ) << reply->exception().what();
		}
		{//the forward now carries the adopted user.
			_session->Write( Forward(app, _session->NextRequestId()) );
			auto forwarded = target->WaitFor( [](let& m){ return m.value_case()==FromServerMessage::kExecute || m.value_case()==FromServerMessage::kExecuteAnonymous; } );
			ASSERT_TRUE( forwarded );
			ASSERT_EQ( forwarded->value_case(), FromServerMessage::kExecute ) << "sent anonymously - the adopted session's user never reached the socket";
			EXPECT_EQ( forwarded->execute().user_pk(), user.Value );
		}
		BlockVoidAwait( target->Close(true, SRCE_CUR) );//the forward is never answered; closing cancels it.
	}
}
