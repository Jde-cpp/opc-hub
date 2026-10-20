#pragma once
#include <boost/beast/ssl.hpp>
#include <absl/synchronization/mutex.h>
#include <jde/web/usings.h>
#include <jde/web/client/socket/IClientSocketSession.h>
#include <jde/web/client/socket/ClientSocketAwait.h>
#include <jde/opc/uatypes/ExNodeId.h>
#include <jde/opc/uatypes/NodeId.h>
#include "Opc.FromClient.pb.h"
#include "Opc.FromServer.pb.h"
#include <jde/app/proto/Common.pb.h>

namespace Jde::Opc::Gateway::Tests{
	struct IListener{
		β OnData( string opcId, NodeId nodeId, const Proto::Value& value )ι->void=0;
		β OnPush( const FromServer::NodeValues& /*push*/ )ι->void{}//the whole push, before OnData:  its quality and timestamps too
	};
	//A historian subscription's listener (spec *Subscriptions*), as the web's will be:  a HistSubscribe's ack resumes its
	//await, and what follows it, the HistSnapshot pages and then the HistRecords pushes, comes here under the HistSubscribe's
	//request id.
	struct IHistListener{
		β OnSnapshot( const FromServer::HistSnapshot& page )ι->void=0;
		β OnRecords( const FromServer::HistRecords& records )ι->void=0;
		β OnAck( const FromServer::HistSubscribeAck& /*ack*/ )ι->void{}//one no await took:  HistSubscribeUnsubscribe's.
		β OnError( string /*what*/ )ι->void{}//the gateway ended the subscription:  its group removed, or the historian stopping.
	};
	//An error the gateway *answered* with - it came back over a working socket, unlike a transport failure.  Anything that reacts
	//to a dead connection must not react to these: the gateway is fine and it is the thing behind it that failed (an OPC server
	//that is down, a rejected write).  The soak's reconnect-on-failure did, and killed itself trying to re-subscribe to a server
	//that was still down (soak-findings #12).  Move/Throw keep the type across BlockAwait's rethrow.
	//Only OnRead's kException path produces one; CloseTasks fails tasks with a CodeException, so a dead socket never reads
	//as an answer (subscription-disconnect #1).
	struct GatewayErrorResponse final : Exception{
		using Exception::Exception;
		α Move()ι->up<Exception> override{ return mu<GatewayErrorResponse>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
	};
	struct GatewayClientSocket final : Web::Client::TClientSocketSession<FromClient::Transmission,FromServer::Transmission>{
		using base = Web::Client::TClientSocketSession<FromClient::Transmission,FromServer::Transmission>;
		Τ using await = Web::Client::ClientSocketAwait<T>;
		GatewayClientSocket( sp<net::io_context> ioc, optional<ssl::context>& ctx )ι;
		//RemoveShutdown pairs Connect's AddShutdown: it registers a raw `this`, and nothing else takes it back out, so a
		//session that dies with the test left Process::Shutdown a dangling IShutdown* to call at exit (the same pairing
		//IAppClient and RemoteLog spell out).  Latent until the client stopped waiting out every request's full timeout -
		//that wait was holding these sessions alive to the end of the process (emulator-review #11).
		~GatewayClientSocket(){ Process::RemoveShutdown( this ); TRACET(ELogTags::Test, "GatewayClientSocket::~GatewayClientSocket"); }

		//A socket to the gateway at host:port, bound to `session` when given.  `path` is the gateway protocol's on a hub, which a
		//standalone gateway ignores, so a socket opened here runs against either.
		Ω Open( string host, PortType port, optional<SessionPK> session, string path="/opc" )ε->sp<GatewayClientSocket>;
		α Connect( SessionPK sessionId, SRCE )ι->await<uint32>;
		α Query( string&& query, jobject variables, bool returnRaw, SRCE )ι->await<jvalue> override;
		α QuerySync( string&& query, jobject variables={}, bool returnRaw=true )ε->jvalue;
		α Subscribe( ServerCnnctnNK slug, const vector<NodeId>& nodes, sp<IListener> listener, SRCE )ε->await<FromServer::SubscriptionAck>;
		α Subscribe( string&& /*query*/, jobject /*variables*/, sp<QL::IListener> /*listener*/, SL )ε->await<jarray> override{ ASSERT(false); throw "noimpl"; }
		α Unsubscribe( vector<QL::SubscriptionId>&&, SL )ι->void override{ ASSERT(false); }

		α Unsubscribe( ServerCnnctnNK slug, const vector<NodeId>& nodeIds, SRCE )ε->await<FromServer::UnsubscribeAck>;
		α PendingSubscriptionRecords()Ι->uint;//this socket's subscribe + unsubscribe requests still awaiting their ack - a failed request may leave none (m2-closing #15).
		//The ack's statuses, one per node.  The listener hears the subscription's pages and pushes until HistUnsubscribe's ack,
		//or the exception that ends it;  any after either is a stray, which HistStrays counts.
		α HistSubscribe( uint32 group, const vector<ExNodeId>& nodes, optional<TimePoint> from, sp<IHistListener> listener, SRCE )ι->await<vector<StatusCode>>;
		α HistUnsubscribe( RequestId subscription, SRCE )ι->await<bool>;
		α LastHistSubscription()Ι->RequestId{ return _lastHistSubscription; }//the request id the last HistSubscribe sent, which names it to HistUnsubscribe.
		//The HistSubscribe twice in one transmission, under one request id:  the gateway takes one and refuses the other, which
		//HistRefusals counts instead of failing the await or dropping the listener.
		α HistSubscribeTwice( uint32 group, const vector<ExNodeId>& nodes, optional<TimePoint> from, sp<IHistListener> listener, SRCE )ι->await<vector<StatusCode>>;
		α HistRefusals()Ι->uint{ lg _{ _histMutex }; return _histRefusals; }
		α LastHistRefusal()Ι->string{ lg _{ _histMutex }; return _lastHistRefusal; }
		α HistStrays()Ι->uint{ lg _{ _histMutex }; return _histStrays; }
		//A HistSubscribe and its HistUnsubscribe in one transmission, so the unsubscribe lands while the subscription registers.
		//The await is the unsubscribe's, and the HistSubscribe's ack goes to the listener's OnAck, which hears on after it.
		α HistSubscribeUnsubscribe( uint32 group, const vector<ExNodeId>& nodes, optional<TimePoint> from, sp<IHistListener> listener, SRCE )ι->await<bool>;
	private:
		α registerHist( RequestId subscription, sp<IHistListener> listener, bool duplicate=false )ι->void;//its listener, under the id that names the subscription, and the last one sent.
		α forgetRequest( RequestId requestId )ι->void;
		α onSubscriptionAck( RequestId requestId, const FromServer::SubscriptionAck& result )ι->StatusCode;
		α onUnsubscribeAck( RequestId requestId )ι->void;//the acked nodes' listeners go with the subscription - see _unsubscribeRequests.
		α onNodeValues( FromServer::NodeValues&& nodeValues )ι->void;
		α CloseTasks( beast::error_code ec )ι->void override;
		α OnRead( FromServer::Transmission&& transmission )ι->void override;
		α OnAck( uint32 ack )ι->void;
		mutable std::mutex _histMutex;
		flat_map<RequestId,sp<IHistListener>> _histListeners;
		RequestId _lastHistSubscription{};
		flat_set<RequestId> _histDuplicates;
		uint _histRefusals{};
		string _lastHistRefusal;
		flat_map<RequestId,RequestId> _histUnsubscribes;//a HistUnsubscribe's request id, to the subscription it ends.
		flat_set<RequestId> _histEnded;
		uint _histStrays{};
		//The opc side, per socket as the hist side is:  a push is this socket's session's, so only this socket's listeners hear it.
		//A Subscribe/Unsubscribe registers its record before it is written, and the ack was the only thing that took it out - so
		//every request that *failed* left its record, listener and all, for the life of the process:  answered with an error, the
		//socket dead, a write on a closed stream, a timeout.  subscription-disconnect #7's per-cycle re-subscribe made that one
		//per second for the length of an outage, in the harness that scores the soak (reviews/m2-closing.md #15).  Two ways out
		//now besides the ack:  forgetRequest, by id, for an error the gateway answered;  and CloseTasks, for a dead socket's.
		mutable absl::Mutex _subscriptionRequestMutex;
		flat_map<RequestId, tuple<ServerCnnctnNK, vector<NodeId>, sp<IListener>>> _subscriptionRequests ABSL_GUARDED_BY(_subscriptionRequestMutex);
		flat_map<RequestId, tuple<ServerCnnctnNK, vector<NodeId>>> _unsubscribeRequests ABSL_GUARDED_BY(_subscriptionRequestMutex);//The ack prunes the nodes' listeners: they used to stay registered forever, so a later Subscribe on the same node through the same socket dispatched pushes to a listener whose fixture was long destroyed.
		absl::Mutex _subscriptionsMutex;
		flat_map<ServerCnnctnNK, flat_map<NodeId, flat_set<sp<IListener>>>> _subscriptions ABSL_GUARDED_BY(_subscriptionsMutex);
	};
}