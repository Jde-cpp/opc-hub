#include "MonitoredItems.h"
#include <open62541/client_subscriptions.h>
#include "../UAClient.h"
#include "../types/UAClientException.h"
#include "../usings.h"

#define let const auto

namespace Jde::Opc::Gateway{
	DeleteMonitoredItemsAwait::DeleteMonitoredItemsAwait( flat_map<SubscriptionId,flat_set<MonitorId>> monitoredItems, sp<UAClient> client, SL sl )ι:
		VoidAwait{sl},
		_client{ move(client) },
		_monitoredItems{ move(monitoredItems) }
	{}

	Ω monitoredItemsDeleteCallback( UA_Client*, void* userdata, RequestId requestId, UA_DeleteMonitoredItemsResponse* response )ι->void{
		DeleteMonitoredItemsAwait& await = *(DeleteMonitoredItemsAwait*)userdata;
		await.OnComplete( *response, requestId );
	}

	α DeleteMonitoredItemsAwait::Suspend()ι->void{
		_client->PostUA( [this]{ SuspendOnStrand(); } );//UA submissions must run on the client's strand.
	}
	α DeleteMonitoredItemsAwait::SuspendOnStrand()ι->void{
		try{
			for( let& [subscriptionId, monitorIds] : _monitoredItems ){
				UA_DeleteMonitoredItemsRequest request{
					.subscriptionId = subscriptionId,
					.monitoredItemIdsSize = monitorIds.size(),
					.monitoredItemIds = ( UA_UInt32* )UA_Array_new( monitorIds.size(), &UA_TYPES[UA_TYPES_UINT32] )
				};
				uint i=0;
				for( let& id : monitorIds )
					request.monitoredItemIds[i++] = id;

				RequestId _requestId{};//name required by the UACε macro (it references _requestId).
				UACε( UA_Client_MonitoredItems_delete_async(_client->UAPointer(), request, monitoredItemsDeleteCallback, this, &_requestId) );
				UA_DeleteMonitoredItemsRequest_clear( &request );
				TRACE( "[{}.{}]Deleting monitored items for subscriptionId={}, count={}", hex(_client->Handle()), hex(_requestId), subscriptionId, monitorIds.size() );
				bool first;
				{ lg l{ _mutex }; first = _submitted++==0; }
				if( first )
					_client->Process( _requestId, "MonitoredItems_delete" );
			}
		}
		catch( runtime_error& e ){
			//_submitted has no concurrent writer here (the loop has exited); if nothing was accepted, no callback will fire, so propagate like the original. Otherwise the in-flight deletes complete and TryResume() resumes normally.
			WARN( "[{}]Delete monitored items: {}/{} submitted before error: {}", hex(_client->Handle()), _submitted, _monitoredItems.size(), e.what() );
			if( !_submitted ){
				ResumeExp( move(e) );
				return;
			}
		}
		{ lg l{ _mutex }; _submissionsComplete = true; }
		TryResume();
	}
	α DeleteMonitoredItemsAwait::TryResume()ι->void{
		{
			lg l{ _mutex };
			if( _resumed || !_submissionsComplete || _finished.size()!=_submitted )
				return;
			_resumed = true;
		}
		Resume();//outside the lock: may synchronously resume+destroy this. Do not touch members after.
	}
	α DeleteMonitoredItemsAwait::OnComplete( UA_DeleteMonitoredItemsResponse& response, RequestId requestId )ι->void{
		_client->ClearRequest( requestId );
		TRACE( "[{}.{}]MonitoredItemsDeleteCallback", hex(_client->Handle()), hex(requestId) );
		if( let sc = response.responseHeader.serviceResult; sc )
			WARN( "[{}.{}]Could not delete monitored items:  {}.", hex(_client->Handle()), hex(requestId), UAException::Message(sc) );
    for( auto sc : Iterable<UA_StatusCode>(response.results, response.resultsSize) ){
			if( sc )
				WARN( "[{}.{}]Could not delete monitored item:  {}.", hex(_client->Handle()), hex(requestId), UAException::Message(sc) );
		}
		UA_DeleteMonitoredItemsResponse_clear( &response );
		{ lg l{ _mutex }; _finished.emplace( requestId ); }
		TryResume();
	}
	ModifyMonitoredItemsAwait::ModifyMonitoredItemsAwait( flat_map<NodeId,MonitoringParameters> nodes, sp<UAClient> client, UA_TimestampsToReturn timestamps, SL sl )ι:
		base{ sl },
		_nodes{ move(nodes) },
		_client{ move(client) },
		_timestamps{ timestamps }
	{}

	Ω monitoredItemsModifyCallback( UA_Client*, void* userdata, RequestId requestId, UA_ModifyMonitoredItemsResponse* response )ι->void{
		ModifyMonitoredItemsAwait& await = *(ModifyMonitoredItemsAwait*)userdata;
		await.OnComplete( *response, requestId );
	}

	α ModifyMonitoredItemsAwait::Suspend()ι->void{
		_client->PostUA( [this]{ SuspendOnStrand(); } );//UA submissions must run on the client's strand.
	}
	α ModifyMonitoredItemsAwait::SuspendOnStrand()ι->void{
		flat_set<NodeId> asked;
		for( let& [node,_] : _nodes )
			asked.emplace( node );
		let handles = _client->MonitoredNodes().Handles( asked );
		flat_map<SubscriptionId,vector<std::pair<NodeId,MonitorId>>> bySubscription;
		for( let& node : asked ){
			if( auto p = handles.find(node); p!=handles.end() )
				bySubscription[p->second.SubId()].emplace_back( node, p->second.MonitorId() );
			else
				_results.try_emplace( node, MonitoredItemModifyResult{UA_STATUSCODE_BADMONITOREDITEMIDINVALID} );
		}
		for( let& [subscriptionId, items] : bySubscription ){
			vector<NodeId> nodes;
			UA_ModifyMonitoredItemsRequest request;
			UA_ModifyMonitoredItemsRequest_init( &request );
			request.subscriptionId = subscriptionId;
			request.timestampsToReturn = _timestamps;
			request.itemsToModifySize = items.size();
			request.itemsToModify = (UA_MonitoredItemModifyRequest*)UA_Array_new( items.size(), &UA_TYPES[UA_TYPES_MONITOREDITEMMODIFYREQUEST] );
			for( uint i=0; i<items.size(); ++i ){
				let& [node, monitorId] = items[i];
				let& parameters = _nodes.find( node )->second;
				auto& item = request.itemsToModify[i];
				item.monitoredItemId = monitorId;//open62541 assigns the clientHandle itself (MonitoredItems_prepareModify).
				item.requestedParameters.samplingInterval = parameters.SamplingInterval;
				item.requestedParameters.queueSize = parameters.QueueSize;
				item.requestedParameters.discardOldest = parameters.DiscardOldest;
				nodes.push_back( node );
			}
			RequestId _requestId{};//name required by the UACε macro.
			try{
				UACε( UA_Client_MonitoredItems_modify_async(_client->UAPointer(), request, monitoredItemsModifyCallback, this, &_requestId) );
			}
			catch( UAException& e ){//this request's nodes fail with the refusal;  the next subscription's still go, a refusal that means the connection went included - it fails the same way.
				UA_ModifyMonitoredItemsRequest_clear( &request );
				WARN( "[{}]Modify monitored items on subscription {} refused for {} node(s):  {}", hex(_client->Handle()), subscriptionId, nodes.size(), e.what() );
				for( let& node : nodes )
					_results.try_emplace( node, MonitoredItemModifyResult{(StatusCode)e.Code()} );
				continue;
			}
			UA_ModifyMonitoredItemsRequest_clear( &request );
			TRACE( "[{}.{}]Modifying monitored items for subscriptionId={}, count={}", hex(_client->Handle()), hex(_requestId), subscriptionId, nodes.size() );
			_requests.emplace( _requestId, move(nodes) );//before Process:  it can run the loop inline, and the answer reads this.
			bool first;
			{ lg l{ _mutex }; first = _submitted++==0; }
			if( first )
				_client->Process( _requestId, "MonitoredItems_modify" );
		}
		{ lg l{ _mutex }; _submissionsComplete = true; }
		TryResume();
	}
	α ModifyMonitoredItemsAwait::TryResume()ι->void{
		{
			lg l{ _mutex };
			if( _resumed || !_submissionsComplete || _finished!=_submitted )
				return;
			_resumed = true;
		}
		Resume( move(_results) );//outside the lock:  may synchronously resume+destroy this.  Do not touch members after.
	}
	α ModifyMonitoredItemsAwait::OnComplete( UA_ModifyMonitoredItemsResponse& response, RequestId requestId )ι->void{
		_client->ClearRequest( requestId );
		let sc = response.responseHeader.serviceResult;
		TRACE( "[{}.{}]MonitoredItemsModifyCallback:  {}", hex(_client->Handle()), hex(requestId), UAException::Message(sc) );
		auto nodes = _requests.find( requestId );
		if( nodes==_requests.end() ){//braced:  the log macros expand to a bare `if`.
			CRITICAL( "[{}.{}]Modify answered a request nothing submitted.", hex(_client->Handle()), hex(requestId) );
		}
		else{
			let answered = !sc && response.resultsSize==nodes->second.size();//a mismatched count is open62541's own BadInternalError, read as a refusal of each.
			if( !answered )
				WARN( "[{}.{}]Could not modify {} monitored item(s):  {}.", hex(_client->Handle()), hex(requestId), nodes->second.size(), sc ? UAException::Message(sc) : "result count mismatch" );
			for( uint i=0; i<nodes->second.size(); ++i ){
				let& node = nodes->second[i];
				MonitoredItemModifyResult result{ answered ? response.results[i].statusCode : sc ? sc : UA_STATUSCODE_BADINTERNALERROR };
				if( answered && !result.Status ){
					result.RevisedSamplingInterval = response.results[i].revisedSamplingInterval;
					result.RevisedQueueSize = response.results[i].revisedQueueSize;
					_client->MonitoredNodes().Modified( node, result.RevisedSamplingInterval, result.RevisedQueueSize );
				}
				else if( answered )
					WARN( "[{}.{}]Could not modify monitored item for '{}':  {}.", hex(_client->Handle()), hex(requestId), node.ToString(), UAException::Message(result.Status) );
				_results.insert_or_assign( node, result );
			}
			_requests.erase( nodes );
		}
		{ lg l{ _mutex }; ++_finished; }
		TryResume();
	}
}