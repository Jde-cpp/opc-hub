#include "Subscribing.h"
#include <jde/fwk/utils/Stopwatch.h>
#include "../../src/async/Subscriptions.h"
#include "helpers.h"

#define let const auto

namespace Jde::Opc::Gateway{
	α Tests::SubscribeOn( const sp<UAClient>& client, sp<IDataChange> listener, flat_set<NodeId> nodes )ε->FromServer::SubscriptionAck{
		BlockVoidAwait( SubscribeAwait{client} );
		return BlockTAwait<FromServer::SubscriptionAck>( DataChangeAwait{move(nodes), move(listener), client} );
	}
	α Tests::SubscribeCounting( const sp<UAClient>& client, const sp<PushCount>& listener, flat_set<NodeId> nodes, Duration timeout )ε->FromServer::SubscriptionAck{
		auto ack = SubscribeOn( client, listener, move(nodes) );
		Stopwatch sw;
		while( !listener->Pushes )
			sw.CheckTimeout( timeout, 1ms );
		return ack;
	}
	α Tests::WaitRevived( Jde::Handle lost, sv slug, Duration timeout )ι->sp<UAClient>{
		sp<UAClient> y;
		WaitUntil( [&]{
			for( let& client : UAClient::LiveClients() ){//Try:  MonitoredNodes() would build every live client this walks a UAMonitoringNodes it never asked for.
				let nodes = client->Handle()!=lost && client->Slug()==slug ? client->TryMonitoredNodes() : nullptr;
				if( nodes && nodes->Count() )
					y = client;
			}
			return y!=nullptr;
		}, timeout );
		return y;
	}
}