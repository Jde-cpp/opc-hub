#include "GatewayClientSocket.h"
#include <jde/fwk/process/execution.h>
#include <jde/app/proto/common.h>
#include "../../src/GatewayAppClient.h"
#include "../../src/types/proto/opc.Common.h"
#include "../../src/types/proto/opc.FromClient.h"
#include "helpers.h"
#include "jde/fwk/process/process.h"

#define let const auto

namespace Jde::Opc::Gateway{
	sp<Tests::GatewayClientSocket> _session;
	α Tests::Socket()ι->Tests::GatewayClientSocket&{
		if( !_session ){
			optional<ssl::context> ctx;
			_session = ms<Tests::GatewayClientSocket>( Executor(), ctx );
			BlockVoidAwait( _session->RunSession("localhost", GatewayPort(), "/opc") );//the gateway protocol's path on a hub; the standalone gateway ignores it.
			BlockAwait<Web::Client::ClientSocketAwait<uint32>,uint>( _session->Connect(AppClient()->SessionId()) );
		}
		return *_session;
	}
namespace Tests{
	GatewayClientSocket::GatewayClientSocket( sp<net::io_context> ioc, optional<ssl::context>& ctx )ι:
		base{ ioc, ctx }
	{}

	α GatewayClientSocket::OnAck( uint32 serverSocketId )ι->void{
		SetId( serverSocketId );
		INFOT( ELogTags::SocketClientRead, "[{}]{} GatewayClientSocket created: {}.", Id(), IsSsl() ? "Ssl" : "Plain", Host() );
	}

	α GatewayClientSocket::HandleException( std::any&& h, Exception&& e )ι{
		if( auto echo = std::any_cast<await<string>::Handle>(&h) ){
			echo->promise().SetExp( move(e) );//SetExp stores e.Move(), so the caller's GatewayErrorResponse-or-not survives.
			echo->resume();
		}
		else if( auto ack = std::any_cast<await<SessionPK>::Handle>(&h) ){
			ack->promise().SetExp( move(e) );
			ack->resume();
		}
		else if( auto q = std::any_cast<await<jvalue>::Handle>(&h) ){
			q->promise().SetExp( move(e) );
			q->resume();
		}
		else if( auto sub = std::any_cast<await<FromServer::SubscriptionAck>::Handle>(&h) ){
			sub->promise().SetExp( move(e) );
			sub->resume();
		}
		else if( auto unsub = std::any_cast<await<FromServer::UnsubscribeAck>::Handle>(&h) ){
			//the _unsubscribeRequests record stays (no request id here) - harmless, and the listeners must stay: the server still pushes.
			unsub->promise().SetExp( move(e) );
			unsub->resume();
		}
		else
			WARNT( ELogTags::SocketClientRead, "Failed to process incomming exception '{}'.", e.what() );
	}

	α onNodeValues( FromServer::NodeValues&& nodeValues )ι->void;
	α onUnsubscribeAck( RequestId requestId )ι->void;
	α onSubscriptionAck( RequestId requestId, const FromServer::SubscriptionAck& result )ι->StatusCode;
	α GatewayClientSocket::OnRead( FromServer::Transmission&& transmission )ι->void{
		auto size = transmission.messages_size();
		for( auto i=0; i<size; ++i ){
			auto m = transmission.mutable_messages( i );
			using enum FromServer::Message::ValueCase;
			let requestId = m->request_id();
			switch( m->Value_case() ){
			case kAck:
				OnAck( m->ack() );
				break;
			case kNodeValues:
				onNodeValues( move(*m->mutable_node_values()) );
				break;
			case kException:{
				std::any h = requestId==0 ? coroutine_handle<>{} : PopTask( requestId );
				let& e = m->exception();
				HandleException( move(h), GatewayErrorResponse{e.what(), e.code()} );//the one place the gateway answered - see GatewayErrorResponse.
				break;}
			case kQuery:{
				auto h = std::any_cast<await<jvalue>::Handle>( IClientSocketSession::PopTask(requestId) );
				try{
					h.promise().Resume( parse(move(*m->mutable_query())), h );
				}
				catch( runtime_error& e ){
					h.promise().ResumeExp( move(e), h );
				}
				break;}
			case kSubscriptionAck:{
				auto& result = *m->mutable_subscription_ack();
				auto h = std::any_cast<await<FromServer::SubscriptionAck>::Handle>( IClientSocketSession::PopTask(requestId) );
				if( let sc = onSubscriptionAck(requestId, result); sc )
					h.promise().ResumeExp( UAException{sc}, h );
				else
					h.promise().Resume( move(result), h );
				break;}
			case kUnsubscribeAck:{
				onUnsubscribeAck( requestId );
				auto h = std::any_cast<await<FromServer::UnsubscribeAck>::Handle>( IClientSocketSession::PopTask(requestId) );
				h.promise().Resume( move(*m->mutable_unsubscribe_ack()), h );
				break;}
			case VALUE_NOT_SET:{
				auto h = std::any_cast<await<uint32>::Handle>( IClientSocketSession::PopTask(requestId) ); //connect
				h.promise().Resume( Id(), h );
			break;}
			default:
				BREAK;
			}
		}
	}
	α GatewayClientSocket::Connect( SessionPK sessionId, SL sl )ι->await<uint32>{
		Process::AddShutdown( this );
		let requestId = NextRequestId();
		return await<uint32>{ FromClientUtils::Connection(sessionId, requestId), requestId, shared_from_this(), sl };
	}

	flat_map<RequestId, tuple<ServerCnnctnNK, vector<NodeId>, sp<IListener>>> _subscriptionRequests; shared_mutex _subscriptionRequestMutex;
	flat_map<RequestId, tuple<ServerCnnctnNK, vector<NodeId>>> _unsubscribeRequests;//under _subscriptionRequestMutex.  The ack prunes the nodes' listeners: they used to stay registered forever, so a later Subscribe on the same node through the same socket dispatched pushes to a listener whose fixture was long destroyed.
	flat_map<ServerCnnctnNK, flat_map<NodeId, flat_set<sp<IListener>>>> _subscriptions; shared_mutex _subscriptionsMutex;
	α GatewayClientSocket::Query( string&& query, jobject variables, bool returnRaw, SL sl )ι->await<jvalue>{
		let requestId = NextRequestId();
		LOGSL( ELogLevel::Trace, sl, ELogTags::SocketClientWrite, "[{:x}]'{}', variables: {}.", requestId, query, serialize(variables) );
		return await<jvalue>{ FromClientUtils::Query(move(query), move(variables), returnRaw, requestId), requestId, shared_from_this(), sl };
	}
	α GatewayClientSocket::QuerySync( string&& query, jobject variables )ε->jvalue{
		return BlockAwait<await<jvalue>,jvalue>( Query(move(query), move(variables), true) );
	}
	α GatewayClientSocket::Subscribe( ServerCnnctnNK slug, const vector<NodeId>& nodes, sp<IListener> listener, SL sl )ε->await<FromServer::SubscriptionAck>{
		let requestId = NextRequestId();
		LOGSL( ELogLevel::Trace, sl, ELogTags::SocketClientWrite, "[{:x}]Subscribe: '{}'.", requestId, slug );
		ul _{ _subscriptionRequestMutex };
		_subscriptionRequests.emplace( requestId, make_tuple(slug, nodes, move(listener)) );
		return await<FromServer::SubscriptionAck>{ FromClientUtils::Subscription(move(slug), nodes, requestId), requestId, shared_from_this(), sl };
	}

	flat_map<SubscriptionId, sp<IListener>> _logSubscriptions; shared_mutex _logSubscriptionsMutex;
	α GatewayClientSocket::LogSubscribe( jobject&& ql, jobject vars, sp<IListener> listener, SL sl )ε->await<jarray>{
		let requestId = NextRequestId();
		ql["id"] = requestId;
		ul _{ _logSubscriptionsMutex };
		_logSubscriptions.emplace( (uint32)requestId, move(listener) );
		auto query = serialize( ql );
		LOGSL( ELogLevel::Trace, sl, ELogTags::SocketClientWrite, "[{:x}]Subscribe: '{}'.", requestId, query.substr(0, Web::Client::MaxLogLength()) );
		return await<jarray>{ FromClientUtils::Query(move(query), move(vars), true, requestId), requestId, shared_from_this(), sl };
	}
	α GatewayClientSocket::Unsubscribe( ServerCnnctnNK slug, const vector<NodeId>& nodeIds, SL sl )ε->await<FromServer::UnsubscribeAck>{
		let requestId = NextRequestId();
		LOGSL( ELogLevel::Trace, sl, ELogTags::SocketClientWrite, "[{:x}]Unsubscribe: '{}'.", requestId, slug );
		{ ul _{ _subscriptionRequestMutex }; _unsubscribeRequests.emplace( requestId, make_tuple(slug, nodeIds) ); }
		return await<FromServer::UnsubscribeAck>{ FromClientUtils::Unsubscription(move(slug), nodeIds, requestId), requestId, shared_from_this(), sl };
	}
	//the acked nodes' listeners go with the subscription - see _unsubscribeRequests.
	α onUnsubscribeAck( RequestId requestId )ι->void{
		ul _{ _subscriptionRequestMutex };
		auto it = _unsubscribeRequests.find( requestId );
		if( it==_unsubscribeRequests.end() ){
			CRITICALT( ELogTags::SocketClientRead, "[{:x}]No unsubscribe request found.", requestId );
			return;
		}
		auto& [slug, nodes] = it->second;
		{
			ul _2{ _subscriptionsMutex };
			if( auto slugNodes = _subscriptions.find(slug); slugNodes!=_subscriptions.end() ){
				for( let& nodeId : nodes )
					slugNodes->second.erase( nodeId );
			}
		}
		_unsubscribeRequests.erase( it );
	}

	α onSubscriptionAck( RequestId requestId, const FromServer::SubscriptionAck& result )ι->StatusCode{
		ul _{ _subscriptionRequestMutex };
		auto it = _subscriptionRequests.find( requestId );
		if( it==_subscriptionRequests.end() ){
			CRITICALT( ELogTags::SocketClientRead, "[{:x}]No subscription request found.", requestId );
			return UA_STATUSCODE_BADINTERNALERROR;
		}
		auto& [slug, nodes, listener] = it->second;
		ul _2{ _subscriptionsMutex };
		auto& slugNodes = _subscriptions[slug];
		uint resultsSize = result.results_size();
		ASSERT( nodes.size()==resultsSize );
		bool added{};
		StatusCode sc{};
		for( uint i=0; i<std::min(resultsSize, nodes.size()); ++i ){
			let& res = result.results( i );
			auto& nodeId = nodes[i];
			if( res.status_code() ){
				sc = res.status_code();
				DBGT( ELogTags::SocketClientRead, "[{:x}]Subscription for node '{}' failed with status code {}.", requestId, nodeId.ToString(), sc );
			}
			else{
				slugNodes[nodeId].emplace( listener );
				added = true;
			}
		}
		_subscriptionRequests.erase( it );
		return added ? UA_STATUSCODE_GOOD : sc;
	}

	α onNodeValues( FromServer::NodeValues&& nodeValues )ι->void{
		sl _{ _subscriptionsMutex };
		let& opcId = nodeValues.opc_id();
		auto slugNodes = _subscriptions.find( opcId );
		if( slugNodes==_subscriptions.end() ){
			DBGT( ELogTags::SocketClientRead, "No subscriptions for opcId '{}'.", opcId );
			return;
		}
		let nodeId = ProtoUtils::ToNodeId( nodeValues.node() );
		let& listeners = slugNodes->second.find( nodeId );
		if( listeners==slugNodes->second.end() ){
			DBGT( ELogTags::SocketClientRead, "[{},{}]No subscriptions.", opcId, nodeId.ToString() );
			return;
		}
		for( let& listener : listeners->second ){
			listener->OnData( opcId, nodeId, Protobuf::ToVector<FromServer::Value>(move(*nodeValues.mutable_values())) );
		}
	}

	α GatewayClientSocket::CloseTasks( beast::error_code ec )ι->void{
		//A plain Exception, never a GatewayErrorResponse:  every transport failure fails its tasks here - the socket closing,
		//a write on a closed stream, a request timing out (AddTimeout -> CloseOnError) - and the soak's reconnect keys on
		//the difference.  Stamping these as answered made every dead socket reset the soak's failure count, so it never
		//reconnected (subscription-disconnect #1).
		auto f = [this, ec]( std::any&& h )->void {
			let e = App::ProtoUtils::ToException( CodeException{ec, ELogTags::SocketClientWrite, ELogLevel::NoLog} );
			HandleException( move(h), Exception{e.what(), e.code()} );
		};
		base::CloseTasks( f );
	}
	α GatewayClientSocket::OnClose( beast::error_code ec )ι->void{
		base::OnClose( ec );
	}
}}