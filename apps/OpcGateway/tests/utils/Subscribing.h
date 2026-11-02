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
	//The OpcServer revises nothing, so the stored create response is swapped for one that says what a test needs.  The original
	//goes back whatever happens:  the suite shares the client, and a later test would see the swap.
	struct RevisedInterval final{
		RevisedInterval( sp<UAClient> client, double revisedPublishingInterval )ε:
			_client{ move(client) },
			_original{ _client->CreatedSubscriptionResponse() }{
			THROW_IF( !_original, "the subscribe stored no create response" );
			auto revised = ms<UA_CreateSubscriptionResponse>();
			UA_CreateSubscriptionResponse_init( revised.get() );
			revised->subscriptionId = _original->subscriptionId;//SubscriptionId() still names the live one - the unsubscribe's delete needs it.
			revised->revisedPublishingInterval = revisedPublishingInterval;
			_client->SetCreatedSubscriptionResponse( move(revised) );
		}
		RevisedInterval( const RevisedInterval& )=delete;
		~RevisedInterval(){ _client->SetCreatedSubscriptionResponse( _original ); }
	private:
		sp<UAClient> _client;
		sp<UA_CreateSubscriptionResponse> _original;
	};
}