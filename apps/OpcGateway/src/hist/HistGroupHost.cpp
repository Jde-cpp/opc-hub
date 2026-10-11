#include "HistGroupHost.h"
#include <jde/opc/proto/opc.Common.h>
#include "../async/ConnectAwait.h"
#include "../async/DataChanges.h"
#include "../async/MonitoredItems.h"
#include "../async/NamespacesAwait.h"
#include "../async/Subscriptions.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)(EOpcLogTags::Opc | EOpcLogTags::Monitoring) };
	using std::chrono::duration;

	HistGroupHost::HistGroupHost( const HistGroupRow& row, ServerCnnctnNK slug, sp<Opc::Hist::Group> group, vector<HistMember> members, Credential collector, Opc::Hist::IClock& clock )ι:
		_id{ row.Id }, _guid{ row.Guid }, _slug{ move(slug) }, _group{ move(group) }, _collector{ move(collector) }, _clock{ clock }, _columns{ row.Thresholds }{
		for( auto& member : members )
			_nodes.emplace( member.Index, Node{move(member.Node), nullopt, member.Thresholds} );
	}
	HistGroupHost::~HistGroupHost(){
		if( _retry )
			_clock.Cancel( _retry );
	}
	α HistGroupHost::Columns()Ι->HistThresholdColumns{ lg _{ _mutex }; return _columns; }
	α HistGroupHost::SetColumns( HistThresholdColumns columns )ι->void{ lg _{ _mutex }; _columns = move(columns); }

	α HistGroupHost::Join( const HistGroupNodeRow& row, Opc::Hist::Thresholds thresholds, optional<Opc::Hist::Writer> by, SL sl )ε->void{
		_group->Add( {row.Node, thresholds, row.Id}, move(by), sl );
		{
			lg _{ _mutex };
			_nodes.insert_or_assign( row.Id, Node{row.Node, nullopt, thresholds} );
		}
		INFO( "[{}]Node {} '{}' joined.", _guid, row.Id, row.Node.ToString() );
		Subscribe();
	}
	α HistGroupHost::Leave( HistGroupNodePK index, optional<Opc::Hist::Writer> by, SL sl )ε->void{
		optional<NodeId> id;
		{
			lg _{ _mutex };
			if( auto p = _nodes.find(index); p!=_nodes.end() ){
				id = p->second.Id;
				if( id )
					_byNode.erase( *id );
				_nodes.erase( p );
			}
		}
		_group->Remove( index, move(by), sl );
		INFO( "[{}]Node {} left.", _guid, index );
		if( id )
			Unsubscribe( {*id} );
	}
	α HistGroupHost::SetNodeThresholds( vector<std::pair<HistGroupNodePK,Opc::Hist::Thresholds>> nodes, SL sl )ε->void{
		flat_map<NodeId,MonitoringParameters> modify;
		try{
			for( auto& [index,thresholds] : nodes ){
				_group->SetThresholds( index, thresholds, sl );
				lg _{ _mutex };
				auto p = _nodes.find( index );
				if( p==_nodes.end() )
					continue;
				auto& node = p->second;
				let parameters = Parameters( thresholds );
				let changed = parameters!=Parameters( node.Thresholds );//a deviation, or Stepped, or MaxTimeInterval:  the item has nothing to change.
				node.Thresholds = move( thresholds );
				if( changed && node.Subscribed )
					modify.emplace( *node.Id, parameters );
				else if( changed )
					node.Stale = true;
			}
		}
		catch( const runtime_error& ){//the nodes before it are the library's already, so their items follow.
			if( modify.size() )
				Modifying( move(modify) );
			throw;
		}
		if( modify.size() )
			Modifying( move(modify) );
	}
	α HistGroupHost::FindThresholds( HistGroupNodePK index )Ι->optional<Opc::Hist::Thresholds>{
		lg _{ _mutex };
		auto p = _nodes.find( index );
		return p==_nodes.end() ? nullopt : optional<Opc::Hist::Thresholds>{ p->second.Thresholds };
	}
	α HistGroupHost::Members()Ι->vector<std::pair<HistGroupNodePK,ExNodeId>>{
		lg _{ _mutex };
		auto y = Reserve<std::pair<HistGroupNodePK,ExNodeId>>( _nodes.size() );
		for( let& [index,node] : _nodes )
			y.emplace_back( index, node.Ex );
		return y;
	}
	α HistGroupHost::Subscribed()Ι->uint{
		lg _{ _mutex };
		return std::ranges::count_if( _nodes, []( let& n ){ return n.second.Subscribed; } );
	}
	α HistGroupHost::Close()ι->void{
		Opc::Hist::IClock::TimerId retry;
		{
			lg _{ _mutex };
			_closed = true;
			retry = std::exchange( _retry, 0 );
			for( auto&& [_,node] : _nodes )
				node.Subscribed = false;
		}
		if( retry )
			_clock.Cancel( retry );
		UAClient::Unsubscribe( shared_from_this() );
	}
	α HistGroupHost::Subscribe()ι->void{
		{
			lg _{ _mutex };
			if( _closed || std::ranges::all_of(_nodes, []( let& n ){ return n.second.Subscribed; }) )
				return;
			if( _subscribing ){
				_again = true;
				return;
			}
			_subscribing = true;
		}
		Subscribing();
	}
	α HistGroupHost::Subscribing()ι->VoidTask{
		auto self = shared_from_this();
		bool retry{};
		try{
			ConnectAwait connect{ string{_slug}, _collector };//an lvalue:  ConnectAwait is neither copied nor moved.
			auto client = co_await Any( connect );
			auto namespaces = co_await Any( NamespacesAwait{client} );
			flat_set<NodeId> nodes;
			{
				lg _{ _mutex };
				for( auto&& [index,node] : _nodes ){
					if( node.Subscribed || _closed )
						continue;
					if( !node.Id ){
						node.Id = namespaces.ToNodeId( node.Ex );
						if( !node.Id ){
							if( !std::exchange(node.Warned, true) )
								WARN( "[{}]Node {} '{}' names a namespace '{}' doesn't have - no item until it does.", _guid, index, node.Ex.ToString(), _slug );
							retry = true;
							continue;
						}
						_byNode.insert_or_assign( *node.Id, index );
					}
					nodes.emplace( *node.Id );
					node.Stale = false;//this create asks for the thresholds as they are now.
				}
			}
			if( nodes.size() ){
				co_await Any( SubscribeAwait{client} );
				auto ack = co_await Any( DataChangeAwait{nodes, self, client} );
				bool closed;
				{
					lg _{ _mutex };
					closed = _closed;
				}
				if( closed )//Close ran while this pass was out:  before the create registered this host, its unsubscribe found nothing to drop.
					UAClient::Unsubscribe( self );
				else{
					uint created{};
					flat_map<NodeId,MonitoringParameters> stale;//edited while the create was out:  SetNodeThresholds left their items to this.
					for( let& result : ack.results() ){
						NodeId node{ ProtoUtils::ToNodeId(result.node()) };
						lg _{ _mutex };
						let p = _byNode.find( node );
						let n = p==_byNode.end() ? _nodes.end() : _nodes.find( p->second );
						if( result.status_code() ){
							retry = true;
							if( n==_nodes.end() || !std::exchange(n->second.Warned, true) ){//braced:  the log macros expand to a bare `if`.
								WARN( "[{}]'{}' refused an item for '{}':  {}.", _guid, _slug, node.ToString(), UAException::Message(result.status_code()) );
							}
							else{
								DBG( "[{}]'{}' refused an item for '{}' again:  {}.", _guid, _slug, node.ToString(), UAException::Message(result.status_code()) );
							}
							continue;
						}
						if( n!=_nodes.end() && !std::exchange(n->second.Subscribed, true) ){
							++created;
							if( std::exchange(n->second.Stale, false) )
								stale.emplace( move(node), Parameters(n->second.Thresholds) );
						}
					}
					INFO( "[{}]{} of {} items created on '{}'.", _guid, created, nodes.size(), _slug );
					if( stale.size() )
						Modifying( move(stale) );
				}
			}
		}
		catch( const runtime_error& e ){
			WARN( "[{}]Could not subscribe on '{}' - trying again in {}s:  {}", _guid, _slug, duration_cast<std::chrono::seconds>(RetryInterval).count(), e.what() );
			retry = true;
		}
		bool again;
		{
			lg _{ _mutex };
			_subscribing = false;
			again = std::exchange( _again, false );
		}
		if( again )//a member that joined meanwhile is asked for now, not a RetryInterval on:  that pass schedules the retry itself.
			Subscribe();
		else if( retry )
			Retry();
	}
	α HistGroupHost::Retry()ι->void{
		auto self = shared_from_this();
		lg _{ _mutex };
		if( _closed || _retry )
			return;
		_retry = _clock.Schedule( RetryInterval, [self]{
			{
				lg _{ self->_mutex };
				self->_retry = 0;
			}
			self->Subscribe();
		} );
	}
	α HistGroupHost::Unsubscribe( flat_set<NodeId>&& nodes )ι->void{
		auto self = shared_from_this();
		if( auto client = UAClient::Find(_slug, _collector); client ){
			if( auto p = client->TryMonitoredNodes(); p )
				p->Unsubscribe( flat_set<NodeId>{nodes}, self );
		}
		UAClient::UnsubscribePending( _slug, self, nodes );
	}
	α HistGroupHost::Modifying( flat_map<NodeId,MonitoringParameters> parameters )ι->VoidTask{
		auto self = shared_from_this();
		try{
			auto client = UAClient::Find( _slug, _collector );
			if( !client )//no client, no item:  the next create takes ItemParameters.
				co_return;
			ModifyMonitoredItemsAwait modify{ move(parameters), client };//an lvalue, as ConnectAwait above.
			auto results = co_await Any( modify );
			for( let& [node,result] : results ){
				if( result.Status ){
					WARN( "[{}]'{}' refused to modify the item for '{}':  {}.", _guid, _slug, node.ToString(), UAException::Message(result.Status) );
				}
				else{
					DBG( "[{}]Item for '{}' modified:  sampling {}ms, queue {}.", _guid, node.ToString(), result.RevisedSamplingInterval, result.RevisedQueueSize );
				}
			}
		}
		catch( const runtime_error& e ){
			WARN( "[{}]Could not modify items on '{}':  {}", _guid, _slug, e.what() );
		}
	}
	α HistGroupHost::ItemParameters( const NodeId& node )Ι->MonitoringParameters{
		lg _{ _mutex };
		if( auto p = _byNode.find(node); p!=_byNode.end() ){
			if( auto n = _nodes.find(p->second); n!=_nodes.end() )
				return Parameters( n->second.Thresholds );
		}
		return Parameters( {} );
	}
	α HistGroupHost::Parameters( const Opc::Hist::Thresholds& thresholds )ι->MonitoringParameters{
		let sampling = duration<double,std::milli>( thresholds.MinTimeInterval ).count();
		let publishing = duration<double,std::milli>( PublishingInterval ).count();
		MonitoringParameters y;
		y.SamplingInterval = sampling>0 ? sampling : 0.0;
		y.QueueSize = sampling>0 ? (uint32)std::ceil( publishing/sampling )+1 : std::numeric_limits<uint32>::max();
		return y;
	}
}