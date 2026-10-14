#pragma once
#include "io/DayFiles.h"

namespace Jde::Opc::Hist{
	//The nodes a read or an edit names, each with every index it has had (spec *Reads*):  the one named, which answers
	//for all of them, each the group knows left the node, which the buffer may still hold values of, and each that a file
	//opened so far lists for the same NodeId, learned from its preamble before its records are read, so a node that
	//rejoined reads as one series.  A node named by an index the group has no NodeId for is read by that index alone.
	struct Joined final{
		//ids is per node, none for one with no NodeId; empty for none.
		Joined( vector<NodeIndex> nodes, vector<optional<ExNodeId>> ids={} )ι;
		//An index the group knows of node, which left it, or is its current one when another is named:  seeded when node
		//is one named.
		α Seed( NodeIndex index, const ExNodeId& node )ι->bool;
		//The slot of the node the index is one of:  none for an index of no node named.
		α Slot( NodeIndex index )Ι->optional<uint>;
		α Index( uint slot )Ι->NodeIndex{ return _nodes[slot]; }//the index named, which the node's values go out under.
		α Canonical( NodeIndex index )Ι->NodeIndex{ return _nodes[*Slot(index)]; }//Index of the index's slot.
		α Indexes( uint slot )Ι->const vector<NodeIndex>&{ return _indexes[slot]; }//the node's indexes, the one named first.
		α Count()Ι->uint{ return _nodes.size(); }
		α Joins()Ι->bool{ return !_byNode.empty(); }//a node has a NodeId, so a file not yet opened may list another index of it.
		α Joins( uint slot )Ι->bool{ return _joins[slot]; }
		//Each index the preamble lists for a named node's NodeId that isn't yet one of its:  added, and returned.
		α Learn( const Preamble& preamble )ι->vector<NodeIndex>;
		//Whether the preamble lists an index of any node named, once learned from.
		α Lists( const Preamble& preamble )Ι->bool;
	private:
		α Seed( NodeIndex index, uint slot )ι->void;
		vector<NodeIndex> _nodes;
		vector<vector<NodeIndex>> _indexes;
		vector<bool> _joins;//per slot, whether it has a NodeId.
		absl::flat_hash_map<NodeIndex,uint> _slots;
		absl::flat_hash_map<ExNodeId,uint> _byNode;
	};
}