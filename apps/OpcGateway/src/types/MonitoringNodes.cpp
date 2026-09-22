#include "MonitoringNodes.h"
#include <jde/fwk/utils/collections.h>
#include "proto/opc.Common.h"
#include "../UAClient.h"
#include "../uatypes/CreateMonitoredItemsRequest.h"
#include "../async/DataChanges.h"
#include "../async/MonitoredItems.h"

#define let const auto

namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ MonitoringTag };
	//α UAMonitoringNodes::LogTag()ι->sp<Jde::LogTag>{ return _logTag; }

	//until orphaned UAClient's go away.
	α UAMonitoringNodes::Shutdown( SL sl )ι->UnsubscribeAwait{
		ul _{ _mutex };
		_requests.clear();
		_calls.clear();
		_errors.clear();
		_takenCalls.clear();

		flat_map<SubscriptionId,flat_set<MonitorId>> monitoredItems;
		for( let& [h,_] : _subscriptions )
			monitoredItems.try_emplace( h.SubId() ).first->second.emplace( h.MonitorId() );
		_subscriptions.clear();
		return UnsubscribeAwait{ move(monitoredItems), _client.lock(), sl };
	}

	α UAMonitoringNodes::FindNode( const NodeId& node )ι->tuple<MonitorHandle,Subscription*>{
		auto p = find_if( _subscriptions, [&node](let& x){ return x.second.Node==node;} );
		return p!=_subscriptions.end() ? make_tuple( p->first, &p->second ) : make_tuple( MonitorHandle{0,0}, nullptr );
	}

	α UAMonitoringNodes::MonitoredItemsRequest( sp<IDataChange>&& dataChange, flat_set<NodeId>&& nodes, Handle& requestId )ι->optional<CreateMonitoredItemsRequest>{
		auto client = _client.lock();
		flat_set<NodeId> newNodes;
		//Only nodes already monitored are found here, not ones whose create is still in flight - a second create for such a node is
		//merged into the first when it answers (OnCreateResponse, subscription-disconnect #15).
		ul lock{ _mutex };
		//Both under the lock, and in this order:  DeleteMonitoring decides whether the subscription may go under this same
		//lock and keeps it if _requests holds anything, so reading the id outside left a window where a delete committed
		//between the read and the registration - and these items were then created on an id the server was about to drop,
		//acked as successful and never pushed (#11).
		let subscriptionId = client->SubscriptionId();
		vector<Subscription*> existing;
		for( let& n : nodes ){
			if( auto pSubscription = get<1>(FindNode(n)); pSubscription )
				existing.push_back( pSubscription );
			else
				newNodes.emplace( n );
		}
		//No subscription to create the new nodes on:  the one the caller's SubscribeAwait found was deleted before this reached the
		//strand (DeleteMonitoring's pass, or a session loss).  Nothing is attached or registered, and requestId 0 tells DataChangeAwait
		//to build a subscription and ask again (subscription-disconnect #6).
		if( newNodes.size() && !subscriptionId ){
			requestId = 0;
			return nullopt;
		}
		requestId = MonitorHandle{ subscriptionId, ++_requestId };//never 0:  the low half counts from 1.
		for( auto pSubscription : existing )
			pSubscription->ClientCalls.emplace( dataChange );
		_requests.emplace( requestId, move(nodes) );
		if( newNodes.empty() ){
			lock.unlock();
			return nullopt;
		}
		else{
			_calls.emplace( requestId, make_tuple(newNodes,move(dataChange),flat_set<NodeId>{}) );
			lock.unlock();
			return CreateMonitoredItemsRequest{ move(newNodes) };
		}
	}
	α UAMonitoringNodes::OnCreateResponse( UA_CreateMonitoredItemsResponse* response, Handle requestId )ι->void{
		MonitorHandle requestHandle{ requestId };
		let client = _client.lock();
		flat_map<SubscriptionId,flat_set<MonitorId>> surplus;//items this answer created that nothing wants:  duplicates, and nodes unsubscribed while the create was out.
		{
			ul _{ _mutex };
			if( auto pCall = _calls.find(requestId); pCall!=_calls.end() ){
				auto& nodes = get<0>(pCall->second);
				auto& dataChange = get<1>(pCall->second);
				let& cancelled = get<2>(pCall->second);
				ASSERT( nodes.size()==response->resultsSize );
				uint i{};
				for( auto pNode = nodes.begin(); i<response->resultsSize && pNode!=nodes.end(); ++pNode, ++i ){
					MonitoredItemCreateResult result{ move(response->results[i]) };
					if( result.statusCode ){
						DBG( "[{}]Could not create monitored item for node '{}':  {}.", hex(requestId), pNode->ToString(), UAException::Message(result.statusCode) );
						_errors.try_emplace( {requestId} ).first->second.try_emplace( move(*pNode), result.statusCode );
					}
					else if( !client ){//braced:  the log macros expand to a bare `if`.
						CRITICAL( "Could not lock UAClient for subscription processing." );
					}
					else if( cancelled.contains(*pNode) ){
						//Its listener unsubscribed it by node while this create was out (reviews/m2-closing.md #6).  The item is nobody's -
						//another create's answer may have made the node's real one meanwhile, and this listener left that too - so it goes
						//the way a duplicate does, and the subscribe's ack says what became of the node rather than "subscribed".
						surplus.try_emplace( requestHandle.SubId() ).first->second.emplace( result.monitoredItemId );
						TRACE( "[{}.{}]'{}' was unsubscribed while its create was out - deleting the item.", hex(client->Handle()), hex(result.monitoredItemId), pNode->ToString() );
						_errors.try_emplace( {requestId} ).first->second.try_emplace( move(*pNode), UA_STATUSCODE_BADREQUESTCANCELLEDBYCLIENT );
					}
					else if( auto pExisting = get<1>(FindNode(*pNode)); pExisting ){
						//Another create for this node answered first.  MonitoredItemsRequest only finds nodes already monitored, so two creates
						//in flight at once - a rebuild restoring a node while its session subscribes to it again, or two sessions subscribing a
						//new node together - made two monitored items for one node:  every change was pushed twice, and an unsubscribe by node
						//removed only one (subscription-disconnect #15).  This listener joins the item that exists, whose result the ack
						//reports, and the new item is deleted.
						pExisting->ClientCalls.emplace( dataChange );
						surplus.try_emplace( requestHandle.SubId() ).first->second.emplace( result.monitoredItemId );
						TRACE( "[{}.{}]Monitoring '{}' already - joined the existing item, deleting the duplicate.", hex(client->Handle()), hex(result.monitoredItemId), pNode->ToString() );
					}
					else{
						let h = MonitorHandle{ requestHandle.SubId(), result.monitoredItemId };
						TRACE( "[{}.{}]Monitoring '{}'", hex(client->Handle()), hex((Handle)h), pNode->ToString() );
						_subscriptions.emplace( h, Subscription{move(*pNode), move(result), dataChange} );
						if( _subscriptions.size()==1 )
							client->ProcessDataSubscriptions();
					}
				}
				_calls.erase( requestId );
			}
			else{//answered - successfully - after its client's items were taken (see TakeForResubscribe), or a real miss.  Two ifs:  the log macros expand to a bare `if`.
				let taken = _takenCalls.contains( requestHandle );
				if( taken )
					DBG( "[{}]Create answered after TakeForResubscribe - its items went with the old session.", hex(requestId) );
				if( !taken )
					CRITICAL( "Could not find call for subscription='{}' index='{}'.", hex(requestHandle.SubId()), hex(requestHandle.MonitorId()) );
			}
		}
		//Posted, after the lock:  this runs inside run_iterate, and a refused submission reaches ConnectionLost, which takes _mutex.
		if( surplus.size() ){
			client->PostStrand( [client, surplus]()mutable{
				[&]()->DeleteMonitoredItemsAwait::Task { co_await DeleteMonitoredItemsAwait{ move(surplus), move(client) }; }();
			});
		}
	}
	α UAMonitoringNodes::GetResult( Handle requestId, StatusCode sc )ι->FromServer::SubscriptionAck{
		FromServer::SubscriptionAck y;
		ul _{ _mutex };
		flat_map<NodeId,StatusCode>* errors = _errors.find(requestId)!=_errors.end() ? &_errors[requestId] : nullptr;
		let taken = _takenCalls.erase( MonitorHandle{requestId} )>0;
		if( auto pRequest = _requests.find(requestId); pRequest!=_requests.end() ){
			for( auto& n : pRequest->second ){
				auto nodeResult = y.add_results();
				*nodeResult->mutable_node() = ProtoUtils::ToNodeId( n );//the set's order, not the request's:  the client matches on this (reviews/m3-closing.md #10)
				//This request's own word on the node first:  its create failed, or its listener unsubscribed the node while the create
				//was out.  The node can be monitored all the same - for another listener - and answering from that item told this one
				//it was subscribed to something it was never attached to (reviews/m2-closing.md #6).
				//An empty optional, not StatusCode{}:  the ternary converted that to an *engaged* optional holding Good, so with no
				//_errors entry every node without an item - a create that failed outright included - was acked as subscribed.
				if( auto nodeSC = errors ? Find(*errors,n) : optional<StatusCode>{}; nodeSC )
					nodeResult->set_status_code( *nodeSC );
				else if( auto pSubscription = get<1>(FindNode(n)); pSubscription ){
					nodeResult->set_status_code( pSubscription->Result.statusCode );
					nodeResult->set_revised_sampling_interval( pSubscription->Result.revisedSamplingInterval );
					nodeResult->set_revised_queue_size( pSubscription->Result.revisedQueueSize );
				}
				else{
					//No item and no per-node error:  the create failed as a whole (sc), or its answer came after TakeForResubscribe took
					//its client's items, so what it created went with that session.
					nodeResult->set_status_code( sc ? sc : taken ? UA_STATUSCODE_BADSESSIONCLOSED : UA_STATUSCODE_BADCONFIGURATIONERROR );
					if( !sc && !taken )
						CRITICAL( "[{}]Could not find subscription for node '{}'.", hex(requestId), n.ToString() );
				}
			}
			_requests.erase( pRequest );
		}
		//A create that failed - refused outright, or failed by the server as a whole - never reaches OnCreateResponse, which erases the
		//_calls entry of one that answered.  Every create resumes through here, so this is where the rest goes:  left behind, each held
		//its listener - a websocket session - until that session closed or the client went (subscription-disconnect #11).
		_calls.erase( requestId );

		if( errors )
			_errors.erase( requestId );
		return y;
	}

	α UAMonitoringNodes::TakeForResubscribe()ι->flat_map<sp<IDataChange>,flat_set<NodeId>>{
		flat_map<sp<IDataChange>,flat_set<NodeId>> y;
		ul _{ _mutex };
		for( let& [h,subscription] : _subscriptions ){
			for( let& call : subscription.ClientCalls )
				y.try_emplace( call ).first->second.emplace( subscription.Node );
		}
		//The live items go:  every handle names the dead session's subscription, and a left-behind entry would shadow the node on
		//the way back (MonitoredItemsRequest treats a node it still finds as already monitored and asks the server for nothing).
		//So do _calls, the in-flight creates' node lists:  those creates fail with BadSessionClosed, a failure never reaches
		//OnCreateResponse, and nothing else would erase them.  Their handles move to _takenCalls, because a create the server
		//already answered can still be read - successfully - while the client disconnects, and that is expected rather than the
		//"could not find call" it would otherwise log.  Their nodes are not parked with the rest:  the subscriber is told they
		//failed, and restoring them anyway would monitor nodes it has already dropped.
		//_requests and _errors stay.  A failed create still resumes through GetResult - DataChangeAwait::await_resume is noexcept
		//and always calls it - and GetResult answers each node from them.  Clearing them sent that subscribe an ack of zero results
		//for its N nodes, which the web client reads as success (subscription-disconnect #5).
		_subscriptions.clear();
		for( let& [h,_] : _calls )
			_takenCalls.emplace( h );
		_calls.clear();
		return y;
	}

	α UAMonitoringNodes::SendDataChange( Handle h, const Value&& value )ι->uint{
		sl _{ _mutex };
		uint calls{};
		auto client = _client.lock();
		if( auto pSubscription = _subscriptions.find(h); client && pSubscription!=_subscriptions.end() ){
			auto& args = pSubscription->second;
			calls = args.ClientCalls.size();
			for_each( args.ClientCalls, [&opcId=client->Slug(),&value,&args](let& x){x->SendDataChange(opcId, args.Node, value);} );
		}
		else
			TRACE( "Could not find subscription:  {}.", hex(h) );
		return calls;
	}
	α UAMonitoringNodes::Unsubscribe( sp<IDataChange> dataChange )ι->void{
		DBG( "[{}]UAMonitoringNodes::Unsubscribe()", dataChange->to_string() );
		flat_set<MonitorHandle> handles;
		auto f = [&dataChange]( let& hNodeDataChange ){return get<1>(hNodeDataChange.second)==dataChange;};
		ul _{ _mutex };
		for( auto p = find_if(_calls, f); p!=_calls.end(); p=find_if(++p, _calls.end(), f) )
			handles.emplace( MonitorHandle{p->first} );
		for( let& h : handles ){
			_calls.erase( h );
			_requests.erase( h );
			_errors.erase( h );
		}

		flat_map<SubscriptionId,flat_set<MonitorId>> toDelete;
		for( auto&& [h,subscription] : _subscriptions ){
			if( subscription.ClientCalls.erase(dataChange) && subscription.ClientCalls.empty() )
				toDelete.try_emplace( h.SubId() ).first->second.emplace( h.MonitorId() );
		}
		if( auto client = _client.lock(); client && toDelete.size() )
			DeleteMonitoring( client, client->Handle(), toDelete );
	}

	α UAMonitoringNodes::Unsubscribe( flat_set<NodeId>&& nodes, sp<IDataChange> dataChange )ι->tuple<flat_set<NodeId>,flat_set<NodeId>>{
		flat_map<SubscriptionId,flat_set<MonitorId>> toDelete;
		tuple<flat_set<NodeId>,flat_set<NodeId>> successFailures;
		ul _{ _mutex };
		for( auto& node : nodes ){
			auto [id,pSubscription] = FindNode( node );
			bool dropped = pSubscription && pSubscription->ClientCalls.erase( dataChange );
			if( dropped && pSubscription->ClientCalls.empty() )
				toDelete.try_emplace( id.SubId() ).first->second.emplace( id.MonitorId() );
			//And the creates still out.  FindNode sees _subscriptions alone, and a node is not there until its create answers, so an
			//unsubscribe inside that round trip - a view opened and closed at once - found nothing, reported a failure, and the
			//answer then attached a listener that would never ask again:  pushed to until its socket closed, the item never retired
			//(reviews/m2-closing.md #6).  The close-path overload, above, always looked here.  Marked, not removed:  the answer is
			//matched to the node list by position.  Both, not either:  a second subscribe for a node can still be out when the
			//first one's answer has already attached this listener.
			for( auto&& [_, call] : _calls ){
				if( get<1>(call)==dataChange && get<0>(call).contains(node) && get<2>(call).emplace(node).second )
					dropped = true;
			}
			if( dropped )
				get<0>(successFailures).emplace( move(node) );
			else{
				TRACE( "Could not find node '{}' for unsubscription.", node.ToString() );
				get<1>(successFailures).emplace( move(node) );
			}
		}
		if( auto client = _client.lock(); client && toDelete.size() )
			DeleteMonitoring( client, client->Handle(), move(toDelete) );
		return successFailures;
	}

	//sp, not wp:  the wait below is exactly the window in which the client can go away (a run_iterate failure calls
	//RemoveClient), and the client owns the up<UAMonitoringNodes> that is `this` - so a weak handle left the resumed
	//coroutine taking _mutex and walking _subscriptions on freed memory.  Both callers already hold a live sp.
	α UAMonitoringNodes::DeleteMonitoring( sp<UAClient> ua, Handle uaHandle, flat_map<SubscriptionId,flat_set<MonitorId>> requested )ι->DurationTimer::Task{
		auto wait = 1s;
		TRACE( "[{}]DeleteMonitoring count={}, wait={}", hex(uaHandle), requested.size(), Chrono::ToString(wait) ); //duration_cast<std::chrono::seconds>(wait).count()
		(void)co_await DurationTimer{ wait };
		flat_map<UA_UInt32,flat_set<MonitorId>> toDelete;//co_return below releases the lock with the frame's locals.
		ul _{ _mutex };
		for( auto&& [subscriptionId, monitoredIds] : requested ){
			for( auto&& monitoredId : monitoredIds ){
				const MonitorHandle h{subscriptionId,monitoredId};
				if( auto p = _subscriptions.find(h); p!=_subscriptions.end() && p->second.ClientCalls.empty() ){
					TRACE( "[{}.{}]DeleteMonitoring for:  {}", hex(uaHandle), hex((Handle)h), p->second.Node.ToString() );
					_subscriptions.erase( h );
					toDelete.try_emplace( subscriptionId ).first->second.emplace( monitoredId );
				}
			}
		}
		//Every item it was armed for was re-subscribed, or taken for a rebuild, meanwhile:  nothing here is finished with, least of
		//all the cached subscription - by now possibly a newer one a subscribe just made, which the old code deleted and uncached,
		//orphaning it on the server (subscription-disconnect #6).
		if( toDelete.empty() )
			co_return;
		//The subscription may only go when nothing is monitored *and* no subscribe is in flight:  a request registered in
		//_requests has already been told which subscription its items belong to, so deleting it here is what stranded them
		//(#11).  With one in flight the items go but the subscription stays - the racing subscribe keeps a live id, and the
		//next round of this is what eventually retires it.  And only the cached subscription this pass emptied, for the same
		//reason as the return above.
		if( _subscriptions.size()==0 && _requests.empty() && toDelete.contains(ua->SubscriptionId()) ){
			//Stop advertising it in the same breath:  from here a subscribe finds no cached subscription (SubscribeAwait's
			//await_ready) and creates its own instead of building on the one being deleted.  Still under _mutex, so it cannot
			//interleave with the MonitoredItemsRequest that reads the id.
			ua->SetCreatedSubscriptionResponse( nullptr );
			[&]()->UnsubscribeAwait::Task { co_await UnsubscribeAwait( move(toDelete), move(ua) ); }();
		}
		else{
			//A kept subscription can end up with no items at all:  the subscribe it was kept for may then fail for every node, and with no
			//item left nothing schedules another round of this to retire it.  SubscriptionRequestId stayed queued, holding the processing
			//loop - and with it the client, its session and the empty subscription - awake past its ttl for good (subscription-disconnect
			//#12).  So once nothing is monitored, stop processing:  the client idles out as it should, and a create that does succeed
			//registers it again (OnCreateResponse).  Decided on the strand, where that re-registration happens:  a clear sent straight from
			//here could land after an item that had just arrived and silence its pushes.  Posted, never run inline - this holds _mutex.
			if( _subscriptions.empty() ){
				ua->PostStrand( [client=ua]{
					if( !client->MonitoredNodes().Count() )
						client->StopProcessDataSubscriptions();
				});
			}
			[&]()->DeleteMonitoredItemsAwait::Task { co_await DeleteMonitoredItemsAwait{ move(toDelete), move(ua) }; }();
		}
	}
}