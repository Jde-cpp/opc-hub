#include <jde/historian/Group.h>
#include <absl/container/flat_hash_set.h>

#define let const auto

namespace Jde::Opc::Hist{
	constexpr ELogTags _tags{ ELogTags::Settings };

	//A percent-of-range format with no usable range stores every change, warned of when the node is added; anything else
	//wrong is the host's config, named by its node.
	Ω validate( const ExNodeId& node, Thresholds& t, SL sl )ε->void{
		THROW_IFSL( t.MinTimeInterval<Duration::zero() || t.MaxTimeInterval<Duration::zero(), "'{}' has a negative MinTimeInterval or MaxTimeInterval.", node.to_string() );
		if( !t.ExceptionDeviation )
			return;
		let deviation = *t.ExceptionDeviation;
		THROW_IFSL( !std::isfinite(deviation) || deviation<0, "'{}' has ExceptionDeviation {}, not a finite, non-negative number.", node.to_string(), deviation );
		THROW_IFSL( t.DeviationFormat>UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE, "'{}' has an unknown ExceptionDeviationFormat, {}.", node.to_string(), underlying(t.DeviationFormat) );
		let ranged = t.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE || t.DeviationFormat==UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE;
		if( ranged && (!t.Range || !std::isfinite(t.Range->Low) || !std::isfinite(t.Range->High) || t.Range->High<=t.Range->Low) ){
			WARN( "'{}' has no usable range for its percent-of-range ExceptionDeviation, so it stores every change.", node.to_string() );
			t.ExceptionDeviation.reset();
		}
	}

	Group::Group( GroupConfig config, sp<IClock> clock, vector<Member> members, Restored restored, SL sl )ε:
		_clock{ move(clock) },
		_config{ move(config) }{
		let now = _clock->Now();
		absl::flat_hash_set<NodeIndex> restoredIndexes, kept;
		restoredIndexes.reserve( restored.Members.size() );
		for( let& [_,index] : restored.Members )
			restoredIndexes.emplace( index );
		vector<Record> added;
		ul _{ _mutex };
		_nodes.reserve( members.size() );
		_indexes.reserve( members.size() );
		_nextIndex = restored.NextIndex;
		for( auto& member : members ){
			auto node = Normalize( member, sl );
			if( auto p = restored.Members.find(node); p!=restored.Members.end() && (Issued() || p->second==member.Index) ){
				kept.emplace( p->second );
				Insert( move(node), move(member.Config), p->second, sl );
			}
			else{
				let index = Insert( node, move(member.Config), member.Index, sl );
				THROW_IFSL( restoredIndexes.contains(index), "Group '{}' cannot give '{}' node_index {}: its files have it as another node's.", Name(), node.to_string(), index );
				added.emplace_back( NodeAdded{index, move(node), now, {}} );
			}
		}
		for( let& [_,index] : restored.Members ){
			if( !kept.contains(index) )
				_buffer.emplace_back( NodeRemoved{index, now, {}} );
		}
		std::ranges::move( added, std::back_inserter(_buffer) );
	}

	//The URI names the namespace, so the index - the host's own numbering - is dropped and never compared.
	α Group::Normalize( Member& member, SL sl )Ε->ExNodeId{
		auto node = move( member.Node );
		THROW_IFSL( !node.namespaceUri.length, "'{}' has no namespace URI - the historian keeps none by its index.", node.to_string() );
		node.nodeId.namespaceIndex = 0;
		THROW_IFSL( Issued()==(member.Index!=0), "Group '{}' {} node_index, but '{}' came with {}.", Name(), Issued() ? "issues its own" : "takes the host's", node.to_string(), member.Index );
		validate( node, member.Config, sl );
		return node;
	}
	α Group::Insert( ExNodeId node, Thresholds config, NodeIndex index, SL sl )ε->NodeIndex{
		THROW_IFSL( _indexes.contains(node), "'{}' is already in group '{}'.", node.to_string(), Name() );
		if( !index )
			index = _nextIndex++;
		THROW_IFSL( _nodes.contains(index), "node_index {} is already in group '{}'.", index, Name() );
		_indexes.emplace( node, index );
		optional<TimePoint> rejoin;
		if( auto p = _left.find(node); p!=_left.end() ){
			rejoin = p->second;
			_left.erase( p );
		}
		_nodes.emplace( index, Node{move(node), move(config), rejoin} );
		return index;
	}

	α Group::Add( Member member, optional<Writer> by, SL sl )ε->NodeIndex{
		auto node = Normalize( member, sl );
		let now = _clock->Now();
		ul _{ _mutex };
		THROW_IFSL( _closed, "Group '{}' was removed.", Name() );
		let index = Insert( node, move(member.Config), member.Index, sl );
		_buffer.emplace_back( NodeAdded{index, move(node), now, move(by)} );
		return index;
	}

	α Group::Remove( NodeIndex index, optional<Writer> by, SL sl )ε->void{
		let now = _clock->Now();
		ul _{ _mutex };
		auto p = _nodes.find( index );
		THROW_IFSL( p==_nodes.end(), "node_index {} is not in group '{}'.", index, Name() );
		_indexes.erase( p->second.Id );
		_left.insert_or_assign( move(p->second.Id), p->second.Break.value_or(now) );
		_nodes.erase( p );
		_buffer.emplace_back( NodeRemoved{index, now, move(by)} );
	}

	α Group::SetThresholds( NodeIndex index, Thresholds thresholds, SL sl )ε->void{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		THROW_IFSL( p==_nodes.end(), "node_index {} is not in group '{}'.", index, Name() );
		validate( p->second.Id, thresholds, sl );
		p->second.Config = move( thresholds );
	}
	α Group::FindThresholds( NodeIndex index )Ι->optional<Thresholds>{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		return p==_nodes.end() ? optional<Thresholds>{} : p->second.Config;
	}
	α Group::Find( const ExNodeId& node )Ι->optional<NodeIndex>{
		ExNodeId key{ node };
		key.nodeId.namespaceIndex = 0;
		ul _{ _mutex };
		auto p = _indexes.find( key );
		return p==_indexes.end() ? optional<NodeIndex>{} : p->second;
	}

	α Group::Enqueue( NodeIndex index, const UA_DataValue& value )ι->bool{
		Value copy{ value };//outside the lock, which the host's own lock is already held over.
		if( !copy.hasServerTimestamp ){
			copy.serverTimestamp = UADateTime{ _clock->Now() }.UA();
			copy.hasServerTimestamp = true;
		}
		ul _{ _mutex };
		auto p = _nodes.find( index );
		if( p==_nodes.end() )
			return false;
		_buffer.emplace_back( DataValue{index, move(copy), std::exchange(p->second.Break, std::nullopt)} );
		return true;
	}

	α Group::Disconnected( TimePoint at )ι->void{
		ul _{ _mutex };
		_connected = false;
		for( auto&& [_,node] : _nodes ){
			if( !node.Break )//no value since an earlier break, so that one still stands.
				node.Break = at;
		}
	}
	α Group::Connected()ι->void{
		ul _{ _mutex };
		_connected = true;
	}
	α Group::IsConnected()Ι->bool{
		ul _{ _mutex };
		return _connected;
	}
	α Group::FindBreak( NodeIndex index )Ι->optional<TimePoint>{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		return p==_nodes.end() ? optional<TimePoint>{} : p->second.Break;
	}

	α Group::Buffer()Ι->vector<Record>{
		ul _{ _mutex };
		return _buffer;
	}
	α Group::Close( optional<Writer> by )ι->void{
		let now = _clock->Now();
		ul _{ _mutex };
		vector<NodeIndex> indexes;//in index order, so the same members leave the same way every run.
		indexes.reserve( _nodes.size() );
		for( let& [index,_] : _nodes )
			indexes.push_back( index );
		std::ranges::sort( indexes );
		for( let index : indexes )
			_buffer.emplace_back( NodeRemoved{index, now, by} );
		_nodes.clear();
		_indexes.clear();
		_closed = true;
	}
}