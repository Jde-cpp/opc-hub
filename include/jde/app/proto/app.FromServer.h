#pragma once
#include <jde/app/usings.h>
#include <jde/ql/usings.h>
#include <jde/web/client/proto/Web.FromServer.pb.h>
#include <stdexcept>


namespace Jde::Crypto{ struct PublicKey; }
namespace Jde::QL{ struct Subscription; }
namespace Jde::Web{ struct Jwt; }
namespace Jde::App::Proto::FromClient { class Status; }
namespace Jde::App::FromServer{
	//Whether a message answers a request the *client* made - i.e. whether its request_id indexes the client's pending-task
	//map at all.  Everything else is a server-originated push stamped with the server's own request id, which is a
	//different id space entirely.  See the definition for why the client may not touch its tasks for those.
	α IsResponse( Proto::FromServer::Message::ValueCase kind )ι->bool;
	α Ack( uint32 serverSocketId )ι->Proto::FromServer::Transmission;
	α Complete( RequestId requestId )ι->Proto::FromServer::Transmission;
	α ConnectionInfo( ProgramPK appPK, ProgInstPK instancePK, ConnectionPK connectionPK, RequestId clientRequestId, const Crypto::PublicKey& appServerPublicKey, Web::FromServer::SessionInfo&& session, optional<bool> authResult )ι->Proto::FromServer::Transmission;
	α Exception( const runtime_error& e, optional<RequestId> requestId )ι->Proto::FromServer::Transmission;
	α Exception( string&& e, optional<RequestId> requestId )ι->Proto::FromServer::Transmission;
	α Execute( string&& executionResult, RequestId clientRequestId )ι->Proto::FromServer::Transmission;
	α ExecuteRequest( RequestId serverRequestId, UserPK userPK, string&& fromClient )ι->Proto::FromServer::Transmission;
	α GraphQL( string&& queryResults, RequestId requestId )ι->Proto::FromServer::Transmission;
	α Jwt( Web::Jwt&& jwt, RequestId requestId )ι->Proto::FromServer::Transmission;
	α LogSubscription( ProgramPK appPK, ProgInstPK instancePK, const Logging::Entry& e, const QL::Subscription& sub )ι->Proto::FromServer::Transmission;
	α QueryClient( string&& query, sp<jobject> variables, Jde::UserPK executer, bool raw, RequestId requestId )ι->Proto::FromServer::Transmission;
	α Session( Web::FromServer::SessionInfo&& session, RequestId requestId )->Proto::FromServer::Transmission;
	α SubscriptionAck( flat_set<QL::SubscriptionId>&& subscriptionIds, RequestId requestId )ι->Proto::FromServer::Transmission;
	α Subscription( string&& s, RequestId requestId )ι->Proto::FromServer::Transmission;
}