#pragma once
#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/utils/HiLow.h>
#include "../uatypes/MonitoredItemCreateResult.h"
#include "../async/DataChanges.h"
#include "../async/MonitoredItems.h"
#include "../async/Subscriptions.h"
#include "../usings.h"

namespace Jde::Opc{ struct Value; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	struct CreateMonitoredItemsRequest;

	//A listener's ask on a node's item that the server refused, put back to the last one it granted (418-watch-items #9):  a rebuild
	//re-creates the item with what the listener asks then, so a refused ask kept would be refused again and the node lost.
	struct AskReverted final{ MonitoringParameters Refused; MonitoringParameters Restored; };
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
		//What this listener asks of a node's item - per node on the connection named, since one session listens on every connection
		//it subscribes to, so the historian samples at each node's MinTimeInterval and a watch list's row carries its own - and
		//with the interval the client's subscription runs at (UAClient::PublishingInterval), which sizes a queue.  Asked at every
		//create, a rebuild's included, at every join of an item that exists, and again at ParametersChanged.  A node several
		//listeners watch has one item, which takes the strictest of what they ask (UAMonitoringNodes, spec *Watch list*):  a
		//listener that asks a deadband on a node another watches unfiltered gets the unfiltered stream.  The collector's clients
		//are its own (Credential::Collector), so its items take its parameters alone.  Called with the monitoring lock held
		//exclusively - on the client's strand from a create, on the caller's from ParametersChanged - as SendDataChange is with it
		//shared:  neither may call into UAClient or its monitored nodes, nor wait on a lock that is held across such a call - an
		//unsubscribe under the listener's own lock would deadlock the strand.
		β ItemParameters( const ServerCnnctnNK& /*opcId*/, const NodeId&, Duration /*publishingInterval*/ )Ι->MonitoringParameters{ return {}; }
		//The interval the subscription runs at, told at every request this listener registers on one - before any ItemParameters.
		//Called as ItemParameters is.
		β RunsAt( Duration /*publishingInterval*/ )ι->void{}
		//The node's item was modified - for another listener's join or leave, or this one's ParametersChanged - to what the server
		//revised it to.  Or the server refused the modify:  the status says why, the item stands as it was and the revised values
		//are what it still runs at, and the next join, leave or ask tries again.  Every listener of the node hears it, the one whose
		//join caused it included, since its ack answered before the modify did.  A refused ask still this listener's own is put back
		//to the last the server granted it - `reverted` - which ItemParameters answers from here, unless it has asked again since.
		//Called off the monitoring lock, on the client's strand, as ConnectionLost is.
		β ItemModified( const ServerCnnctnNK& /*opcId*/, const NodeId&, const MonitoredItemModifyResult&, const optional<AskReverted>& /*reverted*/ )ι->void{}
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
		//The listener's ItemParameters changed for these nodes - a threshold edit (HistGroupHost::SetNodeThresholds) - so each
		//item of its is asked again and the merge recomputed:  the one modify path a join and a leave take (spec *Watch list*).
		//A create of its still out is asked again too, and its answer modifies the item it attaches to when the ask changed since
		//the create was sent (418-watch-items #7).  A node with neither is skipped:  its next create asks ItemParameters.
		α ParametersChanged( const sp<IDataChange>& listener, const flat_set<NodeId>& nodes )ι->void;
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
		//An item:  its node, the server's answer with the revised values, each listener with what it asked (ItemParameters), and
		//what the item was last asked for - the merge of those - which a join, a leave or a ParametersChanged compares its new
		//merge to, so a server's revision never looks like a change to ask again.  None once the server refused a modify:  what
		//it holds is unknown here, so the next merge asks whatever it is.  Granted:  each listener's ask the server last accepted
		//- the create's, one a Good modify carried, or every ask when a merge changes nothing and no modify is out (Pending) -
		//which a refused modify puts back (418-watch-items #9).
		struct Change{ MonitoringParameters Merged; flat_map<sp<IDataChange>,MonitoringParameters> Asks; };//a modify posted, and the asks it merged.
		struct Subscription{
			Subscription( NodeId node, MonitoredItemCreateResult result, sp<IDataChange> listener, MonitoringParameters parameters )ι: Node{ move(node) }, Result{ move(result) }, Requested{ parameters }{ Listeners.emplace( listener, parameters ); Granted.emplace( move(listener), move(parameters) ); }
			NodeId Node;
			MonitoredItemCreateResult Result;
			flat_map<sp<IDataChange>,MonitoringParameters> Listeners;
			optional<MonitoringParameters> Requested;
			flat_map<sp<IDataChange>,MonitoringParameters> Granted;
			uint Pending{};//modifies posted and not yet answered.
			//The merge recomputed after a listener joined, left or changed its ask:  the parameters to modify the item to when they
			//differ from Requested, which they replace, with the asks merged.  None means the item stands.
			α Remerge()ι->optional<Change>;
		};
		//A create in flight:  the new nodes asked for - in the order the server answers them, so never edited - the listener, the
		//parameters it asked per node, which the create sent, the nodes that listener unsubscribed while the create was out
		//(either Unsubscribe), which its answer must not attach, and those it asked again meanwhile (ParametersChanged), which
		//its answer attaches instead of what was sent.
		struct Call{ flat_set<NodeId> Nodes; sp<IDataChange> Listener; flat_map<NodeId,MonitoringParameters> Parameters; flat_set<NodeId> Cancelled; flat_map<NodeId,MonitoringParameters> Refreshed; };
		α GetClient()ι->sp<UAClient>;
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α FindNode( const NodeId& node )ι->tuple<MonitorHandle,Subscription*>;//the pointer is good until the next insert into _subscriptions.
		α DeleteMonitoring( sp<UAClient> ua, Handle uaHandle, flat_map<SubscriptionId,flat_set<MonitorId>> requested )ι->VoidTask;//sp: it has to outlive the wait, and it owns `this`.
		//The ModifyMonitoredItems for items whose merge changed, posted onto the strand - never run under _mutex, nor inline in the
		//strand handler that found the change:  a submission can run the processing loop in place.  Each listener of an item asked
		//hears ItemModified, off the lock, a refusal included.  sp, as DeleteMonitoring takes it:  the client owns `this`.
		α Modify( sp<UAClient> client, flat_map<NodeId,Change> nodes )ι->void;
		α Modifying( sp<UAClient> client, flat_map<NodeId,Change> changes )ι->ModifyMonitoredItemsAwait::Task;

		atomic<RequestId> _requestId{};
		flat_map<MonitorHandle,flat_set<NodeId>> _requests ABSL_GUARDED_BY(_mutex);
		flat_map<MonitorHandle,Call> _calls ABSL_GUARDED_BY(_mutex);
		flat_map<MonitorHandle,flat_map<NodeId,StatusCode>> _errors ABSL_GUARDED_BY(_mutex);
		flat_set<MonitorHandle> _takenCalls ABSL_GUARDED_BY(_mutex);//in-flight creates TakeForResubscribe dropped from _calls - their late answers are expected, not errors.  Until GetResult.
		absl::Mutex _mutex;
		flat_map<MonitorHandle,Subscription> _subscriptions ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<NodeId,MonitorHandle> _byNode ABSL_GUARDED_BY(_mutex);//FindNode's index into _subscriptions - kept in step with it.  Lookups only:  its order changes run to run.
		wp<UAClient> _client;
	};
}