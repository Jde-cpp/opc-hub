#pragma once
#include <span>
#include <absl/synchronization/mutex.h>
#include <open62541/server.h>
#include <jde/opc/server/IHistoryHost.h>
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
	//ExtensionObject's too, and an encoded one of namespace 0's types, decoded first, as a history page's are - at any depth.  A
	//structure of the source's own types stays encoded, its body as it came, indices and all:  the gateway reads none of their
	//definitions.  A later array, the source restarted with its namespaces reordered or the connection pointed at another server,
	//never reads an earlier value.  An index it can't place is left as it is and warned of
	//once per node.  A history page's values go the same way by the group's array as it is now (Remap), and a HistoryUpdate's
	//back again (Unmap), through GatewayHistoryHost (spec *History*), refused where an index won't go back.
	struct ServedValues final{
		//A host's members, from the hooks that tell of them before their items exist (GroupLoaded, NodeJoined) to the ones that tell
		//of them gone.  A value for a member it doesn't hold is dropped:  one still on its way after its group's removal.
		α Join( HistGroupPK group, HistGroupNodePK index )ι->void;
		α Leave( HistGroupPK group, HistGroupNodePK index )ι->void;
		α Forget( HistGroupPK group )ι->void;
		//A served variable's member, whose value it serves, and the group that keeps its history;  false when the node is mapped already.
		α Add( NodeId node, HistGroupPK group, HistGroupNodePK index, ServerCnnctnNK slug, sp<Opc::Hist::Group> history )ι->bool;
		α Remove( const NodeId& node )ι->void;//the variable's mapping and its history's;  its member keeps its value.
		//A served variable's member and connection, where a write to it goes (ServedWrites);  none for a node that isn't served.
		struct Serving final{ HistGroupPK Group; HistGroupNodePK Index; ServerCnnctnNK Slug; };
		α Served( const UA_NodeId& node )Ι->optional<Serving>;
		//The history host's (GatewayHistoryHost), from the same map, so a node's history comes and goes with its value:  its group and
		//index, none for a node that isn't served;  every served node's archive-start properties, with its group.
		using ArchiveStart = Opc::Server::IHistoryHost::ArchiveStart;
		α History( const UA_NodeId& node )Ι->optional<Opc::Server::IHistoryHost::Node>;
		α ArchiveStarts()Ι->vector<ArchiveStart>;
		//GatewayServer's, answered for UAHistory::Publish:  a variable's StartOfArchive and StartOfOnlineArchive once its HA
		//Configuration has them;  and each node of `group` moved to `history`, a restored group's.
		α AddStarts( const NodeId& node, vector<NodeId> properties )ι->vector<ArchiveStart>;
		α Regroup( HistGroupPK group, sp<Opc::Hist::Group> history )ι->vector<ArchiveStart>;
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
		//of the lock, which a store waits on only while reads copy;  the writer's only the once, to mark a node warned.
		α Read( const UA_NodeId& node, const UA_NumericRange* range, UA_DataValue& value )ι->UA_StatusCode;
		//A served variable's history page from the source's indices to the endpoint's, by its group's array as it is now, not the
		//one each value came under, since the library stores no URI inside a value (spec *History*):  one lookup and one reader's
		//lock for the page.  And a HistoryUpdate's or a Write's value the other way, the endpoint's indices to the source's, so it is
		//stored or written as the source's would be.  Nothing for a node that isn't served;  an index it can't place is left as it is
		//and warned of once per node and way, Remap's as Read's are;  Unmap returns how many, a value its caller refuses.
		α Remap( const UA_NodeId& node, std::span<UA_DataValue> values )ι->void;
		α Unmap( const UA_NodeId& node, UA_Variant& value )ι->uint;
		//A value's indices from the source's to the endpoint's by `map`, none for one it can't place;  the count left as they were.
		//0 stays 0.  Structures are walked by their open62541 type descriptions.
		Ω Remap( UA_Variant& value, absl::FunctionRef<optional<UA_UInt16>(UA_UInt16)> map )ι->uint;
	private:
		using Uris = sp<const vector<string>>;
		using Indexes = absl::flat_hash_map<string,UA_UInt16>;//a slug's, the endpoint's index by remote URI.
		//Its member, the group keeping its history, and its archive starts;  warned of an index served as it came, and of one refused.
		struct Variable final{ HistGroupPK Group; HistGroupNodePK Index; ServerCnnctnNK Slug; sp<Opc::Hist::Group> History; vector<NodeId> Starts; bool Warned{}; bool Refused{}; };
		struct Member final{ optional<Value> Last; Uris Namespaces; };//none before its first value;  the array it came under.
		struct Group final{ Uris Namespaces; absl::flat_hash_map<HistGroupNodePK,Member> Members; };//its collector's last array.
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α Find( HistGroupPK group, HistGroupNodePK index )Ι->const Member*;//null for a member it doesn't hold.
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α Namespaces( HistGroupPK group )Ι->const vector<string>*;//its collector's last array, null before one.
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α SlugIndexes( const ServerCnnctnNK& slug )Ι->const Indexes*;//null for a slug with none.
		//A value's indices by the array `uris` and the slug's `indexes`, from the source's to the endpoint's, or `back`:  how many it couldn't place.
		Ω Map( const Indexes* indexes, const vector<string>* uris, UA_Variant& value, bool back )ι->uint;
		//Once per node and way, under the writer's lock:  its callers skip it for a node their reader's lock saw warned already.
		α Warn( const UA_NodeId& node, uint unmapped, bool back=false )ι->void;
		mutable absl::Mutex _mutex;
		absl::flat_hash_map<NodeId,Variable,NodeHash,NodeEqual> _nodes ABSL_GUARDED_BY(_mutex);//found by the callback's UA_NodeId, uncopied.
		absl::flat_hash_map<HistGroupPK,Group> _groups ABSL_GUARDED_BY(_mutex);
		absl::flat_hash_map<ServerCnnctnNK,Indexes> _indexes ABSL_GUARDED_BY(_mutex);//Index's, by slug and remote URI.
	};
}