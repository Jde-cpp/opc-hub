#include "DataChanges.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/opc/uatypes/Value.h>
#include "../UAClient.h"
#include "../types/UAClientException.h"
#include "Subscriptions.h"
#include "../uatypes/CreateMonitoredItemsRequest.h"
#include "../types/proto/opc.Common.h"

#define let const auto

namespace Jde::Opc::Gateway{
	static ELogTags _tags{ (ELogTags)(EOpcLogTags::Opc | EOpcLogTags::Monitoring) };
	Ω createDataChangesCallback( UA_Client*, void *userdata, RequestId, UA_CreateMonitoredItemsResponse* response )ι->void{
		DataChangeAwait& await = *(DataChangeAwait*)userdata;
		await.OnComplete( response );
	}

	Ω dataChangesCallback( UA_Client* ua, SubscriptionId subId, void* /*subContext*/, MonitorId monId, void* /*monContext*/, UA_DataValue* uaValue )->void{
		auto pClient = UAClient::TryFind(ua); if(!pClient) return;
		Value value{ move(*uaValue) };
		let h = MonitorHandle{ subId, monId };
		TRACET( DataChangesTag, "[{}.{}] DataChangesCallback - {}", hex((uint)ua), hex((Handle)h), serialize(value.ToJson()) );
		if( !pClient->MonitoredNodes().SendDataChange(h, move(value)) )
			DBGT( DataChangesTag, "[{}.{}]Could not find node monitored item.", hex((uint)ua), hex((Handle)MonitorHandle{subId, monId}) );
	}

	Ω dataChangesDeleteCallback( UA_Client* ua, SubscriptionId subId, void* /*_subContext_*/, MonitorId monId, void* /*_monContext_*/ )->void{
		TRACE( "[{}.{}]DataChangesDeleteCallback", hex((uint)ua), hex((Handle)MonitorHandle{subId, monId}) );
	}

	α DataChangeAwait::Suspend()ι->void{
		_client->PostUA( [this]{ Submit(); } );//UA submissions must run on the client's strand.
	}
	α DataChangeAwait::Submit()ι->void{
		auto request = _client->MonitoredNodes().MonitoredItemsRequest( sp<IDataChange>{_dataChange}, flat_set<NodeId>{_nodes}, _monitoredRequestId );//copies:  a redrive asks again.
		if( !_monitoredRequestId ){
			//No subscription for the new nodes:  the one the caller's SubscribeAwait found was deleted before this reached the strand -
			//DeleteMonitoring's pass, a second after another listener's last item went.  That failed the subscribe with BadInternalError
			//for every node; build a subscription and ask once more instead (subscription-disconnect #6).  Once:  a second miss is not
			//that race.
			if( !_redriven ){
				_redriven = true;
				Redrive();
			}
			else
				ResumeExp( Exception{"CreatedSubscriptionResponse==null"} );
			return;
		}
		if( !request ){
			_h.resume();
			return;
		}
		//The id the request registered under, not a fresh read:  MonitoredItemsRequest captured it under the same lock
		//DeleteMonitoring decides on, so this is the subscription that cannot be deleted out from under these items (#11).
		let subscriptionId = MonitorHandle{ _monitoredRequestId }.SubId();
		try{
			vector<UA_Client_DeleteMonitoredItemCallback> deleteCallbacks{ request->itemsToCreateSize, dataChangesDeleteCallback };
			vector<UA_Client_DataChangeNotificationCallback> dataChangeCallbacks{ request->itemsToCreateSize, dataChangesCallback };
			void** contexts = nullptr;
			request->subscriptionId = subscriptionId;

			//UACε, as every other submission:  this one used UAε, so a create refused because the connection had gone never reached
			//RemoveIfDisconnected - BadServerNotConnected included - and a rebuild read the refusal on a client still `Connected` as
			//the server's, dropping its listeners (reviews/m2-closing.md #2).  The take behind it moves this request to _takenCalls;
			//GetResult still answers every node with the refusal.
			UACε( UA_Client_MonitoredItems_createDataChanges_async(_client->UAPointer(), *request, contexts, dataChangeCallbacks.data(), deleteCallbacks.data(), createDataChangesCallback, this, &_requestId) );
			UA_CreateMonitoredItemsRequest_clear( &*request );
			//TRACET( MonitoringTag, "[{:x}.{:x}]DataSubscriptions - {}", Handle(), requestId, serialize(request.ToJson()) );
			_client->Process( _requestId, "MonitoredItems_createDataChanges" );//TODO handle BadSubscriptionIdInvalid
		}
		catch( UAException& e ){
			ResumeExp( move(e) );
		}
	}
	//Any: this awaitable's own task type is TAwait<SubscriptionAck>, and the subscribe is a VoidAwait.
	α DataChangeAwait::Redrive()ι->VoidTask{
		try{
			co_await Any( SubscribeAwait{_client} );
			_client->PostUA( [this]{ Submit(); } );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );//last use of this:  resuming the caller ends the awaitable.
		}
	}
	α DataChangeAwait::OnComplete( UA_CreateMonitoredItemsResponse* response )ι->void{
		_client->ClearRequest( _requestId );
		let sc = response->responseHeader.serviceResult;
		TRACE( "[{}.{}]CreateDataChangesCallback: {}", hex(_client->Handle()), hex(_requestId), UAException::Message(sc) );
		if( sc )
			ResumeExp( UAClientException{sc, _client->Handle(), _requestId} );//the request's bookkeeping goes in GetResult, which await_resume calls.
		else{
			_client->MonitoredNodes().OnCreateResponse( response, _monitoredRequestId );
			_h.resume();
		}
	}
	α DataChangeAwait::await_resume()ι->FromServer::SubscriptionAck{
		StatusCode sc{};
		if( up<Exception> e = Promise() && Promise()->Exp() ? Promise()->MoveExp() : nullptr; e )
			sc = e->HasCode() ? (StatusCode)e->Code() : UA_STATUSCODE_BADINTERNALERROR;
		if( !_monitoredRequestId ){//never registered - no subscription even after the redrive - so GetResult has nothing to answer from:  fail each node here.
			FromServer::SubscriptionAck y;
			for( let& node : _nodes ){
				auto result = y.add_results();
				result->set_status_code( sc ? sc : UA_STATUSCODE_BADINTERNALERROR );
				*result->mutable_node() = ProtoUtils::ToNodeId( node );//as GetResult names its results (reviews/m3-closing.md #10)
			}
			return y;
		}
		return FromServer::SubscriptionAck{ _client->MonitoredNodes().GetResult(_monitoredRequestId, sc) };
	}
}
