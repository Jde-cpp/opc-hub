#pragma once
#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/utils/HiLow.h>
#include "../uatypes/MonitoredItemCreateResult.h"
#include "../uatypes/MonitoringParameters.h"
#include "../async/DataChanges.h"
#include "../async/Subscriptions.h"
#include "../usings.h"

namespace Jde::Opc{ struct Value; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	struct CreateMonitoredItemsRequest;

	struct IDataChange{
		β SendDataChange( const ServerCnnctnNK& opcId, const NodeId& node, const Value& value )ι->void=0;
		//The subscription these nodes were on is gone - the connection failed, or the server dropped the session or the
		//subscription (SubscriptionStatusChanged) - broken at `when`, the gateway's clock:  when the client first saw its channel
		//or session go (UAClient::RecordBreak), which can be well before the loss is found, else when it was found.  They are
		//parked and put back (UAClient::ConnectionLost, Resubscribe), and ConnectionRestored says when;  a value that arrives
		//between the two was in flight when it broke (spec *Gaps*).  Called off the monitoring lock, on whichever thread found the
		//loss, after the nodes have left the client.
		β ConnectionLost( const ServerCnnctnNK& /*opcId*/, const flat_set<NodeId>& /*nodes*/, TimePoint /*when*/ )ι->void{}
		//The nodes a rebuild put back for this listener - on a live subscription again, called before it delivers their first
		//value, which is what the historian's gap rule needs.  Never for a node the listener dropped meanwhile, and nothing for
		//one the server refused:  the rebuild's log line says so, and the node stays lost.  A listener that has to keep its nodes
		//asks for them again itself, as the historian's collector does once they have been lost too long (HistGroupHost::Check).
		β ConnectionRestored( const ServerCnnctnNK& /*opcId*/, const flat_set<NodeId>& /*nodes*/ )ι->void{}
		//What this listener's items are created with - per node, so the historian samples at each node's MinTimeInterval, and
		//with the interval the client's subscription runs at (UAClient::PublishingInterval), which sizes a queue.  Asked
		//at every create, a rebuild's included;  a change to an item that exists goes through ModifyMonitoredItemsAwait.  A node
		//another listener already monitors keeps that item's parameters - the collector's clients are its own for that reason
		//(Credential::Collector).  Called on the client's strand with the monitoring lock held exclusively (MonitoredItemsRequest),
		//as SendDataChange is with it shared:  neither may call into UAClient or its monitored nodes, nor wait on a lock that is
		//held across such a call - an unsubscribe under the listener's own lock would deadlock the strand.
		β ItemParameters( const NodeId&, Duration /*publishingInterval*/ )Ι->MonitoringParameters{ return {}; }
		//The interval the subscription runs at, told at every request this listener registers on one - before any ItemParameters, and
		//also when every node joins an existing item, which asks none.  Called as ItemParameters is.
		β RunsAt( Duration /*publishingInterval*/ )ι->void{}
		β to_string()Ι->string=0;
	};

	struct MonitorHandle final : HiLow{
		MonitorHandle( SubscriptionId s, MonitorId m )ι:HiLow{ s, m }{}
		MonitorHandle( Handle x )ι:HiLow{ x }{}
		α SubId()Ι->SubscriptionId{ return Hi(); }
		α MonitorId()Ι->MonitorId{ return Low(); }
	};


	struct UAMonitoringNodes final{
		UAMonitoringNodes(sp<UAClient> p)ι:_client{p}{}
		~UAMonitoringNodes(){_client.reset();}
		[[nodiscard]] α Shutdown( SRCE )ι->UnsubscribeAwait;
		α MonitoredItemsRequest( sp<IDataChange>&& dataChange, flat_set<NodeId>&& nodes, Handle& requestId )ι->optional<CreateMonitoredItemsRequest>;
		α Unsubscribe( flat_set<NodeId>&& nodes, sp<IDataChange> dataChange )ι->tuple<flat_set<NodeId>,flat_set<NodeId>>;
		α Unsubscribe( sp<IDataChange> )ι->void;
		α SendDataChange( Handle h, const Value&& value )ι->uint;
		α OnCreateResponse( UA_CreateMonitoredItemsResponse* response, Handle requestId )ι->void;
		α GetResult( Handle requestId, StatusCode status )ι->FromServer::SubscriptionAck;
		α Handles( const flat_set<NodeId>& nodes )ι->flat_map<NodeId,MonitorHandle>;//the item each node is monitored on - none for a node that has none.  A snapshot:  good until the next change.
		α Modified( const NodeId& node, double revisedSamplingInterval, uint32 revisedQueueSize )ι->void;//a ModifyMonitoredItems answer:  what GetResult reports as the item's parameters from here.
		//What a session-loss rebuild has to re-create: the live node→listener map, taken *out* - every handle in here names a
		//subscription the server dropped with the session, so nothing keyed by one can be reused.  See UAClient::Resubscribe.
		//In-flight creates are not taken:  their requests stay, so each resumes with a per-node failure (see the definition).
		α TakeForResubscribe()ι->flat_map<sp<IDataChange>,flat_set<NodeId>>;
		α Count()ι->uint{ rl _{_mutex}; return _subscriptions.size(); }
	private:
		struct Subscription{
			Subscription( NodeId node, MonitoredItemCreateResult result, sp<IDataChange> clientCall )ι: Node{ move(node) }, Result{ move(result) }, ClientCalls{ move(clientCall) }{}
			NodeId Node;
			MonitoredItemCreateResult Result;
			flat_set<sp<IDataChange>> ClientCalls;
		};
		α GetClient()ι->sp<UAClient>;
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α FindNode( const NodeId& node )ι->tuple<MonitorHandle,Subscription*>;//the pointer is good until the next insert into _subscriptions.
		α DeleteMonitoring( sp<UAClient> ua, Handle uaHandle, flat_map<SubscriptionId,flat_set<MonitorId>> requested )ι->VoidTask;//sp: it has to outlive the wait, and it owns `this`.

		atomic<RequestId> _requestId{};
		flat_map<MonitorHandle,flat_set<NodeId>> _requests ABSL_GUARDED_BY(_mutex);
		//the creates in flight:  the new nodes asked for - in the order the server answers them, so never edited - the listener, and the
		//nodes that listener unsubscribed while the create was out (either Unsubscribe), which its answer must not attach.
		flat_map<MonitorHandle,tuple<flat_set<NodeId>,sp<IDataChange>,flat_set<NodeId>>> _calls ABSL_GUARDED_BY(_mutex);
		flat_map<MonitorHandle,flat_map<NodeId,StatusCode>> _errors ABSL_GUARDED_BY(_mutex);
		flat_set<MonitorHandle> _takenCalls ABSL_GUARDED_BY(_mutex);//in-flight creates TakeForResubscribe dropped from _calls - their late answers are expected, not errors.  Until GetResult.
		absl::Mutex _mutex;
		flat_map<MonitorHandle,Subscription> _subscriptions ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<NodeId,MonitorHandle> _byNode ABSL_GUARDED_BY(_mutex);//FindNode's index into _subscriptions - kept in step with it.  Lookups only:  its order changes run to run.
		wp<UAClient> _client;
	};
}