#include "GatewaySocketSession.h"
#include <jde/app/proto/Common.pb.h>
#include <jde/app/client/IAppClient.h> //!important
#include "GatewayAppClient.h"
#include "UAClient.h"
#include "WebServer.h"
#include "async/Subscriptions.h"
#include "async/DataChanges.h"
#include "ql/GatewayQLAwait.h"
#include "types/proto/opc.Common.h"
#include "types/proto/opc.FromServer.h"

#define let const auto

namespace Jde::Opc::Gateway{
	GatewaySocketSession::GatewaySocketSession( sp<IRestStream> stream, beast::flat_buffer&& buffer, TRequestType&& request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι:
		base{ move(stream), move(buffer), move(request), move(userEndpoint), connectionIndex }
	{}

	α GatewaySocketSession::OnClose()ι->void{
		if( !Stream )
			return;
		LogRead( "OnClose", 0 );
		UAClient::Unsubscribe( SharedFromThis() );
		Server::RemoveSession( Id() );
		base::OnClose();
	}

	α GatewaySocketSession::SendAck( uint32 id )ι->void{
		LogWrite( Ƒ("Ack id: {:x}", id), 0 );
		Write( FromServer::AckTrans(id) );
	}

	α GatewaySocketSession::SendDataChange( const ServerCnnctnNK& opcNK, const NodeId& node, const Value& value )ι->void{
		return Write( MessageTrans(FromServer::ToProto(opcNK, node, value, 0), 0) );
	}

	α GatewaySocketSession::SetSessionId( str strSessionId, RequestId requestId )->Sessions::UpsertAwait::Task{
		LogRead( Ƒ("sessionId: '{}'", strSessionId), requestId );
		try{
			let sessionInfo = co_await Sessions::UpsertAwait( strSessionId, _userEndpoint.address().to_string(), true, AppClient() );
			base::SetSessionInfo( move(sessionInfo) );
			Write( FromServer::CompleteTrans(requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}

	α GatewaySocketSession::Subscribe( ServerCnnctnNK&& opcId, flat_set<NodeId> nodes, uint32 requestId )ι->TAwait<sp<UAClient>>::Task{
		try{
			auto self = SharedFromThis(); //keep alive
			LogRead( Ƒ("({:x})Subscribe: opcId: '{}', nodeCount: {}", base::SessionId(), opcId, nodes.size()), requestId );
			if( !Session() ){//a client can send kSubscribe with no preceding kSessionId; *Session() would be a null deref.
				WriteException( Exception{"Send sessionId before subscribing."}, requestId );
				co_return;
			}
			auto client = co_await ConnectAwait( string{opcId}, *Session() );
			if( client )
				CreateSubscription( move(client), move(nodes), requestId );
			else
				WriteException( Ƒ("Client not found: opcId: '{}'", move(opcId)), requestId, SRCE_CUR );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}

	Ω subscribe( sp<UAClient> client, flat_set<NodeId> nodes, RequestId requestId, sp<GatewaySocketSession> session )ι->TAwait<FromServer::SubscriptionAck>::Task{
		try{
			auto ack = co_await DataChangeAwait{ nodes, session, client };
			session->Write( FromServer::SubscribeAckTrans(move(ack), requestId) );
			[]( flat_set<NodeId>&& nodes, sp<UAClient> client, RequestId requestId, sp<GatewaySocketSession> session )->TAwait<flat_map<NodeId, Value>>::Task {
				try{
					auto values = co_await ReadValueAwait{ move(nodes), client };
					session->Write( FromServer::ReadValuesTrans(client->Slug(), move(values), requestId) );
				}
				catch( runtime_error& e ){
					session->WriteException( move(e), requestId );
				}
			}( move(nodes), move(client), requestId, move(session) );
		}
		catch( runtime_error& e ){
			session->WriteException( move(e), requestId );
		}
	}
	α GatewaySocketSession::CreateSubscription( sp<UAClient> client, flat_set<NodeId> nodes, RequestId requestId )ι->VoidAwait::Task{
		try{
			auto self = SharedFromThis(); //keep alive
			co_await SubscribeAwait{ client };
			subscribe( move(client), move(nodes), requestId, move(self) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}

	α GatewaySocketSession::GraphQL( Jde::Proto::Query&& proto, uint requestId )ι->TAwait<jvalue>::Task{
		jobject vars;
		try{
			if( auto p = proto.mutable_variables(); p->size() )
				vars = parse( move(*p) ).as_object();
			auto ql = QL::Parse( move(*proto.mutable_text()), move(vars), Schemas(), proto.return_raw() );
			auto v = co_await QL::QLAwait<>{ move(ql), {Session()}, Gateway::QLPtr() };
			Write( FromServer::QueryTrans(serialize(move(v)), requestId) );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
			co_return;
		}
	}
	α GatewaySocketSession::Unsubscribe( ServerCnnctnNK&& opcId, flat_set<NodeId> nodes, uint32 requestId )ι->void {
		try{
			auto self = SharedFromThis();//keep alive
			LogRead( Ƒ("Unsubscribe: opcId: '{}', nodeCount: {}", opcId, nodes.size()), requestId );
			//By what this session monitors, not by the credential Subscribe connected with:  the cached credential can be
			//gone (a logout) or a different one (a login on the session after the subscribe), and either miss stranded the
			//subscription as "Client not found" (soak-findings #5).  A session can only ever drop its own items, so every
			//live client on the slug is asked and the nodes none of them held for this session are the failures.
			flat_set<NodeId> successes, remaining{ nodes };
			bool anyClient{};
			for( let& client : UAClient::LiveClients() ){
				if( client->Slug()!=opcId )
					continue;
				anyClient = true;
				if( auto p = remaining.size() ? client->TryMonitoredNodes() : nullptr; p ){
					auto [dropped, _] = p->Unsubscribe( flat_set<NodeId>{remaining}, self );
					for( let& node : dropped ){
						successes.emplace( node );
						remaining.erase( node );
					}
				}
			}
			//Then what is parked for a reconnect (soak-findings #10):  while the server is down these nodes belong to no live
			//client, so without this they are reported as failures and the reconnect re-subscribes what was just dropped.
			if( remaining.size() ){
				let dropped = UAClient::UnsubscribePending( opcId, self, remaining );
				for( let& node : dropped ){
					anyClient = true;//it is this session's subscription either way - answering "Client not found" for it would be a lie.
					successes.emplace( node );
					remaining.erase( node );
				}
			}
			if( anyClient )
				Write( FromServer::UnsubscribeTrans(requestId, move(successes), move(remaining)) );
			else
				WriteException( Ƒ("Client not found: opcId: '{}'", opcId), requestId, SRCE_CUR );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}
	α GatewaySocketSession::WriteSubscription( const jvalue& /*j*/, Jde::RequestId /*requestId*/ )ι->void{
		ASSERT_DESC( false, "Not Implemented" );
	}
	α GatewaySocketSession::WriteSubscriptionAck( flat_set<QL::SubscriptionId>&& /*subscriptionIds*/, Jde::RequestId /*requestId*/ )ι->void{
		ASSERT_DESC( false, "Not Implemented" );
	}
	α GatewaySocketSession::WriteComplete( Jde::RequestId /*requestId*/ )ι->void{
		ASSERT_DESC( false, "Not Implemented" );
	}

	α GatewaySocketSession::WriteException( runtime_error&& e, Jde::RequestId requestId, SL sl )ι->void{
		LogWriteException( e, requestId, ELogLevel::Debug, sl );
		Write( FromServer::ExceptionTrans(move(e), requestId) );
	}
	α GatewaySocketSession::WriteException( string&& e, Jde::RequestId requestId, SL sl )ι->void{
		LogWriteException( move(e), requestId, ELogLevel::Debug, sl );
		Write( FromServer::ExceptionTrans(Exception(move(e)), requestId) );
	}

	α GatewaySocketSession::OnRead( FromClient::Transmission&& transmission )ι->void{
		for( auto i=0; i<transmission.messages_size(); ++i ){
			auto& m = *transmission.mutable_messages( i );
			let requestId = m.request_id();

			switch( m.Value_case() ){
			using enum FromClient::Message::ValueCase;
			case kSessionId:
				SetSessionId( m.session_id(), requestId );
				break;
			case kSubscribe:{
				auto& s = *m.mutable_subscribe();
				Subscribe( move(*s.mutable_opc_id()), ProtoUtils::ToNodeIds(move(*s.mutable_nodes())), requestId );
				break;}
			case kQuery:
				GraphQL( move(*m.mutable_query()), requestId );
				break;
			case kUnsubscribe:{
				auto& u = *m.mutable_unsubscribe();
				Unsubscribe( move(*u.mutable_opc_id()), ProtoUtils::ToNodeIds(move(*u.mutable_nodes())), requestId );
				break;}
			default:
				LogRead( Ƒ("Unknown message type '{}'", underlying(m.Value_case())), requestId, ELogLevel::Critical );
				WriteException( Exception("({})Message not implemented.", m.Value_case()), requestId );
			}
		}
	}
}