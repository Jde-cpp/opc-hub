#include <jde/historian/Group.h>

#define let const auto

namespace Jde::Opc::Hist{
	Group::Group( GroupConfig config, sp<IClock> clock )ι:
		_clock{ move(clock) },
		_config{ move(config) }
	{}

	//The URI names the namespace, so the index - the host's own numbering - is dropped and never compared.
	Ω normalize( ExNodeId node, SL sl )ε->ExNodeId{
		THROW_IFSL( !node.namespaceUri.length, "'{}' has no namespace URI - the historian keeps none by its index.", node.to_string() );
		node.nodeId.namespaceIndex = 0;
		return node;
	}

	α Group::Add( Member member, optional<Writer> by, SL sl )ε->NodeIndex{
		auto node = normalize( move(member.Node), sl );
		let issued = _config.Indexes==EIndexes::Issued;
		THROW_IFSL( issued==(member.Index!=0), "Group '{}' {} node_index, but '{}' came with {}.", Name(), issued ? "issues its own" : "takes the host's", node.to_string(), member.Index );
		let now = _clock->Now();
		ul _{ _mutex };
		THROW_IFSL( _indexes.contains(node), "'{}' is already in group '{}'.", node.to_string(), Name() );
		let index = issued ? _nextIndex++ : member.Index;
		THROW_IFSL( _nodes.contains(index), "node_index {} is already in group '{}'.", index, Name() );
		_indexes.emplace( node, index );
		_buffer.emplace_back( NodeAdded{index, node, now, move(by)} );
		_nodes.emplace( index, Node{move(node), move(member.Config), {}} );
		return index;
	}

	α Group::Remove( NodeIndex index, optional<Writer> by, SL sl )ε->void{
		let now = _clock->Now();
		ul _{ _mutex };
		auto p = _nodes.find( index );
		THROW_IFSL( p==_nodes.end(), "node_index {} is not in group '{}'.", index, Name() );
		_indexes.erase( p->second.Id );
		_nodes.erase( p );
		_buffer.emplace_back( NodeRemoved{index, now, move(by)} );
	}

	α Group::SetThresholds( NodeIndex index, Thresholds thresholds, SL sl )ε->void{
		ul _{ _mutex };
		auto p = _nodes.find( index );
		THROW_IFSL( p==_nodes.end(), "node_index {} is not in group '{}'.", index, Name() );
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
		if( !_nodes.contains(index) )
			return false;
		_buffer.emplace_back( DataValue{index, move(copy)} );
		return true;
	}

	α Group::Disconnected( TimePoint at )ι->void{
		ul _{ _mutex };
		if( !_connected )
			return;//still the first break: nothing was delivered since.
		_connected = false;
		for( auto&& [_,node] : _nodes ){
			if( !node.Break )
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
}