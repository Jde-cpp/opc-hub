#pragma once
#include <span>
#include <absl/container/flat_hash_map.h>
#include <absl/synchronization/mutex.h>
#include <open62541/server.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include "../hist/HistRows.h"

namespace Jde::Opc::Gateway{
	//The mirror's cache (docs/servers/spec.md *Live values*, *Namespaces*, plan Phase 3b):  the last UA_DataValue of each served
	//variable, and which group member it is, fed by HistGroupHost's tap under the host's lock and read by the variables' callback
	//value source under open62541's service lock.  Its lock is the innermost, the only one both sides share, held for a store or a
	//copy and never across a UA_Server_* call or a host's.  A loss sets Bad_NoCommunication on the connection's nodes, their values
	//kept, and the first value after replaces it.  Each connection's namespace table, the NamespaceArray as the collector last read
	//it, source index to the endpoint's, remaps every value on its way out:  a NodeId, an ExpandedNodeId without a URI, a
	//QualifiedName and an encoded ExtensionObject's type id, alone, in an array or inside a Variant;  an index the table doesn't
	//hold is left as it is and warned of once per node.  Phases 4 and 5 call Remap too.
	struct ServedValues final{
		//A served variable's member, with the last value its host delivered - as the group took it, Bad_NoCommunication while its
		//item is lost - or none before its first.  False when the node is mapped already.
		α Add( NodeId node, HistGroupPK group, HistGroupNodePK index, ServerCnnctnNK slug, optional<Value> last )ι->bool;
		α Remove( const NodeId& node )ι->void;
		//The tap's:  a member's value as its group takes it, and a member's item lost.  Nothing for a member with no variable here.
		α Store( HistGroupPK group, HistGroupNodePK index, const UA_DataValue& value )ι->void;
		α Lose( HistGroupPK group, std::span<const HistGroupNodePK> indexes )ι->void;
		α SetNamespaces( ServerCnnctnNK slug, vector<UA_UInt16> table )ι->void;//the connection's, source index to the endpoint's.
		α Table( const ServerCnnctnNK& slug )Ι->vector<UA_UInt16>;//empty for a connection whose array hasn't been read.
		//The callback source's read:  the last value remapped, under `range` where one is asked, Bad_WaitingForInitialData before
		//the first, Bad_NodeIdUnknown for a node that isn't served.
		α Read( const NodeId& node, const UA_NumericRange* range, UA_DataValue& value )ι->UA_StatusCode;
		//A value's indices from the source's to the endpoint's by `table`;  the count left as they were, past the table's end.
		Ω Remap( UA_Variant& value, std::span<const UA_UInt16> table )ι->uint;
		//open62541's UA_CallbackValueSource over a node whose context is the ServedValues it is served from;  no write until Phase 5.
		Ω Source()ι->UA_CallbackValueSource;
	private:
		struct Served final{ HistGroupPK Group; HistGroupNodePK Index; ServerCnnctnNK Slug; optional<Value> Last; bool Warned{}; };
		mutable absl::Mutex _mutex;
		absl::flat_hash_map<NodeId,Served> _nodes ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<std::pair<HistGroupPK,HistGroupNodePK>,NodeId> _members ABSL_GUARDED_BY(_mutex);//Store's lookup into _nodes.
		absl::flat_hash_map<ServerCnnctnNK,vector<UA_UInt16>> _tables ABSL_GUARDED_BY(_mutex);
	};
}