#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include "../uatypes/Browse.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//A node's step toward the Objects folder, as a ParentPathsAwait browsed it:  Refused, lost;  AtObjects, its parent is the
	//folder;  else Up, its parent and that parent's browse name, or none - no hierarchical parent - and so no path.
	struct PathParent final{ bool Refused{}; bool AtObjects{}; optional<std::pair<NodeId,BrowseSegment>> Up; };
	using PathParents = flat_map<NodeId,PathParent>;
	//A node's ancestors' browse names from the Objects folder down to its parent, empty for a child of the folder, or none:  outside
	//that tree (a type, a view, the folder itself), or Deep, more than MaxDepth, which a reference cycle looks like.  Lost:  a parent
	//on the way the server refused.
	struct ParentPath final{ optional<vector<BrowseSegment>> Parents; bool Lost{}; bool Deep{}; };
	//Each node's path from the Objects folder, up the inverse HierarchicalReferences (Browse::Request::Parents) - the folder among
	//several parents, else the first:  the tree the picker and the search index reach a node by.  A round browses every parent no
	//walk knows yet, at most `batch` a request and fewer after one refused as too many;  `parents` keeps each, so a node whose
	//ancestor another walked finishes without a request, in this walk and in the caller's next.  Throws as a browse does.
	//HistGroupHost::Describe's paths and NodeQLAwait's `path` on the id form, each spelling the segments its own way.
	struct ParentPathsAwait final : AnyAwait<vector<ParentPath>>{
		static constexpr uint MaxDepth{ 64 };
		ParentPathsAwait( vector<NodeId> nodes, PathParents& parents, sp<UAClient> client, uint batch=std::numeric_limits<uint>::max(), SRCE )ι:
			AnyAwait{ sl }, _nodes{ move(nodes) }, _parents{ parents }, _client{ move(client) }, _batch{ std::max<uint>(1, batch) }{}
	private:
		α Suspend()ι->void override{ Walk(); }
		α Walk()ι->VoidTask;
		vector<NodeId> _nodes;
		PathParents& _parents;
		sp<UAClient> _client;
		uint _batch;
	};
}