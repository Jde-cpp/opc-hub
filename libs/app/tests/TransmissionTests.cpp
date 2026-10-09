//The transmission builders both ends of the app socket speak through.  Each one sets one oneof member on one message,
//so what these hold is the request id it is answered on, the oneof case the receiver switches on, and the payload.
#include <gtest/gtest.h>
#include <jde/fwk/process/process.h>
#include <jde/app/proto/App.FromServer.pb.h> //app.FromServer.h declares against the generated types without including them.
#include <jde/app/proto/app.FromClient.h>
#include <jde/app/proto/app.FromServer.h>
#include <jde/db/DBException.h>
#include <jde/app/proto/common.h>
#include "helpers.h"

#define let const auto

namespace Jde::App::Tests{
	using CMessage = Proto::FromClient::Message;
	using SMessage = Proto::FromServer::Message;

	TEST( FromClientTests, Query ){
		let variables = jobject{ {"id", 42} };
		let t = FromClient::Query( "{users{id}}", variables, 9, false );
		ASSERT_EQ( t.messages_size(), 1 );
		let& m = t.messages( 0 );
		EXPECT_EQ( m.request_id(), 9u );
		ASSERT_EQ( m.value_case(), CMessage::kQuery );
		EXPECT_EQ( m.query().text(), "{users{id}}" );
		EXPECT_EQ( m.query().variables(), serialize(variables) );
		EXPECT_FALSE( m.query().return_raw() );
	}

	//Same payload as a query, a different oneof member - that is all that tells the server to keep the query live.
	TEST( FromClientTests, Subscription ){
		let variables = jobject{ {"id", 42} };
		let t = FromClient::Subscription( string{"{users{id}}"}, variables, 10 );
		ASSERT_EQ( t.messages_size(), 1 );
		let& m = t.messages( 0 );
		EXPECT_EQ( m.request_id(), 10u );
		ASSERT_EQ( m.value_case(), CMessage::kSubscription );
		EXPECT_EQ( m.subscription().text(), "{users{id}}" );
		EXPECT_EQ( m.subscription().variables(), serialize(variables) );
	}

	//M2's missing half.  The ids are the client's own request ids - the same ones the ack Remembered and StopListenRemote returns -
	//because IWebsocketSession::AddSubscription stores each subscription under the requestId it arrived on.  Nothing built this
	//message before, so IQL::Unsubscribe edited an in-process registry and the server went on pushing the cancelled id.
	TEST( FromClientTests, Unsubscription ){
		let t = FromClient::Unsubscription( vector<QL::SubscriptionId>{7,9}, 15 );
		ASSERT_EQ( t.messages_size(), 1 );
		let& m = t.messages( 0 );
		EXPECT_EQ( m.request_id(), 15u );//its own id, not a subscription's - a failure comes back as a kException on it.
		ASSERT_EQ( m.value_case(), CMessage::kUnsubscription );
		ASSERT_EQ( m.unsubscription().request_ids_size(), 2 );
		EXPECT_EQ( m.unsubscription().request_ids(0), 7u );
		EXPECT_EQ( m.unsubscription().request_ids(1), 9u );
	}

	TEST( FromClientTests, AddSession ){
		let t = FromClient::AddSession( "jde.com", "bob", 3, "127.0.0.1", true, 11 );
		ASSERT_EQ( t.messages_size(), 1 );
		let& m = t.messages( 0 );
		EXPECT_EQ( m.request_id(), 11u );
		ASSERT_EQ( m.value_case(), CMessage::kAddSession );
		EXPECT_EQ( m.add_session().domain(), "jde.com" );
		EXPECT_EQ( m.add_session().login_name(), "bob" );
		EXPECT_EQ( m.add_session().provider_pk(), 3u );
		EXPECT_EQ( m.add_session().user_endpoint(), "127.0.0.1" );
		EXPECT_TRUE( m.add_session().is_socket() );
	}

	TEST( FromClientTests, SessionAndJwtAreBareRequests ){
		let session = FromClient::Session( 12, 13 );
		ASSERT_EQ( session.messages(0).value_case(), CMessage::kSessionInfo );
		EXPECT_EQ( session.messages(0).session_info(), 12u );
		EXPECT_EQ( session.messages(0).request_id(), 13u );

		let jwt = FromClient::Jwt( 14 );
		ASSERT_EQ( jwt.messages(0).value_case(), CMessage::kRequestType );
		EXPECT_EQ( jwt.messages(0).request_type(), Proto::FromClient::ERequestType::Jwt );
		EXPECT_EQ( jwt.messages(0).request_id(), 14u );
	}

	TEST( FromClientTests, Instance ){
		let t = FromClient::Instance( "Jde.App.Tests", "instance", 12, 5, "opc.plant" );
		ASSERT_EQ( t.messages_size(), 1 );
		EXPECT_EQ( t.messages(0).request_id(), 5u );
		let& i = t.messages( 0 ).instance();
		EXPECT_EQ( i.application(), "Jde.App.Tests" );
		EXPECT_EQ( i.instance_name(), "instance" );
		EXPECT_EQ( i.session_id(), 12u );
		EXPECT_EQ( i.host(), Process::HostName() );
		EXPECT_EQ( i.pid(), (uint32_t)Process::ProcessId() );
		EXPECT_EQ( i.web_port(), 5010u ); ///http/port from the test settings.
		EXPECT_EQ( Protobuf::ToTimePoint(i.start_time()), Process::StartTime() );
		//M10: field 10 was never set, so the AppServer's `if( instance.auth_resource().size() )` arm - the whole delegated-admin
		//registration IAdminAcl exists for - could not run, and every check fell back to the AppServer's own Authorize.
		EXPECT_EQ( i.auth_resource(), "opc.plant" );
	}

	//An app that authorizes nothing - the gateway - leaves ResourceSchema empty, and the AppServer's arm stays skipped for it.
	TEST( FromClientTests, InstanceWithoutAnAuthResource ){
		let t = FromClient::Instance( "Jde.App.Tests", "instance", 12, 5, "" );
		EXPECT_TRUE( t.messages(0).instance().auth_resource().empty() );
	}

	TEST( FromClientTests, Exception ){
		let text = FromClient::Exception( string{"boom"}, 3 );
		ASSERT_EQ( text.messages_size(), 1 );
		EXPECT_EQ( text.messages(0).request_id(), 3u );
		EXPECT_EQ( text.messages(0).exception().what(), "boom" );
		EXPECT_EQ( text.messages(0).exception().code(), 0u );
		EXPECT_EQ( text.messages(0).exception().status_code(), 0u ); //text-only stays unset - the receiver resolves 0 to 500.

		let thrown = FromClient::Exception( std::runtime_error{"plain"}, 4 );
		EXPECT_EQ( thrown.messages(0).exception().what(), "plain" );
		EXPECT_EQ( thrown.messages(0).exception().code(), 0u ); //not a Jde::Exception - there is no code to carry.
		EXPECT_EQ( thrown.messages(0).exception().status_code(), 500u ); //unclassified server fault.
		EXPECT_EQ( thrown.messages(0).exception().category(), Jde::Proto::Jde );
		EXPECT_EQ( thrown.messages(0).exception().category_code(), 0u ); //nor a DBException - "unclassified" and "not one" read the same.
	}

	//M10: both directions share Common.proto's Exception, but only FromServer carried the DB classification - so a DB error a client
	//forwarded to the app server arrived looking like it had never been one, and the receiver branches on exactly that.
	TEST( FromClientTests, ExceptionCarriesTheDbClassification ){
		DB::DBException e{ DB::EDbError::Duplicate, DB::Sql{"insert into t values(1)"}, "duplicate key", {} };
		let t = FromClient::Exception( move(e), 5 );
		ASSERT_EQ( t.messages_size(), 1 );
		EXPECT_EQ( t.messages(0).exception().category(), Jde::Proto::DB );
		EXPECT_EQ( t.messages(0).exception().category_code(), (uint32)DB::EDbError::Duplicate );
		EXPECT_EQ( t.messages(0).exception().status_code(), 409u ); //Duplicate -> conflict.
	}

	//Log traffic is unsolicited:  one message per entry, no request to answer.
	TEST( FromClientTests, LogEntries ){
		vector<Logging::Entry> entries{ entry(tp(0), ELogLevel::Information, 1, "a"), entry(tp(1), ELogLevel::Error, 2, "b") };
		let t = FromClient::LogEntries( move(entries) );
		ASSERT_EQ( t.messages_size(), 2 );
		EXPECT_EQ( t.messages(0).request_id(), 0u );
		ASSERT_EQ( t.messages(0).value_case(), CMessage::kLogEntry );
		EXPECT_EQ( t.messages(0).log_entry().text(), "a" );
		EXPECT_EQ( t.messages(1).log_entry().line(), 2u );
	}

	TEST( FromServerTests, Ack ){
		let t = FromServer::Ack( 42 );
		ASSERT_EQ( t.messages_size(), 1 );
		ASSERT_EQ( t.messages(0).value_case(), SMessage::kAck );
		EXPECT_EQ( t.messages(0).ack(), 42u );
		EXPECT_EQ( t.messages(0).request_id(), 0u ); //the handshake answers no request.
	}

	//"your request is done, there is nothing else coming" - a request id and no payload.
	TEST( FromServerTests, Complete ){
		let t = FromServer::Complete( 7 );
		ASSERT_EQ( t.messages_size(), 1 );
		EXPECT_EQ( t.messages(0).request_id(), 7u );
		EXPECT_EQ( t.messages(0).value_case(), SMessage::VALUE_NOT_SET );
	}

	TEST( FromServerTests, Exception ){
		let t = FromServer::Exception( std::runtime_error{"plain"}, RequestId{8} );
		ASSERT_EQ( t.messages_size(), 1 );
		EXPECT_EQ( t.messages(0).request_id(), 8u );
		EXPECT_EQ( t.messages(0).exception().what(), "plain" );
		EXPECT_EQ( t.messages(0).exception().code(), 0u );
		EXPECT_EQ( t.messages(0).exception().status_code(), 500u ); //unclassified server fault.
		EXPECT_EQ( t.messages(0).exception().category(), Jde::Proto::Jde );
		EXPECT_EQ( t.messages(0).exception().category_code(), 0u ); //not a DBException - "unclassified" and "not one" are the same to the receiver.

		let unsolicited = FromServer::Exception( std::runtime_error{"plain"}, nullopt );
		EXPECT_EQ( unsolicited.messages(0).request_id(), 0u );

		let text = FromServer::Exception( string{"boom"}, nullopt );
		EXPECT_EQ( text.messages(0).exception().what(), "boom" );
		EXPECT_EQ( text.messages(0).exception().status_code(), 0u ); //text-only stays unset - the receiver resolves 0 to 500.
	}

	//Forwarded execution carries the user only when there is one;  otherwise it is the anonymous member, not user 0.
	TEST( FromServerTests, ExecuteRequest ){
		let anonymous = FromServer::ExecuteRequest( 3, UserPK{}, string{"payload"} );
		ASSERT_EQ( anonymous.messages_size(), 1 );
		EXPECT_EQ( anonymous.messages(0).request_id(), 3u );
		ASSERT_EQ( anonymous.messages(0).value_case(), SMessage::kExecuteAnonymous );
		EXPECT_EQ( anonymous.messages(0).execute_anonymous(), "payload" );

		let identified = FromServer::ExecuteRequest( 4, UserPK{9}, string{"payload"} );
		EXPECT_EQ( identified.messages(0).request_id(), 4u );
		ASSERT_EQ( identified.messages(0).value_case(), SMessage::kExecute );
		EXPECT_EQ( identified.messages(0).execute().user_pk(), 9u );
		EXPECT_EQ( identified.messages(0).execute().transmission(), "payload" );
	}

	TEST( FromServerTests, ExecuteResponse ){
		let t = FromServer::Execute( string{"result"}, 5 );
		ASSERT_EQ( t.messages(0).value_case(), SMessage::kExecuteResponse );
		EXPECT_EQ( t.messages(0).execute_response(), "result" );
		EXPECT_EQ( t.messages(0).request_id(), 5u );
	}

	TEST( FromServerTests, GraphQLAndSubscription ){
		let query = FromServer::GraphQL( string{R"({"users":[]})"}, 6 );
		ASSERT_EQ( query.messages(0).value_case(), SMessage::kQueryResult );
		EXPECT_EQ( query.messages(0).query_result(), R"({"users":[]})" );
		EXPECT_EQ( query.messages(0).request_id(), 6u );

		let sub = FromServer::Subscription( string{R"({"users":[]})"}, 7 );
		ASSERT_EQ( sub.messages(0).value_case(), SMessage::kSubscription );
		EXPECT_EQ( sub.messages(0).subscription(), R"({"users":[]})" );
	}

	TEST( FromServerTests, SubscriptionAck ){
		let t = FromServer::SubscriptionAck( flat_set<QL::SubscriptionId>{3,1,2}, 8 );
		ASSERT_EQ( t.messages(0).value_case(), SMessage::kSubscriptionAck );
		EXPECT_EQ( t.messages(0).request_id(), 8u );
		let& ids = t.messages( 0 ).subscription_ack().server_ids();
		EXPECT_EQ( Protobuf::ToVector(ids), (vector<uint32_t>{1,2,3}) ); //flat_set - sorted, deduped.
	}

	TEST( FromServerTests, QueryClient ){
		let variables = ms<jobject>( jobject{{"id",42}} );
		let t = FromServer::QueryClient( string{"{users{id}}"}, variables, UserPK{9}, true, 6 );
		ASSERT_EQ( t.messages(0).value_case(), SMessage::kClientQuery );
		EXPECT_EQ( t.messages(0).request_id(), 6u );
		EXPECT_EQ( t.messages(0).client_query().query(), "{users{id}}" );
		EXPECT_EQ( t.messages(0).client_query().variables(), serialize(*variables) );
		EXPECT_TRUE( t.messages(0).client_query().raw() );
		EXPECT_EQ( t.messages(0).client_query().executer_pk(), 9u );

		let none = FromServer::QueryClient( string{"{users{id}}"}, nullptr, UserPK{}, false, 7 );
		EXPECT_TRUE( none.messages(0).client_query().variables().empty() ); //no variables object -> the field is left unset, not "null".
		EXPECT_FALSE( none.messages(0).client_query().raw() );
	}

	//#8: the client used to PopTask for *every* incoming kind before switching on it - i.e. it treated all eighteen as
	//responses.  The nine below that are pushes carry the *server's* request ids, from a counter that has nothing to do
	//with the client's, so each one erased and dropped whichever await of the client's shared that number.  The pop is
	//gated on this classifier now, and the EXPECT_FALSE half is the regression: those nine must never touch the task map.
	//The builders above are what mint these kinds, which is why the classification is pinned next to them.
	TEST( FromServerTests, IsResponse ){
		using enum SMessage::ValueCase;
		for( let kind : {kConnectionInfo, kException, kJwt, kQueryResult, kSessionInfo, kSubscriptionAck} )
			EXPECT_TRUE( FromServer::IsResponse(kind) ) << "value_case " << (int)kind << " answers a client request";
		for( let kind : {kAck, kClientQuery, kExecute, kExecuteAnonymous, kExecuteResponse, kSubscription, kTraces, VALUE_NOT_SET} )
			EXPECT_FALSE( FromServer::IsResponse(kind) ) << "value_case " << (int)kind << " is a server push - popping a task for it drops an unrelated await";
	}

	TEST( ProtoUtilsTests, ToQuery ){
		let query = ProtoUtils::ToQuery( string{"{users{id}}"}, jobject{{"id",42}}, false );
		EXPECT_EQ( query.text(), "{users{id}}" );
		EXPECT_EQ( query.variables(), serialize(jobject{{"id",42}}) );
		EXPECT_FALSE( query.return_raw() );
	}
}
