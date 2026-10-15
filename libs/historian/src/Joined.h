#pragma once
#include "io/DayFiles.h"

namespace Jde::Opc::Hist{
	//The nodes a read or an edit names, each with every index it has had (spec *Reads*):  the one named, which answers
	//for all of them, and each other the group gave the same NodeId or its files list for it, so a node that rejoined
	//reads as one series.  A node named by an index the group has no NodeId for is read by that index alone.
	struct Joined final{
		//ids is per node, none for one with no NodeId; empty for none.
		Joined( vector<NodeIndex> nodes={}, vector<optional<ExNodeId>> ids={} )ι;
		//An index the group knows of node:  seeded when node is one named.
		α Seed( NodeIndex index, const ExNodeId& node )ι->bool;
		//The slot of the node the index is one of:  none for an index of no node named.
		α Slot( NodeIndex index )Ι->optional<uint>;
		α Index( uint slot )Ι->NodeIndex{ return _nodes[slot]; }//the index named, which the node's values go out under.
		α Canonical( NodeIndex index )Ι->NodeIndex{ return _nodes[*Slot(index)]; }//Index of the index's slot.
		//The node's indexes, the one named first:  a node named again answers through the first slot that names it.
		α Indexes( uint slot )Ι->const vector<NodeIndex>&{ return _indexes[_slots.find(_nodes[slot])->second]; }
		α Count()Ι->uint{ return _nodes.size(); }
		α Named()Ι->absl::flat_hash_map<NodeIndex,NodeIndex>;//each index, to Canonical.
		//Whether the preamble lists an index of any node named.
		α Lists( const Preamble& preamble )Ι->bool;
	private:
		α Seed( NodeIndex index, uint slot )ι->void;
		vector<NodeIndex> _nodes;
		vector<vector<NodeIndex>> _indexes;
		absl::flat_hash_map<NodeIndex,uint> _slots;
		absl::flat_hash_map<ExNodeId,uint> _byNode;
	};
}