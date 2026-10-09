#include <jde/app/proto/app.FromServer.h>
#include <jde/app/proto/common.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/ql/types/Subscription.h>
#include <jde/web/Jwt.h>
#include <jde/web/client/proto/Web.FromServer.pb.h>

#define let const auto

namespace Jde::App::FromServer{
	//AppClientSocketSession::ProcessTransmission used to PopTask for every kind ahead of its switch, and nine of them never
	//look at the handle - so a server-originated push (kClientQuery, kExecute, kSubscription, kTraces, ...) erased and
	//dropped the handle of whichever request of the client's happened to share that id.  The ids do collide: the client
	//allocates from a process-global counter and the server from a per-session one, both starting at 1, and
	//Server::QuerySessions fans a kClientQuery out to every app client.  The dropped coroutine is never resumed and never
	//failed, and the 60s watchdog cannot rescue it - AddTimeout starts with `if( !HasTask(requestId) ) co_return;` and
	//PopTask has already erased the entry - so the caller waits out the process.
	//Every enumerator is listed and there is no default, so adding a kind is a -Wswitch error rather than a silent
	//classification; when in doubt a kind is a push, which fails safe (an unanswered request still trips the watchdog).
	α IsResponse( Proto::FromServer::Message::ValueCase kind )ι->bool{
		using enum Proto::FromServer::Message::ValueCase;
		switch( kind ){
		case kConnectionInfo: case kException: case kJwt: case kQueryResult: case kSessionInfo: case kSubscriptionAck:
			return true;
		case kAck: case kClientQuery: case kExecute: case kExecuteAnonymous: case kExecuteResponse:
		case kSubscription: case kTraces: case VALUE_NOT_SET:
			return false;
		}
		return false;
	}

	Ω setMessage( RequestId requestId, auto&& set )ι->Proto::FromServer::Transmission{ return ProtoUtils::SingleMessage<Proto::FromServer::Transmission>( requestId, FWD(set) ); }
}
namespace Jde::App{
	α FromServer::Ack( uint32 serverSocketId )ι->Proto::FromServer::Transmission{
		return setMessage( 0, [&](auto& m){ m.set_ack( serverSocketId ); } );//answers no request; a proto3 zero is the same bytes as unset.
	}

	α FromServer::Complete( RequestId requestId )ι->Proto::FromServer::Transmission{
		return setMessage( requestId, [](auto&){} );
	}
	α FromServer::ConnectionInfo( ProgramPK appPK, ProgInstPK instancePK, ConnectionPK connectionPK, RequestId clientRequestId, const Crypto::PublicKey& appServerPubKey, Web::FromServer::SessionInfo&& session, optional<bool> authResult )ι->Proto::FromServer::Transmission{
		return setMessage( clientRequestId, [&](auto& m){
			auto& info = *m.mutable_connection_info();
			info.set_app_pk( appPK );
			info.set_instance_pk( instancePK );
			info.set_connection_pk( connectionPK );
			info.set_certificate_modulus( {appServerPubKey.Modulus.begin(), appServerPubKey.Modulus.end()} );
			info.set_certificate_exponent( {appServerPubKey.Exponent.begin(), appServerPubKey.Exponent.end()} );
			if( authResult )
				info.set_auth_result( *authResult );
			*info.mutable_session_info() = move( session );
			TRACET( ELogTags::Test, "Connected. UserPK='{}'", info.session_info().user_pk() );
		});
	}

	α FromServer::Exception( const runtime_error& e, optional<RequestId> requestId )ι->Proto::FromServer::Transmission{
		return setMessage( requestId.value_or(0), [&](auto& m){ *m.mutable_exception() = ProtoUtils::ToException( e ); } );
	}
	α FromServer::Exception( string&& e, optional<RequestId> requestId )ι->Proto::FromServer::Transmission{
		return setMessage( requestId.value_or(0), [&](auto& m){
			auto& proto = *m.mutable_exception();
			proto.set_what( move(e) );
		});
	}
	α FromServer::Jwt( Web::Jwt&& jwt, RequestId requestId )ι->Proto::FromServer::Transmission{
		return setMessage( requestId, [&](auto& m){
			m.set_jwt( jwt.Payload() );
		});
	}

	α FromServer::LogSubscription( ProgramPK appPK, ProgInstPK instancePK, const Logging::Entry& m, const QL::Subscription& sub )ι->Proto::FromServer::Transmission{
		return setMessage( sub.Id, [&](auto& msg){
			auto& traces = *msg.mutable_traces();
			traces.set_app_id( appPK );
			auto proto = traces.add_values();
			proto->set_id( sub.Id );
			proto->set_instance_id( instancePK );
			*proto->mutable_time() = Protobuf::ToTimestamp( m.Time );
			proto->set_level( (Log::Proto::ELogLevel)m.Level );
			proto->set_message_id( Protobuf::ToBytes(m.Id()) );
			proto->set_file_id( Protobuf::ToBytes(m.FileId()) );
			proto->set_function_id( Protobuf::ToBytes(m.FunctionId()) );
			proto->set_line( m.Line );
			proto->set_user_pk( m.UserPK.Value );
			for( let& arg : m.Arguments )
				proto->add_args( arg );
		});
	}

	α FromServer::QueryClient( string&& query, sp<jobject> variables, Jde::UserPK executer, bool raw, RequestId requestId )ι->Proto::FromServer::Transmission{
		return setMessage( requestId, [&](auto& m){
			auto& clientQuery = *m.mutable_client_query();
			clientQuery.set_query( move(query) );
			clientQuery.set_executer_pk( executer.Value );//.Value: UserPK converts to bool implicitly, so the raw struct sets 1 for every non-zero user.
			clientQuery.set_raw( raw );
			if( variables )
				*clientQuery.mutable_variables() = serialize(*variables);
		});
	}
	α FromServer::SubscriptionAck( flat_set<QL::SubscriptionId>&& subscriptionIds, RequestId requestId )ι->Proto::FromServer::Transmission{
		return setMessage( requestId, [&](auto& m){
			auto& ack = *m.mutable_subscription_ack();
			for_each( subscriptionIds, [&](auto id){ack.add_server_ids(id);} );
		});
	}
	α FromServer::Subscription( string&& s, RequestId requestId )ι->Proto::FromServer::Transmission{
		return setMessage( requestId, [&](auto& m){
			*m.mutable_subscription() = move( s );
		});
	}

	α FromServer::ExecuteRequest( RequestId serverRequestId, UserPK userPK, string&& fromClient )ι->Proto::FromServer::Transmission{
		return setMessage( serverRequestId, [&](auto& m){
			if( userPK ){
				auto& customExecute = *m.mutable_execute();
				customExecute.set_user_pk( userPK.Value );//.Value: UserPK converts to bool implicitly, so the raw struct sets 1 for every non-zero user.
				*customExecute.mutable_transmission() = move( fromClient );
			}
			else
				m.set_execute_anonymous( move(fromClient) );
		});
	}

	α FromServer::Execute( string&& executionResult, RequestId clientRequestId )ι->Proto::FromServer::Transmission{
		return setMessage( clientRequestId, [&](auto& m){ m.set_execute_response( move(executionResult) ); } );
	}

	α FromServer::GraphQL( string&& queryResults, RequestId requestId )ι->Proto::FromServer::Transmission{
		return setMessage( requestId, [&](auto& m){
			m.set_query_result( move(queryResults) );
		});
	}
	α FromServer::Session( Web::FromServer::SessionInfo&& session, RequestId requestId )->Proto::FromServer::Transmission{
		return setMessage( requestId, [&](auto& m){
			*m.mutable_session_info() = move( session );
		});
	}
}