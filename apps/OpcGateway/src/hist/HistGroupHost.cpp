#include "HistGroupHost.h"
#include <jde/fwk/settings.h>
#include <jde/opc/UAException.h>
#include <jde/opc/proto/opc.Common.h>
#include "../async/ConnectAwait.h"
#include "../async/DataChanges.h"
#include "../async/MonitoredItems.h"
#include "../async/ParentPathsAwait.h"
#include "../async/ReadValueAwait.h"
#include "../async/TranslateBrowsePathsAwait.h"
#include "../async/ValueTypesAwait.h"
#include "../async/Subscriptions.h"
#include "HistHost.h"
#include "../uatypes/Browse.h"
#include "../UAClient.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)(EOpcLogTags::Opc | EOpcLogTags::Monitoring) };
	using std::chrono::duration;
	Ω ranged( const Opc::Hist::Thresholds& t )ι->bool{
		return t.ExceptionDeviation && ( t.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE || t.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE );
	}

	HistGroupHost::HistGroupHost( const HistGroupRow& row, ServerCnnctnNK slug, sp<Opc::Hist::Group> group, vector<HistMember> members, Credential collector, Opc::Hist::IClock& clock )ι:
		_id{ row.Id }, _guid{ row.Guid }, _slug{ move(slug) }, _group{ move(group) }, _publishingInterval{ Gateway::_publishingInterval }, _collector{ move(collector) }, _clock{ clock }, _name{ row.Name }, _columns{ row.Thresholds },
		_describeBatch{ std::max<uint>(1, Settings::FindNumber<uint>("/gateway/hist/describeBatch").value_or(64)) }{//value_or mirrors config/Opc.Gateway.jsonnet.
		for( auto& member : members )
			_nodes.emplace( member.Index, Node{ .Ex=move(member.Node), .Thresholds=member.Thresholds, .Attributes=move(member.Attributes) } );
	}
	HistGroupHost::~HistGroupHost(){
		for( let timer : {_retry, _watch} ){
			if( timer )
				_clock.Cancel( timer );
		}
	}
	α HistGroupHost::Name()Ι->string{ ul _{ _mutex }; return _name; }
	α HistGroupHost::SetName( string name )ι->void{ ul _{ _mutex }; _name = move(name); }
	α HistGroupHost::Columns()Ι->HistThresholdColumns{ ul _{ _mutex }; return _columns; }
	α HistGroupHost::SetColumns( HistThresholdColumns columns )ι->void{ ul _{ _mutex }; _columns = move(columns); }

	α HistGroupHost::Join( const HistGroupNodeRow& row, Opc::Hist::Thresholds thresholds, optional<Opc::Hist::Writer> by, SL sl )ε->void{
		_group->Add( {row.Node, ForGroup(thresholds), row.Id}, move(by), sl );
		{
			ul _{ _mutex };
			_nodes.insert_or_assign( row.Id, Node{ .Ex=row.Node, .Thresholds=thresholds, .Attributes=row.Attributes } );
		}
		if( auto listener = HistHost::Instance().Listener(); listener )
			listener->NodeJoined( *this, {row.Id, row.Node, thresholds, row.Attributes} );
		INFO( "[{}]Node {} '{}' joined.", _guid, row.Id, row.Node.ToString() );
		Subscribe();
	}
	α HistGroupHost::Leave( HistGroupNodePK index, optional<Opc::Hist::Writer> by, SL sl )ε->void{
		optional<NodeId> id;
		{
			ul _{ _mutex };
			if( auto p = _nodes.find(index); p!=_nodes.end() ){
				id = p->second.Id;
				if( id )
					_byNode.erase( *id );
				_nodes.erase( p );
			}
		}
		_group->Remove( index, move(by), sl );
		if( auto listener = HistHost::Instance().Listener(); listener )
			listener->NodeLeft( _id, index );
		INFO( "[{}]Node {} left.", _guid, index );
		if( id )
			Unsubscribe( {*id} );
	}
	α HistGroupHost::SetNodeThresholds( vector<std::pair<HistGroupNodePK,Opc::Hist::Thresholds>> nodes, SL sl )ε->void{
		flat_map<NodeId,MonitoringParameters> modify;
		vector<std::pair<HistGroupNodePK,Opc::Hist::Thresholds>> set;//the members' the library has now, for the listener.
		let finish = [&]{
			if( modify.size() )
				Modifying( move(modify) );
			if( auto listener = set.size() ? HistHost::Instance().Listener() : nullptr; listener )
				listener->ThresholdsChanged( _id, move(set) );
		};
		try{
			for( auto& [index,thresholds] : nodes ){
				ul _{ _mutex };//over the group's call, so a Describe can't give the group a range over the thresholds this replaces.
				auto p = _nodes.find( index );
				_group->SetThresholds( index, ForGroup(thresholds, p==_nodes.end() ? HistDescription{} : p->second.Described), sl );
				if( p==_nodes.end() )
					continue;
				auto& node = p->second;
				set.emplace_back( index, thresholds );
				let parameters = Parameters( thresholds, _publishingInterval );
				let changed = parameters!=Parameters( node.Thresholds, _publishingInterval );//a deviation, or Stepped, or MaxTimeInterval:  the item has nothing to change.
				node.Thresholds = move( thresholds );
				if( changed && node.Subscribed )
					modify.emplace( *node.Id, parameters );
				else if( changed )
					node.Stale = true;
			}
		}
		catch( const runtime_error& ){//the nodes before it are the library's already, so their items and the endpoint follow.
			finish();
			throw;
		}
		finish();
	}
	α HistGroupHost::FindThresholds( HistGroupNodePK index )Ι->optional<Opc::Hist::Thresholds>{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		return p==_nodes.end() ? nullopt : optional<Opc::Hist::Thresholds>{ p->second.Thresholds };
	}
	α HistGroupHost::Description( HistGroupNodePK index )Ι->optional<HistDescription>{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		return p==_nodes.end() ? nullopt : optional<HistDescription>{ p->second.Described };
	}
	α HistGroupHost::Attributes( HistGroupNodePK index )Ι->optional<HistNodeAttributes>{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		return p==_nodes.end() ? nullopt : optional<HistNodeAttributes>{ p->second.Attributes };
	}
	α HistGroupHost::Band( HistGroupNodePK index )Ι->optional<EHistBand>{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		if( p==_nodes.end() )
			return nullopt;
		let& node = p->second;
		if( !node.Thresholds.ExceptionDeviation )
			return EHistBand::None;
		if( !node.Described.Enumeration )
			return EHistBand::Waiting;
		if( *node.Described.Enumeration )
			return EHistBand::Enumeration;
		if( let applied = _group->FindThresholds(index); applied && applied->ExceptionDeviation )
			return EHistBand::Applied;
		return ranged( node.Thresholds ) && !node.Described.Ranged ? EHistBand::Waiting : EHistBand::NoRange;
	}
	α HistGroupHost::ServerId( HistGroupNodePK index )Ι->optional<NodeId>{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		return p==_nodes.end() ? nullopt : p->second.Id;
	}
	α HistGroupHost::HistMembers()Ι->vector<HistMember>{
		ul _{ _mutex };
		auto y = Reserve<HistMember>( _nodes.size() );
		for( let& [index, node] : _nodes )
			y.push_back( {index, node.Ex, node.Thresholds, node.Attributes} );
		return y;
	}
	α HistGroupHost::NamespaceUris()Ι->vector<string>{ ul _{ _mutex }; return _namespaces.Uris; }
	α HistGroupHost::Members()Ι->vector<std::pair<HistGroupNodePK,ExNodeId>>{
		ul _{ _mutex };
		auto y = Reserve<std::pair<HistGroupNodePK,ExNodeId>>( _nodes.size() );
		for( let& [index,node] : _nodes )
			y.emplace_back( index, node.Ex );
		return y;
	}
	α HistGroupHost::Index( const ExNodeId& node )Ι->optional<HistGroupNodePK>{
		if( node.namespaceUri.length )
			return _group->Find( node );
		ul _{ _mutex };
		let ex = _namespaces.ToExNodeId( node.nodeId );
		return ex ? _group->Find( *ex ) : nullopt;
	}
	α HistGroupHost::Indexes( const vector<ExNodeId>& nodes, SL sl )Ε->vector<optional<HistGroupNodePK>>{
		vector<optional<HistGroupNodePK>> y; y.reserve( nodes.size() );
		for( let& node : nodes ){
			auto index = Index( node );
			if( index && std::ranges::find(y, index)!=y.end() )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "'{}' is member {}, which the call names already.", node.ToString(), *index };
			y.push_back( index );
		}
		return y;
	}
	α HistGroupHost::Subscribed()Ι->uint{
		ul _{ _mutex };
		return std::ranges::count_if( _nodes, []( let& n ){ return n.second.Subscribed; } );
	}
	α HistGroupHost::Close()ι->void{
		Opc::Hist::IClock::TimerId retry, watch;
		sp<IHistListener> listener;//released off the lock.
		{
			ul _{ _mutex };
			_closed = true;
			listener = move( _listener );
			retry = std::exchange( _retry, 0 );
			watch = std::exchange( _watch, 0 );
			_verifying = false;
			_held.clear();
			for( auto&& [_,node] : _nodes )
				node.Subscribed = false;
		}
		for( let timer : {retry, watch} ){
			if( timer )
				_clock.Cancel( timer );
		}
		UAClient::Unsubscribe( shared_from_this() );
	}
	α HistGroupHost::Subscribe()ι->void{
		{
			ul _{ _mutex };
			if( _closed )
				return;
			Watch();
			if( std::ranges::all_of(_nodes, []( let& n ){ return n.second.Subscribed; }) )
				return;
			if( _subscribing ){
				_again = true;
				return;
			}
			_subscribing = true;
		}
		Subscribing();
	}
	α HistGroupHost::Reset()ι->void{
		for( auto&& [_,node] : _nodes ){
			node.Subscribed = node.Stale = node.Fed = false;
			node.Id.reset();
			node.Lost.reset();
		}
		_byNode.clear();
	}
	α HistGroupHost::Subscribing()ι->VoidTask{
		auto self = shared_from_this();
		bool retry{};
		try{
			ConnectAwait connect{ string{_slug}, _collector };//an lvalue:  ConnectAwait is neither copied nor moved.
			auto client = co_await Any( connect );
			auto namespaces = co_await Any( NamespacesAwait{client} );
			flat_set<NodeId> nodes;
			bool moved{};
			{
				ul _{ _mutex };
				//The server came back with a member's namespace at another index:  the item's index is another node's now, so every
				//item goes and every member resolves again.  A rebuild's check still out decides nothing.
				if( Moved(namespaces) ){
					moved = true;
					Reset();
					++_epoch;
					_verifying = false;
					_held.clear();
				}
				_namespaces = namespaces;
				if( _listener )
					_listener->NamespacesRead( _id, _slug, _namespaces.Uris );
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
					node.Fed = false;
				}
			}
			if( moved ){
				WARN( "[{}]'{}''s namespaces moved - every item is made again, each node by its URI.", _guid, _slug );
				UAClient::Unsubscribe( self );
			}
			if( nodes.size() ){
				co_await Any( SubscribeAwait{client} );
				flat_set<NodeId> existing;//items the client already had, a node that left and joined again within DeleteMonitoring's second:  no initial value comes.
				if( auto monitored = client->TryMonitoredNodes(); monitored ){
					for( auto&& [node,_] : monitored->Handles(nodes) )
						existing.emplace( node );
				}
				auto ack = co_await Any( DataChangeAwait{nodes, self, client} );
				//Inline on the strand in the create's answer, before the publish with the items' first values (UAClient's resubscribe):
				//a member the gateway lost, and this pass asked for again, brings the group's connection back.
				if( !_group->IsConnected() )
					_group->Connected();
				bool closed;
				{
					ul _{ _mutex };
					closed = _closed;
				}
				if( closed )//Close ran while this pass was out:  before the create registered this host, its unsubscribe found nothing to drop.
					UAClient::Unsubscribe( self );
				else{
					flat_map<NodeId,MonitoringParameters> stale;//edited while the create was out:  SetNodeThresholds left their items to this.
					flat_set<NodeId> joined;//created on an item the client already had.
					flat_set<NodeId> created;//each node's DataType read for the group.
					for( let& result : ack.results() ){
						NodeId node{ ProtoUtils::ToNodeId(result.node()) };
						ul _{ _mutex };
						let n = FindNode( node ).second;
						if( result.status_code() ){
							retry = true;
							if( !n || !std::exchange(n->Warned, true) ){//braced:  the log macros expand to a bare `if`.
								WARN( "[{}]'{}' refused an item for '{}':  {}.", _guid, _slug, node.ToString(), UAException::Message(result.status_code()) );
							}
							else{
								DBG( "[{}]'{}' refused an item for '{}' again:  {}.", _guid, _slug, node.ToString(), UAException::Message(result.status_code()) );
							}
							continue;
						}
						if( n && !std::exchange(n->Subscribed, true) ){
							n->Lost.reset();
							if( std::exchange(n->Stale, false) )
								stale.emplace( node, Parameters(n->Thresholds, _publishingInterval) );
							if( existing.contains(node) )
								joined.emplace( node );
							created.emplace( move(node) );
						}
					}
					INFO( "[{}]{} of {} items created on '{}'.", _guid, created.size(), nodes.size(), _slug );
					if( stale.size() )
						Modifying( move(stale) );
					if( joined.size() )
						ReadJoined( client, move(joined) );
					if( created.size() )
						Describe( client, move(created) );
				}
			}
		}
		catch( const runtime_error& e ){
			WARN( "[{}]Could not subscribe on '{}' - trying again in {}s:  {}", _guid, _slug, duration_cast<std::chrono::seconds>(RetryInterval).count(), e.what() );
			retry = true;
		}
		bool again;
		{
			ul _{ _mutex };
			_subscribing = false;
			again = std::exchange( _again, false );
		}
		if( again )//a member that joined meanwhile is asked for now, not a RetryInterval on:  that pass schedules the retry itself.
			Subscribe();
		else if( retry )
			Retry();
	}
	α HistGroupHost::ReadJoined( sp<UAClient> client, flat_set<NodeId> nodes )ι->VoidTask{
		auto self = shared_from_this();
		let count = nodes.size();
		try{
			auto values = co_await Any( ReadValueAwait{move(nodes), client, UA_TIMESTAMPSTORETURN_BOTH} );
			ul _{ _mutex };
			for( auto&& [node,value] : values ){
				let [pk, n] = FindNode( node );
				if( !n || _closed || std::exchange(n->Fed, true) )//a value came first:  the read would repeat it.
					continue;
				if( _verifying )
					_held.emplace_back( pk, move(value) );
				else
					Deliver( pk, value );
			}
		}
		catch( const runtime_error& e ){
			WARN( "[{}]Could not read the {} nodes whose items '{}' already had - each is stored from its next change:  {}", _guid, count, _slug, e.what() );
		}
	}
	//A read's cell, when it answered with a value of `type`.
	Ω answered( const UA_DataValue& v, uint type )ι->bool{ return v.hasValue && !UA_StatusCode_isBad( v.status ) && UA_Variant_hasScalarType( &v.value, &UA_TYPES[type] ); }
	Ω text( const UA_DataValue& v )ι->optional<string>{//a LocalizedText's text;  none for an empty one, or no answer.
		if( !answered(v, UA_TYPES_LOCALIZEDTEXT) )
			return nullopt;
		let& t = static_cast<const UA_LocalizedText*>( v.value.data )->text;
		return t.length ? optional<string>{ ToString(t) } : nullopt;
	}
	Ŧ scalar( const UA_DataValue& v, uint type )ι->optional<T>{ return answered( v, type ) ? optional<T>{ *static_cast<const T*>(v.value.data) } : nullopt; }

	α HistGroupHost::Describe( sp<UAClient> client, flat_set<NodeId> nodes, bool again )ι->VoidTask{
		auto self = shared_from_this();
		{
			ul _{ _mutex };
			if( _describing ){//one at a time, so two passes can't write a node's row out of order:  the one out describes these next.
				_describeAgain = _toDescribe.empty() ? again : _describeAgain && again;
				_toDescribe.insert( nodes.begin(), nodes.end() );
				TRACE( "[{}]A describe is out:  {} nodes wait for it.", _guid, nodes.size() );
				co_return;
			}
			_describing = true;
		}
		for( ;; ){
			let level = again ? ELogLevel::Debug : ELogLevel::Warning;
			const vector<NodeId> all{ nodes.begin(), nodes.end() };
			Namespaces namespaces;
			uint batch;
			{
				ul _{ _mutex };
				namespaces = _namespaces;
				batch = _describeBatch;
			}
			PathParents parents;//each browsed on the way to Objects, once for every member whose path passes it.
			//`batch` nodes a pass, so no request carries more than six operations a node:  a server refuses one past its limits
			//(MaxNodesPerRead, MaxNodesPerBrowse, …) whole.  One refused as too many halves the batch, for the group, and the pass is
			//made again.
			for( uint start=0; start<all.size(); ){
				{
					ul _{ _mutex };
					if( _closed ){
						_describing = false;
						co_return;
					}
				}
				const vector<NodeId> ids{ all.begin()+start, all.begin()+std::min<uint>(start+batch, all.size()) };
				let smaller = [&]( const runtime_error& e )ι->bool{
					let ua = dynamic_cast<const UAException*>( &e );
					if( !ua || ua->Code()!=UA_STATUSCODE_BADTOOMANYOPERATIONS || ids.size()==1 )//one node's are too many:  lost, as any refusal.
						return false;
					batch = std::max<uint>( 1, ids.size()/2 );
					LOG( level, _tags, "[{}]'{}' refused a request for {} nodes as too many - asking of {} at a time.", _guid, _slug, ids.size(), batch );
					ul _{ _mutex };
					_describeBatch = std::min( _describeBatch, batch );
					return true;
				};
				vector<HistDescription> described( ids.size() );
				vector<HistNodeAttributes> read( ids.size() );//each one's attributes, EngineeringUnits and Path as the server gave them, which `attributed`, Ranged and `pathed` say answered;  its DataType and EURange the lock section takes from `typed` and `described`.
				vector<bool> attributed( ids.size() ), pathed( ids.size() );
				vector<optional<HistPathSegment>> own( ids.size() );//each node's own browse name, its path's last segment.
				ValueTypes typed;
				try{
					typed = co_await ValueTypesAwait{ ids, client };
					flat_map<NodeId,optional<bool>> enumerations;//by DataType:  none where the server's answer was lost.
					for( let& dataType : typed.DataTypes ){
						if( !dataType || enumerations.contains(*dataType) )
							continue;
						optional<bool> enumeration;
						if( let type = dataType->namespaceIndex ? nullptr : UA_findDataType(&*dataType); type )
							enumeration = type->typeKind==UA_DATATYPEKIND_ENUM;
						else{
							try{
								co_await client->EnumTypes().Get( client, *dataType );
								enumeration = true;
							}
							catch( const std::exception& ){
								if( client->EnumTypes().Refused(*dataType) )
									enumeration = false;
							}
						}
						enumerations.emplace( *dataType, enumeration );
					}
					for( uint i=0; i<ids.size() && i<typed.DataTypes.size(); ++i ){
						if( let& dataType = typed.DataTypes[i]; dataType )
							described[i].Enumeration = enumerations.find( *dataType )->second;
					}
				}
				catch( const runtime_error& e ){
					if( smaller(e) )
						continue;
					LOG( level, _tags, "[{}]Could not read the DataType of {} nodes on '{}' - an edit of each takes the type of its stored values, and a band waits:  {}", _guid, ids.size(), _slug, e.what() );
				}
				//The attributes a served variable carries (spec *Address space*), and each node's own browse name, its path's last segment.  A
				//node whose DisplayName, ValueRank, AccessLevel or BrowseName was refused keeps what it had, and is asked again;  a Description or
				//ArrayDimensions refused is none, since Part 3 makes both optional.
				try{
					constexpr uint count{ 6 };//a node's cells are 6i to 6i+5, in the request's order.
					ReadAwait attributes{ ReadRequest{ids, {UA_ATTRIBUTEID_DISPLAYNAME, UA_ATTRIBUTEID_DESCRIPTION, UA_ATTRIBUTEID_VALUERANK, UA_ATTRIBUTEID_ARRAYDIMENSIONS, UA_ATTRIBUTEID_ACCESSLEVEL, UA_ATTRIBUTEID_BROWSENAME}}, client };
					let values = co_await Any( attributes );
					for( uint i=0; (i+1)*count<=values.resultsSize; ++i ){
						let* v = values.results+i*count;
						if( !answered(v[0], UA_TYPES_LOCALIZEDTEXT) || !answered(v[2], UA_TYPES_INT32) || !answered(v[4], UA_TYPES_BYTE) || !answered(v[5], UA_TYPES_QUALIFIEDNAME) )
							continue;
						auto& a = read[i];
						a.DisplayName = ToString( static_cast<const UA_LocalizedText*>(v[0].value.data)->text );//empty kept:  only none is unread.
						a.Description = text( v[1] );
						a.ValueRank = scalar<UA_Int32>( v[2], UA_TYPES_INT32 );
						if( let& d = v[3]; d.hasValue && !UA_StatusCode_isBad(d.status) && UA_Variant_hasArrayType(&d.value, &UA_TYPES[UA_TYPES_UINT32]) && d.value.arrayLength ){
							let* dimensions = static_cast<const UA_UInt32*>( d.value.data );
							a.ArrayDimensions = vector<uint32>{ dimensions, dimensions+d.value.arrayLength };
						}
						a.AccessLevel = scalar<UA_Byte>( v[4], UA_TYPES_BYTE );
						let& name = *static_cast<const UA_QualifiedName*>( v[5].value.data );
						own[i] = ToHistPathSegment( namespaces.Uri(name.namespaceIndex), ToString(name.name) );
						attributed[i] = true;
					}
				}
				catch( const runtime_error& e ){
					if( smaller(e) )
						continue;
					LOG( level, _tags, "[{}]Could not read the attributes of {} nodes on '{}' - each row keeps what it had:  {}", _guid, ids.size(), _slug, e.what() );
				}
				//The path from the Objects folder (ParentPathsAwait), as a node page's, each segment by its namespace URI, each parent's with its
				//node's id, which the endpoint's folder for it takes (servers spec *Namespaces*).  A node outside that tree - a type, a view - has
				//no path, as has one deeper than ParentPathsAwait::MaxDepth and one with a browse name or an id on the way that makes no segment
				//(ToHistPathSegment).  A parent the server refused is lost:  the row keeps its path, and the watch asks
				//again.
				try{
					vector<NodeId> walked; vector<uint> slots;
					for( uint i=0; i<ids.size(); ++i ){
						if( own[i] ){
							walked.push_back( ids[i] );
							slots.push_back( i );
						}
						else if( attributed[i] )
							pathed[i] = true;
					}
					let found = co_await ParentPathsAwait{ move(walked), parents, client, batch };
					uint deep{};
					for( uint k=0; k<found.size(); ++k ){
						let i = slots[k];
						if( found[k].Lost )
							continue;
						pathed[i] = true;
						deep += found[k].Deep;
						if( !found[k].Parents )
							continue;
						vector<HistPathSegment> path;
						for( uint j=0; j<found[k].Parents->size(); ++j ){
							let& parent = (*found[k].Parents)[j];
							auto segment = ToHistPathSegment( namespaces.Uri(parent.Ns), parent.Name );
							if( segment )
								segment->Id = namespaces.ToExNodeId( found[k].Ids[j] );
							if( !segment || !segment->Id )
								break;
							path.push_back( move(*segment) );
						}
						if( path.size()==found[k].Parents->size() ){
							path.push_back( move(*own[i]) );
							read[i].Path = move( path );
						}
					}
					if( deep )
						LOG( level, _tags, "[{}]{} nodes on '{}' are deeper than {} below Objects, or in a reference cycle - no path.", _guid, deep, _slug, ParentPathsAwait::MaxDepth );
				}
				catch( const runtime_error& e ){
					if( smaller(e) )
						continue;
					LOG( level, _tags, "[{}]Could not browse the paths of {} nodes on '{}' - each row keeps its path:  {}", _guid, ids.size(), _slug, e.what() );
				}
				try{
					constexpr sv properties[]{ "InstrumentRange", "EURange", "EngineeringUnits" };//a node's are paths 3i to 3i+2.
					vector<UABrowsePath> paths; paths.reserve( ids.size()*std::size(properties) );
					for( let& id : ids ){
						for( let property : properties ){
							const BrowseSegment segment{ 0, string{property} };
							paths.emplace_back( std::span<const BrowseSegment>{&segment, 1}, id );
						}
					}
					TranslateBrowsePathsAwait translate{ move(paths), client };
					let found = co_await Any( translate );
					vector<NodeId> held; vector<uint> slots;//the properties the nodes have, and each one's path.
					for( uint k=0; k<found.size(); ++k ){
						if( found[k] ){
							held.emplace_back( found[k]->nodeId );
							slots.push_back( k );
						}
					}
					vector<optional<Opc::Hist::Range>> ranges( found.size() );
					vector<optional<HistEngineeringUnits>> units( found.size() );
					if( held.size() ){
						ReadAwait values{ ReadRequest{held, UA_ATTRIBUTEID_VALUE}, client };
						let response = co_await Any( values );
						for( uint j=0; j<slots.size() && j<response.resultsSize; ++j ){
							let& v = response.results[j];
							if( answered(v, UA_TYPES_RANGE) ){
								let& range = *static_cast<const UA_Range*>( v.value.data );
								ranges[slots[j]] = Opc::Hist::Range{ range.low, range.high };
							}
							else if( answered(v, UA_TYPES_EUINFORMATION) ){
								let& eu = *static_cast<const UA_EUInformation*>( v.value.data );
								units[slots[j]] = HistEngineeringUnits{ ToString(eu.namespaceUri), eu.unitId, ToString(eu.displayName.text), ToString(eu.description.text) };
							}
						}
					}
					for( uint i=0; i<ids.size() && 3*i+2<ranges.size(); ++i ){
						described[i].Instrument = ranges[3*i];
						described[i].EU = ranges[3*i+1];
						described[i].Ranged = true;
						read[i].EngineeringUnits = move( units[3*i+2] );
					}
				}
				catch( const runtime_error& e ){
					if( smaller(e) )
						continue;
					LOG( level, _tags, "[{}]Could not read the InstrumentRange, EURange and EngineeringUnits of {} nodes on '{}' - a percent-of-range band waits for them:  {}", _guid, ids.size(), _slug, e.what() );
				}
				for( auto& a : read )
					a.FitColumns();//so the host carries what its row holds, and a restart reads back.
				vector<std::pair<HistGroupNodePK,HistNodeAttributes>> rows;//each node the server said anything of, written once the lock is let go.
				{
					ul _{ _mutex };
					for( uint i=0; i<ids.size() && !_closed; ++i ){
						let [pk, n] = FindNode( ids[i] );
						if( !n )
							continue;
						auto& node = *n;
						let before = node.Attributes.Values();//the columns as the row holds them, unless Unwritten.
						optional<NodeId> served;//the DataType the node is served with (spec *Address space*).
						if( i<typed.Types.size() && !UA_StatusCode_isBad(typed.Statuses[i]) ){
							let& dataType = typed.DataTypes[i];
							//None for one of namespace 0 with no built-in type:  abstract, as Number is, or BaseDataType, its values may be any subtype's.
							if( let abstract = dataType && !dataType->namespaceIndex && !ValueTypes::BuiltIn(&*dataType); abstract || typed.Types[i] )
								_group->SetDataType( pk, abstract ? nullptr : typed.Types[i] );
							//The source's where that is a namespace-0 type, abstract or not, since every one exists on the mirror;  else the built-in the
							//historian stores its values by, an enumeration Int32 and a structure Structure, since the source's type has no node there.
							if( dataType && !dataType->namespaceIndex )
								served = *dataType;
							else if( let type = _group->DataType(pk); type )
								served = NodeId{ type->typeId };
						}
						if( served ){
							if( auto ex = namespaces.ToExNodeId(*served); ex )
								node.Attributes.DataType = move( *ex );
						}
						let& d = described[i];
						let known = node.Described.Enumeration.has_value();
						if( d.Enumeration )
							node.Described.Enumeration = d.Enumeration;
						if( d.Ranged ){
							node.Described.Instrument = d.Instrument;
							node.Described.EU = d.EU;
							node.Described.Ranged = true;
							node.Attributes.EURange = d.EU;
							node.Attributes.EngineeringUnits = move( read[i].EngineeringUnits );
						}
						if( attributed[i] ){
							node.Attributes.DisplayName = move( read[i].DisplayName );
							node.Attributes.Description = move( read[i].Description );
							node.Attributes.ValueRank = read[i].ValueRank;
							node.Attributes.ArrayDimensions = move( read[i].ArrayDimensions );
							node.Attributes.AccessLevel = read[i].AccessLevel;
						}
						if( pathed[i] )
							node.Attributes.Path = move( read[i].Path );
						//Asked again:  the ranges or the DataType read lost, the attributes or the path lost or refused, or no answer on a DataType
						//it read.  A DataType the server refused isn't.
						node.Undescribed = !node.Described.Ranged || !attributed[i] || !pathed[i] || ( !node.Described.Enumeration && (i>=typed.DataTypes.size() || typed.DataTypes[i]) );
						if( node.Unwritten || node.Attributes.Values()!=before )//a reconnect's describe mostly changes nothing.
							rows.emplace_back( pk, node.Attributes );
						if( !node.Thresholds.ExceptionDeviation )
							continue;
						if( d.Enumeration.value_or(false) && !known )
							INFO( "[{}]Node {} '{}' is an enumeration, whose every change is stored:  its ExceptionDeviation isn't used.", _guid, pk, node.Ex.ToString() );
						try{
							_group->SetThresholds( pk, ForGroup(node.Thresholds, node.Described) );//one whose range is missing the library warns of, and stores every change.
						}
						catch( const Exception& e ){
							e.Log();
						}
					}
				}
				//The endpoint serves what the host now carries, the row's write aside (spec *Address space*).
				if( auto listener = rows.size() ? HistHost::Instance().Listener() : nullptr; listener ){
					for( let& [pk, attributes] : rows )
						listener->NodeDescribed( _id, pk, attributes );
				}
				//Each changed row's columns, written directly - the QL never does - so a restart serves them before any source is up.  A
				//write that fails leaves the row its last answer, and the node Unwritten, which the next describe writes again.
				flat_set<HistGroupNodePK> unwritten;
				for( auto& [pk, attributes] : rows ){
					try{
						co_await Any( DS()->Execute(UpdateHistNodeAttributesSql(pk, attributes)) );
					}
					catch( const runtime_error& e ){
						WARN( "[{}]Node {}'s attributes weren't written to its row:  {}", _guid, pk, e.what() );
						unwritten.emplace( pk );
					}
				}
				if( rows.size() ){
					ul _{ _mutex };
					for( let& [pk, _] : rows ){
						if( auto p = _nodes.find(pk); p!=_nodes.end() )
							p->second.Unwritten = unwritten.contains( pk );
					}
				}
				start += ids.size();
			}
			auto next = Client();
			ul _{ _mutex };
			if( _closed || !next || _toDescribe.empty() ){//no client:  its next connect's pass describes every member.
				_describing = false;
				_toDescribe.clear();
				co_return;
			}
			client = move( next );
			nodes = std::exchange( _toDescribe, {} );
			again = _describeAgain;
		}
	}
	α HistGroupHost::Retry()ι->void{
		auto self = shared_from_this();
		ul _{ _mutex };
		if( _closed || _retry )
			return;
		_retry = _clock.Schedule( RetryInterval, [self]{
			{
				ul _{ self->_mutex };
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

	α HistGroupHost::Deliver( HistGroupNodePK index, const Value& value )ι->void{
		_group->Enqueue( index, value );
		if( _listener )
			_listener->ValueChanged( _id, index, value );
	}
	α HistGroupHost::Listen( sp<IHistListener> listener )ι->void{
		ul _{ _mutex };
		_listener.swap( listener );//the one it replaces goes with the argument, off the lock.
	}
	α HistGroupHost::SendDataChange( const ServerCnnctnNK&, const NodeId& node, const Value& value )ι->void{
		ul _{ _mutex };
		let [pk, n] = FindNode( node );
		if( !n || _closed )//one that left, still on its way.
			return;
		n->Fed = true;
		if( _verifying )
			_held.emplace_back( pk, value );
		else
			Deliver( pk, value );
	}
	α HistGroupHost::ConnectionLost( const ServerCnnctnNK&, const flat_set<NodeId>& nodes, TimePoint when )ι->void{
		let now = _clock.Now();
		ul _{ _mutex };
		_group->Disconnected( when );
		++_epoch;//a check still out decides nothing:  this break stands.
		if( std::exchange(_verifying, false) )//from the items this break took, before their namespaces were checked:  inside its gap.
			_held.clear();
		vector<HistGroupNodePK> lost;
		for( let& id : nodes ){
			if( let [pk, n] = FindNode(id); n && !n->Lost ){
				n->Lost = now;
				lost.push_back( pk );
			}
		}
		if( _listener && lost.size() )
			_listener->NodesLost( _id, lost );
	}
	α HistGroupHost::ConnectionRestored( const ServerCnnctnNK&, const flat_set<NodeId>& nodes )ι->void{
		uint epoch;
		{
			ul _{ _mutex };
			if( _closed )
				return;
			for( let& id : nodes ){
				if( let n = FindNode(id).second; n )
					n->Lost.reset();
			}
			epoch = ++_epoch;
			_verifying = true;
		}
		Verify( epoch );
	}
	α HistGroupHost::Verify( uint epoch )ι->VoidTask{
		auto self = shared_from_this();
		optional<Namespaces> read;
		try{
			auto client = Client();
			THROW_IF( !client, "The collector's client is gone." );
			read = co_await Any( NamespacesAwait{client} );
		}
		catch( const runtime_error& e ){
			WARN( "[{}]Could not read '{}''s namespaces once its items came back - every item is made again:  {}", _guid, _slug, e.what() );
		}
		bool moved;
		{
			ul _{ _mutex };
			if( _closed || epoch!=_epoch )//a later break or return overtook it, and decides what waited.
				co_return;
			_verifying = false;
			moved = !read || Moved( *read );
			if( read ){
				_namespaces = move( *read );
				if( _listener )//before the held values, which came under it.
					_listener->NamespacesRead( _id, _slug, _namespaces.Uris );
			}
			_group->Connected();
			if( moved ){
				_held.clear();
				Reset();
			}
			else{
				for( let& [index,value] : _held )
					Deliver( index, value );
				_held.clear();
			}
		}
		if( moved ){
			if( read )
				WARN( "[{}]'{}' came back with its namespaces moved - every item is made again, each node by its URI.", _guid, _slug );
			UAClient::Unsubscribe( self );
			Subscribe();
		}
	}
	α HistGroupHost::Moved( const Namespaces& namespaces )Ι->bool{
		return std::ranges::any_of( _nodes, [&]( let& n ){ return n.second.Id && namespaces.ToNodeId(n.second.Ex)!=n.second.Id; } );
	}
	α HistGroupHost::Watch()ι->void{
		if( _closed || _watch )
			return;
		_watch = _clock.Schedule( RetryInterval, [weak=weak_from_this()]{
			if( auto self = weak.lock(); self )
				self->Check();
		} );
	}
	α HistGroupHost::Check()ι->void{
		let now = _clock.Now();
		let client = Client();
		let monitored = client ? client->TryMonitoredNodes() : nullptr;
		flat_set<NodeId> live;//subscribed and not known lost:  each should have an item on the client.
		{
			ul _{ _mutex };
			_watch = 0;
			if( _closed )
				return;
			if( _subscribing ){//its ack is what says.
				Watch();
				return;
			}
			for( let& [_,node] : _nodes ){
				if( node.Subscribed && !node.Lost )
					live.emplace( *node.Id );
			}
		}
		let handles = monitored && live.size() ? monitored->Handles( live ) : flat_map<NodeId,MonitorHandle>{};
		flat_set<NodeId> abandoned;
		vector<HistGroupNodePK> lost;
		{
			ul _{ _mutex };
			if( _closed )
				return;
			//Gone without a ConnectionLost:  a client removed while it was up.  The connection's whole, so the group's break.
			if( !client && live.size() )
				_group->Disconnected( now );
			for( auto&& [index,node] : _nodes ){
				if( !node.Subscribed )
					continue;
				if( !node.Lost && live.contains(*node.Id) && !handles.contains(*node.Id) ){
					node.Lost = now;
					lost.push_back( index );
				}
				if( node.Lost && now-*node.Lost>=AbandonAfter ){
					abandoned.emplace( *node.Id );
					_byNode.erase( *node.Id );
					node.Subscribed = false;
					node.Lost.reset();
					node.Id.reset();//resolved again by URI.
				}
			}
			if( _listener && lost.size() )
				_listener->NodesLost( _id, lost );
			Watch();
		}
		if( abandoned.size() ){
			WARN( "[{}]{} items on '{}' were lost {}s ago and never came back - asking for them again.", _guid, abandoned.size(), _slug, duration_cast<std::chrono::seconds>(AbandonAfter).count() );
			Unsubscribe( move(abandoned) );
			Subscribe();
		}
		Redescribe();
	}
	α HistGroupHost::Redescribe()ι->void{
		auto client = Client();
		if( !client )
			return;
		flat_set<NodeId> nodes;
		{
			ul _{ _mutex };
			if( _closed )
				return;
			for( auto&& [_,node] : _nodes ){
				if( node.Subscribed && !node.Lost && std::exchange(node.Undescribed, false) )
					nodes.emplace( *node.Id );
			}
		}
		if( nodes.size() )
			Describe( move(client), move(nodes), true );
	}
	α HistGroupHost::Modifying( flat_map<NodeId,MonitoringParameters> parameters )ι->VoidTask{
		auto self = shared_from_this();
		try{
			auto client = Client();
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
	α HistGroupHost::FindNode( const NodeId& node )ι->std::pair<HistGroupNodePK,Node*>{
		let p = _byNode.find( node );
		if( p==_byNode.end() )
			return {};
		let n = _nodes.find( p->second );
		return { p->second, n==_nodes.end() ? nullptr : &n->second };
	}
	α HistGroupHost::Client()Ι->sp<UAClient>{
		return UAClient::Find( _slug, _collector );
	}
	α HistGroupHost::ItemParameters( const NodeId& node, Duration publishingInterval )Ι->MonitoringParameters{
		ul _{ _mutex };
		if( publishingInterval!=_publishingInterval ){
			_publishingInterval = publishingInterval;
			_group->SetPublishingInterval( publishingInterval );
		}
		let n = const_cast<HistGroupHost*>( this )->FindNode( node ).second;//a read:  nothing is written through it.
		return Parameters( n ? n->Thresholds : Opc::Hist::Thresholds{}, publishingInterval );
	}
	α HistGroupHost::ForGroup( Opc::Hist::Thresholds t, const HistDescription& described )ι->Opc::Hist::Thresholds{
		if( described.Enumeration.value_or(true) )
			t.ExceptionDeviation.reset();
		else if( ranged(t) ){
			if( let& range = t.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE ? described.Instrument : described.EU; range )
				t.Range = *range;
			else if( !described.Ranged )
				t.ExceptionDeviation.reset();
		}
		return t;
	}
	α HistGroupHost::Parameters( const Opc::Hist::Thresholds& thresholds, Duration publishingInterval )ι->MonitoringParameters{
		let sampling = duration<double,std::milli>( thresholds.MinTimeInterval ).count();
		let publishing = duration<double,std::milli>( publishingInterval ).count();
		constexpr double deepest{ std::numeric_limits<UA_UInt32>::max() };//the request's field:  a server's absurd interval over a fast sample would wrap it.
		MonitoringParameters y;
		y.SamplingInterval = sampling>0 ? sampling : 0.0;
		y.QueueSize = sampling>0 ? (uint32)std::min( std::ceil(publishing/sampling)+1, deepest ) : std::numeric_limits<uint32>::max();
		return y;
	}
}