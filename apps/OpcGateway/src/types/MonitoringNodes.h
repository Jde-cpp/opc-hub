#pragma once
#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/utils/HiLow.h>
#include "../uatypes/MonitoredItemCreateResult.h"
#include "../async/DataChanges.h"
#include "../async/Subscriptions.h"
#include "../usings.h"

namespace Jde::Opc{ struct Value; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	struct CreateMonitoredItemsRequest;

	struct IDataChange{
		β SendDataChange( const ServerCnnctnNK& opcId, const NodeId& node, const Value& value )ι->void=0;
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
		//What a session-loss rebuild has to re-create: the live node→listener map, taken *out* - every handle in here names a
		//subscription the server dropped with the session, so nothing keyed by one can be reused.  See UAClient::Resubscribe.
		//In-flight creates are not taken:  their requests stay, so each resumes with a per-node failure (see the definition).
		α TakeForResubscribe()ι->flat_map<sp<IDataChange>,flat_set<NodeId>>;
		α Count()ι->uint{ rl _{_mutex}; return _subscriptions.size(); }
	private:
		struct Subscription{
			Subscription( /*ServerCnnctnNK opcId,*/ NodeId node, MonitoredItemCreateResult result, sp<IDataChange> clientCall )ι: /*ServerCnnctnNK{move(opcId)},*/ Node{ move(node) }, Result{ move(result) }, ClientCalls{ move(clientCall) }{}
			NodeId Node;
			MonitoredItemCreateResult Result;
			flat_set<sp<IDataChange>> ClientCalls;
		};
		α GetClient()ι->sp<UAClient>;
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α FindNode( const NodeId& node )ι->tuple<MonitorHandle,Subscription*>;//the pointer is good until the next insert into _subscriptions.
		α DeleteMonitoring( sp<UAClient> ua, Handle uaHandle, flat_map<SubscriptionId,flat_set<MonitorId>> requested )ι->DurationTimer::Task;//sp: it has to outlive the wait, and it owns `this`.

		atomic<RequestId> _requestId{};
		flat_map<MonitorHandle,flat_set<NodeId>> _requests ABSL_GUARDED_BY(_mutex);
		//the creates in flight:  the new nodes asked for - in the order the server answers them, so never edited - the listener, and the
		//nodes that listener unsubscribed while the create was out (the by-node Unsubscribe), which its answer must not attach.
		flat_map<MonitorHandle,tuple<flat_set<NodeId>,sp<IDataChange>,flat_set<NodeId>>> _calls ABSL_GUARDED_BY(_mutex);
		flat_map<MonitorHandle,flat_map<NodeId,StatusCode>> _errors ABSL_GUARDED_BY(_mutex);
		flat_set<MonitorHandle> _takenCalls ABSL_GUARDED_BY(_mutex);//in-flight creates TakeForResubscribe dropped from _calls - their late answers are expected, not errors.  Until GetResult.
		absl::Mutex _mutex;
		flat_map<MonitorHandle,Subscription> _subscriptions ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<NodeId,MonitorHandle> _byNode ABSL_GUARDED_BY(_mutex);//FindNode's index into _subscriptions - kept in step with it.  Lookups only:  its order changes run to run.
		wp<UAClient> _client;
	};
}