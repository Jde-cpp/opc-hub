#pragma once

namespace Jde::Opc::Gateway{
	struct UAClient;
	extern Duration _publishingInterval;//what the create asks for, /gateway/subscription/publishingInterval:  set once by Configure, before the web server starts.  The processing loop polls the subscription at the server's revised one, and at this until that arrives.
	α CreateSubscriptionRequest()ι->UA_CreateSubscriptionRequest;//the one data subscription per client:  open62541's defaults but the publishing interval.
	struct SubscribeAwait final : VoidAwait{
		SubscribeAwait( sp<UAClient> c, SRCE )ι:VoidAwait{sl}, _client{move(c)}{}
		α await_ready()ι->bool override;
		α Suspend()ι->void override;
		α OnComplete( UA_CreateSubscriptionResponse& response )ι->void;
	private:
		sp<UAClient> _client;
		RequestId _requestId{};
	};

	α SubscriptionStatusChanged( const sp<UAClient>& client, SubscriptionId subscriptionId, StatusCode status )ι->void;//a StatusChangeNotification, on the strand:  the client's current subscription lost is rebuilt.

	struct UnsubscribeAwait final : VoidAwait{
		UnsubscribeAwait( flat_map<UA_UInt32,flat_set<MonitorId>>&& subscriptions, sp<UAClient> client, SRCE )ι;
		α await_ready()ι->bool override{ return !_client; }
		α Suspend()ι->void override{ RemoveMonitoredItems(); }
		α RemoveMonitoredItems()ι->VoidAwait::Task;
		α Unsubscribe()ι->void;
		α OnComplete( UA_DeleteSubscriptionsResponse& response )ι->void;
	private:
		sp<UAClient> _client;
		RequestId _requestId{};
		flat_map<UA_UInt32,flat_set<MonitorId>> _subscriptions;
	};

}