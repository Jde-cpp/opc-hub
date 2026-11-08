#include "GatewaySocketSession.h"
#include <jde/fwk/utils/collections.h>
#include <jde/app/proto/Common.pb.h>
#include <jde/app/client/IAppClient.h>
#include "GatewayAppClient.h"
#include "UAClient.h"
#include "WebServer.h"
#include "async/Subscriptions.h"
#include "async/DataChanges.h"
#include "hist/HistSubscriber.h"
#include "ql/GatewayQLAwait.h"
#include <jde/opc/proto/opc.Common.h>
#include "types/proto/opc.FromClient.h"
#include "types/proto/opc.FromServer.h"

#define let const auto

namespace Jde::Opc::Gateway{
	GatewaySocketSession::GatewaySocketSession( sp<IRestStream> stream, beast::flat_buffer&& buffer, TRequestType&& request, tcp::endpoint&& userEndpoint, uint32 connectionIndex )ι:
		base{ move(stream), move(buffer), move(request), move(userEndpoint), connectionIndex }
	{}

	α GatewaySocketSession::OnClose()ι->void{
		if( !IsOpen() )
			return;
		LogRead( "OnClose", 0 );
		UAClient::Unsubscribe( SharedFromThis() );
		flat_map<Jde::RequestId,sp<HistSubscriber>> histSubscriptions;
		{
			lg _{ _histMutex };
			_closed = true;
			histSubscriptions = move( _histSubscriptions );
		}
		for( auto&& [_,subscriber] : histSubscriptions )
			subscriber->End();
		Server::RemoveSession( Id() );
		base::OnClose();
	}

	α GatewaySocketSession::SendAck( uint32 id )ι->void{
		LogWrite( Ƒ("Ack id: {:x}", id), 0 );
		Write( FromServer::AckTrans(id) );
	}

	α GatewaySocketSession::SendDataChange( const ServerCnnctnNK& opcNK, const NodeId& node, const Value& value, const ITypes* types )ι->void{
		return Write( MessageTrans(FromServer::ToProto(opcNK, node, value, 0, types), 0) );
	}
	α GatewaySocketSession::SendEvent( const ServerCnnctnNK& opcNK, const NodeId& node, const vector<const UA_Variant*>& fields, const ITypes* types )ι->void{
		Write( FromServer::EventTrans(opcNK, node, fields, types) );
	}
	α GatewaySocketSession::ItemParameters( const ServerCnnctnNK& opcNK, const NodeId& node, Duration publishingInterval )Ι->MonitoringParameters{
		MonitoringParameters y;
		{
			lg _{ _parametersMutex };
			if( auto p = _parameters.find(opcNK); p!=_parameters.end() )
				y = FindDefault( p->second, node );
		}
		if( y.SamplingInterval<0 )//Part 4's "the publishing interval", which the merge would take for the fastest (418-watch-items #4).
			y.SamplingInterval = std::chrono::duration<double,std::milli>{ publishingInterval }.count();
		return y;
	}
	//An ask kept here against the one the map holds:  ItemParameters gives a negative interval as the publishing interval, so that
	//matches whatever interval it was given.
	Ω sameAsk( MonitoringParameters kept, const MonitoringParameters& held )ι->bool{
		if( kept.SamplingInterval<0 )
			kept.SamplingInterval = held.SamplingInterval;
		return kept==held;
	}
	α GatewaySocketSession::ItemModified( const ServerCnnctnNK& opcNK, const NodeId& node, const MonitoredItemModifyResult& result, const optional<AskReverted>& reverted )ι->void{
		if( reverted ){//the refused ask goes, unless a Subscribe since has asked the node again (418-watch-items #9).
			lg _{ _parametersMutex };
			if( auto p = _parameters.find(opcNK); p!=_parameters.end() ){
				if( auto kept = p->second.find(node); kept!=p->second.end() && sameAsk(kept->second, reverted->Refused) )
					kept->second = reverted->Restored;
			}
		}
		LogWrite( Ƒ("MonitoredItemModified: opcId: '{}', node: {}, sampling: {}, queue: {}, status: {:x}", opcNK, node.ToString(), result.RevisedSamplingInterval, result.RevisedQueueSize, result.Status), 0 );
		Write( FromServer::ItemModifiedTrans(opcNK, node, result) );
	}

	α GatewaySocketSession::SetSessionId( str strSessionId, RequestId requestId )->Sessions::UpsertAwait::Task{
		LogRead( Ƒ("sessionId: '{}'", strSessionId), requestId );
		try{
			let sessionInfo = co_await Sessions::UpsertAwait( strSessionId, _userEndpoint.address().to_string(), true, AppClient() );
			base::SetSessionInfo( move(sessionInfo) );
			WriteComplete( requestId );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}

	α GatewaySocketSession::Subscribe( ServerCnnctnNK opcId, flat_map<NodeId,MonitoringParameters> asked, uint32 requestId )ι->TAwait<sp<UAClient>>::Task{
		try{
			auto self = SharedFromThis(); //keep alive
			LogRead( Ƒ("({:x})Subscribe: opcId: '{}', nodeCount: {}", base::SessionId(), opcId, asked.size()), requestId );
			if( !Session() ){//a client can send kSubscribe with no preceding kSessionId; *Session() would be a null deref.
				WriteException( Exception{"Send sessionId before subscribing."}, requestId );
				co_return;
			}
			auto client = co_await ConnectAwait( string{opcId}, *Session() );
			if( !client ){
				WriteException( Ƒ("Client not found: opcId: '{}'", move(opcId)), requestId, SRCE_CUR );
				co_return;
			}
			flat_map<NodeId,optional<MonitoringParameters>> prior;//put back for a node the create refuses (418-watch-items #9).
			{//under the slug the client carries, which ItemParameters is asked by - "" is the default connection's (418-watch-items #6) - and before the create, which asks it on the strand.
				lg _{ _parametersMutex };
				auto& parameters = _parameters[client->Slug()];
				for( auto&& [node,p] : asked ){
					auto kept = parameters.find( node );
					prior.emplace( node, kept==parameters.end() ? nullopt : optional<MonitoringParameters>{kept->second} );
					parameters.insert_or_assign( node, p );
				}
			}
			CreateSubscription( move(client), move(asked), move(prior), requestId );
		}
		catch( runtime_error& e ){
			WriteException( move(e), requestId );
		}
	}

	//One frame:  its own type is SubscribeAwait's, and the create and the first read go through Any().
	α GatewaySocketSession::CreateSubscription( sp<UAClient> client, flat_map<NodeId,MonitoringParameters> asked, flat_map<NodeId,optional<MonitoringParameters>> prior, RequestId requestId )ι->VoidAwait::Task{
		auto self = SharedFromThis(); //keep alive
		flat_set<NodeId> nodes;
		for( let& [node,_] : asked )
			nodes.emplace( node );
		bool acked{};
		try{
			co_await SubscribeAwait{ client };
			DataChangeAwait create{ nodes, self, client };
			auto ack = co_await Any( create );
			acked = true;
			flat_set<NodeId> refused;
			for( let& result : ack.results() ){
				if( result.status_code() )
					refused.emplace( ProtoUtils::ToNodeId(result.node()) );
			}
			Unstore( client->Slug(), refused, asked, prior );
			Write( FromServer::SubscribeAckTrans(move(ack), requestId) );
			flat_set<NodeId> variables;//the first push, of the data-change items alone:  an event item's node is an object, with no Value to read (spec *Events*).
			for( let& [node,parameters] : asked ){
				if( !parameters.IsEvent() )
					variables.emplace( node );
			}
			if( variables.empty() )
				co_return;
			ReadValueAwait read{ move(variables), client, UA_TIMESTAMPSTORETURN_BOTH };//the first push:  its timestamps as a data change's
			auto values = co_await Any( read );
			Write( FromServer::ReadValuesTrans(client->Slug(), move(values), requestId, client->Types().get()) );
		}
		catch( runtime_error& e ){
			if( !acked )
				Unstore( client->Slug(), nodes, asked, prior );
			WriteException( move(e), requestId );
		}
	}
	α GatewaySocketSession::Unstore( const ServerCnnctnNK& slug, const flat_set<NodeId>& refused, const flat_map<NodeId,MonitoringParameters>& asked, const flat_map<NodeId,optional<MonitoringParameters>>& prior )ι->void{
		if( refused.empty() )
			return;
		lg _{ _parametersMutex };
		auto p = _parameters.find( slug );
		if( p==_parameters.end() )
			return;
		for( let& node : refused ){
			auto kept = p->second.find( node );
			auto ask = asked.find( node );
			if( kept==p->second.end() || ask==asked.end() || kept->second!=ask->second )
				continue;//asked again since.
			if( auto before = prior.find(node); before!=prior.end() && before->second )
				kept->second = *before->second;
			else
				p->second.erase( kept );
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
			//Every node, not just what is left:  a node a rebuild's create had out was dropped from that create above, and the
			//rebuild still lists it - left there it counts as refused, and is parked again if the client then dies (m2-closing #6).
			let dropped = UAClient::UnsubscribePending( opcId, self, nodes );
			for( let& node : dropped ){
				anyClient = true;//it is this session's subscription either way - answering "Client not found" for it would be a lie.
				successes.emplace( node );
				remaining.erase( node );
			}
			if( successes.size() ){
				lg _{ _parametersMutex };
				if( auto p = _parameters.find(opcId); p!=_parameters.end() ){
					for( let& node : successes )
						p->second.erase( node );
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
	α GatewaySocketSession::WriteComplete( Jde::RequestId requestId )ι->void{
		Write( FromServer::CompleteTrans(requestId) );
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
				flat_map<NodeId,MonitoringParameters> asked;//every node's checked before any is stored:  one refused refuses the Subscribe.
				try{
					for( int i=0; i<s.nodes_size(); ++i ){
						auto node = ProtoUtils::ToNodeId( s.nodes(i) );
						auto parameters = i<s.parameters_size() ? FromClientUtils::ToParameters( s.parameters(i), node ) : MonitoringParameters{};
						asked.insert_or_assign( move(node), move(parameters) );
					}
				}
				catch( Exception& e ){
					WriteException( move(e), requestId );
					break;
				}
				Subscribe( move(*s.mutable_opc_id()), move(asked), requestId );
				break;}
			case kQuery:
				GraphQL( move(*m.mutable_query()), requestId );
				break;
			case kUnsubscribe:{
				auto& u = *m.mutable_unsubscribe();
				Unsubscribe( move(*u.mutable_opc_id()), ProtoUtils::ToNodeIds(move(*u.mutable_nodes())), requestId );
				break;}
			case kSubscribeEvents:{//an event item on the one node, its clauses the base fields and what `select` names (spec *Events*);  a path refused refuses the message.
				auto& s = *m.mutable_subscribe_events();
				flat_map<NodeId,MonitoringParameters> asked;
				try{
					vector<string> select{ s.select().begin(), s.select().end() };
					asked.emplace( ProtoUtils::ToNodeId(s.node()), MonitoringParameters::ForEvents(EventFilter::Of(select)) );
				}
				catch( Exception& e ){
					WriteException( move(e), requestId );
					break;
				}
				Subscribe( move(*s.mutable_opc_id()), move(asked), requestId );
				break;}
			case kUnsubscribeEvents:{
				auto& u = *m.mutable_unsubscribe_events();
				Unsubscribe( move(*u.mutable_opc_id()), flat_set<NodeId>{ProtoUtils::ToNodeId(u.node())}, requestId );
				break;}
			case kHistSubscribe:
				HistSubscribe( move(*m.mutable_hist_subscribe()), requestId );
				break;
			case kHistUnsubscribe:
				HistUnsubscribe( m.hist_unsubscribe().subscription(), requestId );
				break;
			default:
				LogRead( Ƒ("Unknown message type '{}'", underlying(m.Value_case())), requestId, ELogLevel::Critical );
				WriteException( Exception("({})Message not implemented.", m.Value_case()), requestId );
			}
		}
	}
}