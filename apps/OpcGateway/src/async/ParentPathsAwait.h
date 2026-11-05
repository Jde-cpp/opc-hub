#pragma once
#include <jde/fwk/co/AnyAwait.h>
#include "../uatypes/Browse.h"

namespace Jde::Opc::Gateway{
	struct UAClient;
	//A node's step toward the stop node, as a ParentPathsAwait browsed it:  Refused, lost;  AtStop, its parent is the stop node;
	//else Up, its parent and that parent's browse name, or none - no hierarchical parent - and so no path.  A `parents` map is one
	//stop node's:  the step is read against it.
	struct PathParent final{ bool Refused{}; bool AtStop{}; optional<std::pair<NodeId,BrowseSegment>> Up; };
	using PathParents = flat_map<NodeId,PathParent>;
	//A node's ancestors' browse names from the stop node down to its parent, empty for a child of it, or none:  outside that tree (a
	//type or a view under the Objects folder, the stop node itself), or Deep, more than MaxDepth, which a reference cycle looks
	//like.  Lost:  a parent on the way the server refused.
	struct ParentPath final{ optional<vector<BrowseSegment>> Parents; vector<NodeId> Ids; bool Lost{}; bool Deep{}; };//Ids:  each parent's, as Parents, once it's set.
	//Each node's path from `stop`, the Objects folder or Root, up the inverse HierarchicalReferences (Browse::Request::Parents) - among
	//several parents the stop node, else the Objects folder, else an object or a variable, else the first:  the tree the picker and
	//the search index reach a node by, and not a view that organizes it too.  A round browses every parent no walk knows yet, at
	//most `batch` a request and fewer after one refused as too many;  `parents` keeps each, so a node whose ancestor another walked
	//finishes without a request, in this walk and in the caller's next.  Throws as a browse does.  HistGroupHost::Describe's paths,
	//from the Objects folder, and NodeQLAwait's `path` on the id form, from Root, each spelling the segments its own way.
	struct ParentPathsAwait final : AnyAwait<vector<ParentPath>>{
		static constexpr uint MaxDepth{ 64 };
		ParentPathsAwait( vector<NodeId> nodes, PathParents& parents, sp<UAClient> client, uint batch=std::numeric_limits<uint>::max(), NodeId stop=NodeId::ObjectsFolder(), SRCE )ι:
			AnyAwait{ sl }, _nodes{ move(nodes) }, _parents{ parents }, _client{ move(client) }, _batch{ std::max<uint>(1, batch) }, _stop{ move(stop) }{}
	private:
		α Suspend()ι->void override{ Walk(); }
		α Walk()ι->VoidTask;
		vector<NodeId> _nodes;
		PathParents& _parents;
		sp<UAClient> _client;
		uint _batch;
		NodeId _stop;
	};
}