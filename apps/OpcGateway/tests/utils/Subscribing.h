#pragma once
//A test's own listener, subscribed on a UAClient directly rather than through a socket, and the client a reconnect revives.
#include "../../src/UAClient.h"

namespace Jde::Opc::Gateway::Tests{
	//Counts its data changes.  A listener that does more overrides SendDataChange and counts as well.
	struct PushCount : IDataChange{
		virtual ~PushCount()=default;
		α SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value& )ι->void override{ ++Pushes; }
		α to_string()Ι->string override{ return "Tests.PushCount"; }
		atomic<uint> Pushes;
	};
	//The client's subscription, then the create for `nodes`:  the create's ack.
	α SubscribeOn( const sp<UAClient>& client, sp<IDataChange> listener, flat_set<NodeId> nodes )ε->FromServer::SubscriptionAck;
	//SubscribeOn, then the initial push:  the item is live when this returns.
	α SubscribeCounting( const sp<UAClient>& client, const sp<PushCount>& listener, flat_set<NodeId> nodes, Duration timeout=6s )ε->FromServer::SubscriptionAck;
	//The client a reconnect brings up for `slug` in place of `lost`, once it monitors a node again;  null after `timeout`.
	α WaitRevived( Jde::Handle lost, sv slug, Duration timeout=30s )ι->sp<UAClient>;
}