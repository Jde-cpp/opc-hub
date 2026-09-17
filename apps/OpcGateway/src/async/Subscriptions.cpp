#include "Subscriptions.h"
#include <jde/fwk/process/execution.h>
#include "../UAClient.h"
#include "../types/UAClientException.h"
#include "MonitoredItems.h"

#define let const auto

namespace Jde::Opc::Gateway{
	static ELogTags _tags{ (ELogTags)(EOpcLogTags::Monitoring) };
	Ω statusChangeNotificationCallback( UA_Client* ua, UA_UInt32 subId, void* /*subContext*/, UA_StatusChangeNotification* /*notification*/ )ι->void{
		BREAK;
		TRACE( "[{}.{}]StatusChangeNotificationCallback", hex((uint)ua), subId );
	}

	Ω deleteSubscriptionCallback( UA_Client* ua, UA_UInt32 subId, void* /*subContext*/ )ι->void{
		if( auto client = UAClient::TryFind(ua); client ){
			INFO( "[{}.{}]DeleteSubscriptionCallback", hex(client->Handle()), subId );
			//Only if it is still *this* subscription:  a delete that lands after a rebuild (UAClient::Resubscribe) names the
			//old id, and clearing unconditionally would drop the new subscription every later monitored item is created on.
			//Deliberately NOT a Resubscribe() here, though monitored items surviving this delete are dead by definition (they
			//sit on a subscription the server no longer has).  That state comes from one race - a subscribe landing inside the
			//delete's round trip - and rebuilding from here fixes it only when that subscribe has already *completed*: a create
			//still in flight has its bookkeeping taken out from under it by the rebuild, and its items are stranded just as
			//dead, with a successful-looking ack.  Healing it properly means re-driving in-flight creates too - soak-findings
			//#11, which is where the race is written up.
			if( auto p = client->CreatedSubscriptionResponse(); p && p->subscriptionId==subId )
				client->SetCreatedSubscriptionResponse( nullptr );
		}
	}

	α SubscribeAwait::await_ready()ι->bool{ return _client->CreatedSubscriptionResponse()!=nullptr; }

	static flat_map<sp<UAClient>,vector<SubscribeAwait::Handle>> _requests; static mutex _requestsMutex;
	Ω createSubscriptionCallback( UA_Client*, void* userdata, RequestId, UA_CreateSubscriptionResponse* response )ι->void{
		auto await = (SubscribeAwait*)userdata;
		await->OnComplete( *response );
	}
	α SubscribeAwait::Suspend()ι->void{
		_client->PostUA( [this]{//UA submission must run on the client's strand; _requests is cross-client so it keeps its own mutex.
			try{
				bool first;
				{//must not hold _requestsMutex across Process(): it can run ProcessingLoop inline, and a same-iterate create response re-enters OnComplete's lock on this stack - self-deadlock.
					lg _{ _requestsMutex };
					auto& clientRequests = _requests[_client];
					clientRequests.push_back( _h );
					first = clientRequests.size()==1;
				}
				if( first ){
					UACε( UA_Client_Subscriptions_create_async(_client->UAPointer(), UA_CreateSubscriptionRequest_default(), nullptr, statusChangeNotificationCallback, deleteSubscriptionCallback, createSubscriptionCallback, this, &_requestId) );
					TRACE( "[{}.{}]CreateSubscription", hex(_client->Handle()), hex(_requestId) );
					_client->Process( _requestId, "Subscriptions_create" );
				}
				else
					TRACE( "[{}.{}]CreateSubscription - queued", hex(_client->Handle()), hex(_requestId) );
			}
			catch( runtime_error& e ){
				ResumeExp( move(e) );
			}
		});
	}
	α SubscribeAwait::OnComplete( UA_CreateSubscriptionResponse& response )ι->void{
		_client->ClearRequest( _requestId );
		let sc = response.responseHeader.serviceResult;
		TRACE( "[{}.{}]createSubscriptionCallback - subscriptionId: {}, sc: {}", hex(_client->Handle()), hex(_requestId), response.subscriptionId, hex(sc) );
		if( !sc )
			_client->SetCreatedSubscriptionResponse( ms<UA_CreateSubscriptionResponse>( move(response) ) );
		vector<SubscribeAwait::Handle> handles;
		{//resume outside the lock - a resumed chain can subscribe again (Suspend takes _requestsMutex).
			lg _{ _requestsMutex };
			if( auto clientRequests = _requests.find(_client); clientRequests != _requests.end() ){
				handles = move( clientRequests->second );
				_requests.erase( clientRequests );
			}
		}
		for( auto&& h : handles ){
			if( sc )
				Post( move(h), UAClientException{sc, _client->Handle()} );
			else
				Post( move(h) );
		}
	}

	UnsubscribeAwait::UnsubscribeAwait( flat_map<UA_UInt32,flat_set<MonitorId>>&& subscriptions, sp<UAClient> client, SL sl )ι:
		VoidAwait{sl},
		_client{ move(client) },
		_subscriptions{ move(subscriptions) }{
		_client->StopProcessDataSubscriptions();
	}
	α UnsubscribeAwait::RemoveMonitoredItems()ι->VoidAwait::Task{
		try{
			if( _subscriptions.size() )
				co_await DeleteMonitoredItemsAwait{ _subscriptions, _client };
		}
		catch( runtime_error& e ){
			//A refused submission - no channel, the client is being torn down.  The subscription delete below fails the
			//same way and Resume()s; without this the exception left *this un-resumed, and the coroutine frame holding
			//it - sp<UAClient> included - leaked, the client with it.
			WARN( "[{}]Could not delete the monitored items ({}) - deleting the subscription anyway.", hex(_client->Handle()), e.what() );
		}
		Unsubscribe();
	}
	α UnsubscribeAwait::Unsubscribe()ι->void{
		_client->PostUA( [this]{//UA submission must run on the client's strand.
			UA_DeleteSubscriptionsRequest request{
				.subscriptionIdsSize = _subscriptions.size(),
				.subscriptionIds = ( UA_UInt32* )UA_Array_new( _subscriptions.size(), &UA_TYPES[UA_TYPES_UINT32] )
			};
			uint i=0;
			for( let& [subscriptionId, _] : _subscriptions )
				request.subscriptionIds[i++] = subscriptionId;
			auto onComplete = []( UA_Client*, void* userdata, RequestId, UA_DeleteSubscriptionsResponse* response )ι->void {
				UnsubscribeAwait& await = *(UnsubscribeAwait*)userdata;
				await.OnComplete( *response );
			};
			let sc = UA_Client_Subscriptions_delete_async( _client->UAPointer(), request, onComplete, this, &_requestId );
			UA_DeleteSubscriptionsRequest_clear( &request );
			if( sc ){
				//The async delete never dispatched, so onComplete/Resume() will never fire. This awaiter is co_awaited from UAClient::Shutdown; resume it anyway or shutdown wedges forever.
				WARN( "[{}]Could not delete subscriptions ({}); resuming to avoid wedging shutdown.", hex(_client->Handle()), UAException::Message(sc) );
				Resume();
				return;
			}
			_client->Process( _requestId, "Subscriptions_delete" );
			TRACE( "[{}.{}]Unsubscribe", hex(_client->Handle()), hex(_requestId) );
		});
	}

	α UnsubscribeAwait::OnComplete( UA_DeleteSubscriptionsResponse& response )ι->void{
		_client->ClearRequest( _requestId );
		TRACE( "[{}.{}]UnsubscribeCallback count={}", hex(_client->Handle()), hex(_requestId), _subscriptions.size() );
		if( let sc = response.responseHeader.serviceResult; sc )
			WARN( "[{}.{}]Could not delete subscriptions:  {}.", hex(_client->Handle()), hex(_requestId), UAException::Message(sc) );
		for( auto sc : Iterable<UA_StatusCode>(response.results, response.resultsSize) ){
			if( sc )
				WARN( "[{}.{}]Could not delete subscription:  {}.", hex(_client->Handle()), hex(_requestId), UAException::Message(sc) );
		}
		Resume();
	}

}