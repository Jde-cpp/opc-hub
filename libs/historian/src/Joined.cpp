#include "Joined.h"

#define let const auto

namespace Jde::Opc::Hist{
	Joined::Joined( vector<NodeIndex> nodes, vector<optional<ExNodeId>> ids )ι:_nodes{ move(nodes) }, _indexes( _nodes.size() ){
		for( uint i=0; i<_nodes.size(); ++i ){
			if( _slots.try_emplace(_nodes[i], i).second )
				_indexes[i].push_back( _nodes[i] );
			if( i<ids.size() && ids[i] )
				_byNode.try_emplace( move(*ids[i]), i );
		}
	}
	α Joined::Seed( NodeIndex index, uint slot )ι->void{
		if( _slots.try_emplace(index, slot).second )
			_indexes[slot].push_back( index );
	}
	α Joined::Seed( NodeIndex index, const ExNodeId& node )ι->bool{
		auto p = _byNode.find( node );
		if( p==_byNode.end() )
			return false;
		Seed( index, p->second );
		return true;
	}
	α Joined::Slot( NodeIndex index )Ι->optional<uint>{
		auto p = _slots.find( index );
		return p==_slots.end() ? optional<uint>{} : p->second;
	}
	α Joined::Named()Ι->absl::flat_hash_map<NodeIndex,NodeIndex>{
		absl::flat_hash_map<NodeIndex,NodeIndex> y;
		for( let& [index,slot] : _slots )
			y.emplace( index, _nodes[slot] );
		return y;
	}
	α Joined::Lists( const Preamble& preamble )Ι->bool{
		return std::ranges::any_of( preamble, [this]( let& member ){ return _slots.contains( member.first ); } );
	}
}