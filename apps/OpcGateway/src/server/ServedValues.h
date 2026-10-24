#pragma once
#include <span>
#include <absl/container/flat_hash_map.h>
#include <absl/functional/function_ref.h>
#include <absl/synchronization/mutex.h>
#include <open62541/server.h>
#include <jde/opc/uatypes/NodeId.h>
#include <jde/opc/uatypes/Value.h>
#include "../hist/HistRows.h"

namespace Jde::Opc::Gateway{
	//The mirror's cache (docs/servers/spec.md *Live values*, *Namespaces*, plan Phase 3b):  the last UA_DataValue of each group
	//member, fed by HistGroupHost's tap under the host's lock, and which member each served variable is, read by the variables'
	//callback value source under open62541's service lock.  A member is held from the host's hook that tells of it, which comes
	//before its item exists, so its first value is kept whether its variable is served yet or not, and a variable added or moved
	//later serves it at once.  Its lock is the innermost, the only one both sides share, held for a store or a copy and never across
	//a UA_Server_* call or a host's.  A loss sets Bad_NoCommunication on the connection's members, their values kept, and the first
	//value after replaces it.  Each value keeps the NamespaceArray its group's collector had read when it came, and is remapped on
	//its way out by that array, URI by URI to the endpoint's index for it:  a NodeId, an ExpandedNodeId without a URI, a QualifiedName
	//and an encoded ExtensionObject's type id, alone, in an array, in a Variant or a DataValue, or a member of a structure - a decoded
	//ExtensionObject's too - at any depth.  A structure of the source's own types stays encoded, its body as it came, indices and
	//all:  the gateway reads none of their definitions.  A later array, the source restarted with its namespaces reordered or the
	//connection pointed at another server, never reads an earlier value.  An index it can't place is left as it is and warned of
	//once per node.  A history page's values go the same way by the group's array as it is now (Remap), and a HistoryUpdate's
	//back again (Unmap), through GatewayHistoryHost (spec *History*).
	struct ServedValues final{
		//A host's members, from the hooks that tell of them before their items exist (GroupLoaded, NodeJoined) to the ones that tell
		//of them gone.  A value for a member it doesn't hold is dropped:  one still on its way after its group's removal.
		α Join( HistGroupPK group, HistGroupNodePK index )ι->void;
		α Leave( HistGroupPK group, HistGroupNodePK index )ι->void;
		α Forget( HistGroupPK group )ι->void;
		//A served variable's member, whose value it serves;  false when the node is mapped already.
		α Add( NodeId node, HistGroupPK group, HistGroupNodePK index, ServerCnnctnNK slug )ι->bool;
		α Remove( const NodeId& node )ι->void;//the variable's mapping;  its member keeps its value.
		//The tap's:  a member's value as its group takes it, under the group's last NamespaceArray, and a member's item lost.
		α Store( HistGroupPK group, HistGroupNodePK index, const UA_DataValue& value )ι->void;
		α Lose( HistGroupPK group, std::span<const HistGroupNodePK> indexes )ι->void;
		//The NamespaceArray a group's collector just read, before any value under it, which each value it stores from here on keeps.
		//SeedUris:  a restart's, for a group whose host read its array before this cache was told;  nothing where it was.
		α SetUris( HistGroupPK group, vector<string> uris )ι->void;
		α SeedUris( HistGroupPK group, vector<string> uris )ι->void;
		//The endpoint's index for a connection's remote URI (GatewayServer::Namespace), which never changes:  open62541 drops none.
		α Index( const ServerCnnctnNK& slug, string remote, UA_UInt16 index )ι->void;
		//The callback source's read:  the last value remapped, under `range` where one is asked.  With no value the status is
		//returned, Bad_WaitingForInitialData before the first or a loss's;  Bad_NodeIdUnknown for a node that isn't served.  A reader
		//of the lock, which a store waits on only while reads copy;  the writer's only to mark a node warned.
		α Read( const UA_NodeId& node, const UA_NumericRange* range, UA_DataValue& value )ι->UA_StatusCode;
		//A served variable's history value from the source's indices to the endpoint's, by its group's array as it is now, not the one
		//the value came under, since the library stores no URI inside a value (spec *History*);  and a HistoryUpdate's value the
		//other way, the endpoint's indices to the source's, so it is stored as the source's would be.  Nothing for a node that isn't
		//served;  an index it can't place is left as it is and warned of once per node, as Read's are.
		α Remap( const UA_NodeId& node, UA_Variant& value )ι->void;
		α Unmap( const UA_NodeId& node, UA_Variant& value )ι->void;
		//A value's indices from the source's to the endpoint's by `map`, none for one it can't place;  the count left as they were.
		//0 stays 0.  Structures are walked by their open62541 type descriptions.
		Ω Remap( UA_Variant& value, absl::FunctionRef<optional<UA_UInt16>(UA_UInt16)> map )ι->uint;
		//open62541's UA_CallbackValueSource over a node whose context is the ServedValues it is served from;  no write until Phase 5.
		Ω Source()ι->UA_CallbackValueSource;
	private:
		using Uris = sp<const vector<string>>;
		struct Served final{ HistGroupPK Group; HistGroupNodePK Index; ServerCnnctnNK Slug; bool Warned{}; };
		struct Member final{ optional<Value> Last; Uris Namespaces; };//none before its first value;  the array it came under.
		struct Group final{ Uris Namespaces; absl::flat_hash_map<HistGroupNodePK,Member> Members; };//its collector's last array.
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α Find( HistGroupPK group, HistGroupNodePK index )Ι->const Member*;//null for a member it doesn't hold.
		//A value's indices by the array `uris` and the slug's table, from the source's to the endpoint's, or `back`:  how many it couldn't place.
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α Map( const Served& served, const vector<string>* uris, UA_Variant& value, bool back )Ι->uint;
		α Map( const UA_NodeId& node, UA_Variant& value, bool back )ι->void;//Remap's and Unmap's, by the group's array now, and warned of.
		α Warn( const UA_NodeId& node, uint unmapped )ι->void;//once per node.
		mutable absl::Mutex _mutex;
		absl::flat_hash_map<NodeId,Served,NodeHash,NodeEqual> _nodes ABSL_GUARDED_BY(_mutex);//found by the callback's UA_NodeId, uncopied.
		absl::flat_hash_map<HistGroupPK,Group> _groups ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ServerCnnctnNK,absl::flat_hash_map<string,UA_UInt16>> _indexes ABSL_GUARDED_BY(_mutex);//Index's, by slug and remote URI.
	};
}