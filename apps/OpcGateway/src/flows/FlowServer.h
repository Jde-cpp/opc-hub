#pragma once
#include <jde/opc/server/UAAccess.h>
#include <jde/opc/server/UAServer.h>
#include <jde/flows/Graph.h>
#include "FlowRows.h"

namespace Jde::Opc::Gateway{
	//A `variable` node's variable as its props name it (docs/flows/spec.md *Derived variables*):  `name`, its browse name
	//under the flow's folder and with the flow's slug its node's id, so no '/', the id's and a page path's separator, and
	//not `_Flow`, the folder's own object;  `dataType`, a built-in type by its name, `Double`, or an array of one,
	//`Double[]`, a value's json being read toward it as updateVariable reads one;  `description`;  `engineeringUnits`, the
	//unit's text or Part 8's { displayName, description, unitId, namespaceUri };  and `euRange`, { low, high }.  Throws
	//what the node's deploy refuses by.
	struct FlowVariable final{
		FlowVariable( const jobject& props, SRCE )ε;
		struct EUInformation final{ string NamespaceUri; int32_t UnitId{}; string DisplayName; string Description; bool operator==( const EUInformation& )const = default; };
		struct EURange final{ double Low; double High; bool operator==( const EURange& )const = default; };
		bool operator==( const FlowVariable& )const = default;
		α TypeName()Ι->string{ return Ƒ( "{}{}", Type->typeName, Array ? "[]" : "" ); }
		string Name;
		const UA_DataType* Type;
		bool Array{};
		string Description;
		optional<EUInformation> Units;
		optional<EURange> Range;
	};

	//The flow server (docs/flows/spec.md *Derived variables*):  the derived variables of every flow, on one UA server the
	//gateway runs for itself as a driver row's is (drivers/DriverServer.h), the library's over the process's /http/ssl pair
	//on 127.0.0.1 at /gateway/flows/server/port, Sign & Encrypt alone, the trust of two and the own-certificate rule, the
	//applicationName `Jde-Cpp Flows flows`, on a thread of its own.  Every node is open to browse and Read by every session
	//and writable by none:  a variable is its flow's.
	//Its row is server_connections' one of kind `flow`, slug `flows`, url `opc.tcp://127.0.0.1:<port>`, made at the start
	//where there is none, its url put right where the port has changed, and never purged here;  a deleted one is left
	//deleted.  The gateway's own clients dial it as any row's url, so the variables are browsed, grouped, historized, served
	//and searched as any node is.
	//The address space, in the one namespace `urn:jde:flows:flows`, index 2:  under Objects a folder for each flow whose
	//current version has a `variable` node, by the flow's slug, `s=<slug>`;  under it a BaseDataVariable per node, by its
	//name, `s=<slug>/<name>`, AccessLevel CurrentRead, with its DataType, ValueRank, Description and, where the props have
	//them, its EngineeringUnits and EURange properties, Bad_WaitingForInitialData until its flow writes it;  and `_Flow`,
	//`s=<slug>/_Flow`, with Version and Enabled, stored, and Messages and Errors, read from the host as they are asked for:
	//what the flow's sources have sent and its nodes' errors since its run started, 0 with none.  Built from every flow's
	//current version before the server listens, so a group that holds a variable finds it at the historian's start, and
	//changed after on the server's own thread (UAServer::Post), in the order the host asks, where a wait for the service
	//lock holds no executor thread.
	//A variable keeps its node across versions by its name, and its value with it;  one whose type changes is changed in
	//place.  One a deploy drops leaves its folder and reads Bad_ObjectDeleted, and the folder leaves Objects with the last,
	//or at its flow's purge:  no node is deleted while the server runs.  open62541 (1.5.10) leaves a monitored item that
	//samples at 0, as a collector's does, linked into a node it deletes, so an item on a variable of the same id made
	//later, a deploy that brings the variable back, would be unlinked through freed memory when the first goes.  A node
	//that leaves keeps its items instead, which hear it again when a deploy brings it back, the group that holds it with them.
	//No /gateway/flows block, no flow server, and a row there is stands.  One that cannot listen, the port another holds,
	//is an Error, and the host tries it again (FlowHost::Relisten):  a flow with a `variable` meanwhile stays stopped,
	//saying so, and is tried again too.  A row that cannot be made or put right is an Error, and the server runs.
	struct FlowServer final : std::enable_shared_from_this<FlowServer>{
		//Gateway::Connect's, after the access snapshot, which its sessions' rights read, and before the historian's start.
		//Throws where the row or the flows cannot be read.
		Ω Start()ε->void;
		Ω Wanted()ι->bool;//a /gateway/flows block.
		//The server built from the flows' rows and their current graphs (FlowSql::CurrentGraphs), and listening:  false
		//where it cannot, logged as an Error, or with `again`, a retry's, at Debug.
		Ω Listen( vector<FlowRow> rows, vector<DB::Row> graphs, bool again )ι->bool;
		//The shutdown's, after UAClient::Shutdown's:  without `terminate` the sessions the clients are closing are waited
		//for, then the server goes.
		Ω Shutdown( bool terminate )ι->void;
		Ω Instance()ι->sp<FlowServer>;//null with no flows block, for one that could not listen, before Start and after Shutdown.
		Ω NamespaceUri()ι->string;
		Ω FolderId( sv flow )ι->NodeId;
		Ω VariableId( sv flow, sv name )ι->NodeId;//a variable's, and `_Flow`'s.
		Ω FlowId( sv flow, sv name )ι->NodeId;//of `_Flow`:  Version, Enabled, Messages or Errors.
		Ω NodeText( sv flow, sv name )ι->string;//the variable as a group's node column spells it (HistNodeText).
		//The variable's type's zero, or an array of none, with `status`:  what it holds until it is written, and a status with
		//no value, which open62541 refuses a number's node.
		Ω Zero( const FlowVariable& variable, StatusCode status )ι->Value;
		//The graph's variables, in node order, or the node a deploy refuses:  a `variable` whose props do not read, or whose
		//name another node of the graph has.
		Ω Read( const Flows::Graph& graph )ι->std::expected<vector<FlowVariable>,Flows::Fault>;

		//FlowHost's:  the flow's folder as its current version has it, made, changed or, with no variable, taken from Objects.
		α Deployed( FlowPK id, string slug, uint32 version, bool enabled, vector<FlowVariable> variables )ι->void;
		α Enabled( string slug, bool enabled )ι->void;//`_Flow.Enabled`, for a flow with a folder.
		α Purged( string slug )ι->void;
		//A variable as its sink writes it:  its node's id, made once, and the names its lines carry.
		//Failing:  the server thread's, the writes refused since the last that succeeded.
		struct Target final{ Target( string slug, string name )ι; const NodeId Id; const string Slug; const string Name; mutable uint Failing{}; };
		//The `variable` sink's, from its flow's strand:  the stored value, which the server's subscribers hear at once.  A
		//write the server refuses is a Warning, those after it counted until one succeeds, whose line says how many.
		α Write( sp<const Target> target, Value value )ι->void;
		α Port()Ι->PortType{ return _port; }
		α Sessions()Ι->vector<Opc::Server::UAAccess::Session>{ return _sessions->List(); }
		α Writes()Ι->uint{ return _writes; }//the values written, for tests.
	private:
		FlowServer()ε;
		//A node the server made, linked into the address space or left out of it:  a property a variable had and has no more,
		//a variable its flow's version dropped, a folder with none.
		enum class EProperty : uint8{ None, Linked, Unlinked };
		struct Held final{ FlowVariable Variable; bool Linked; std::array<EProperty,2> Properties; };//EngineeringUnits, EURange.
		struct Folder final{ FlowPK Id; bool Linked; flat_map<string,Held> Variables; };
		α Post( absl::AnyInvocable<void( UA_Server& )> f )ι->void;//dropped once the shutdown has begun.
		//Each on the server's thread, or before it runs.
		α Apply( UA_Server& ua, FlowPK id, str slug, uint32 version, bool enabled, vector<FlowVariable>&& variables )ι->void;
		α AddFolder( UA_Server& ua, FlowPK id, str slug )ε->void;
		α AddVariable( UA_Server& ua, str slug, const FlowVariable& variable )ε->void;
		α Retype( UA_Server& ua, str slug, const FlowVariable& variable )ε->void;
		α SetProperties( UA_Server& ua, str slug, Held& held, const FlowVariable& variable )ε->void;
		α Drop( UA_Server& ua, str slug, Held& held )ι->void;
		α SetFlow( UA_Server& ua, str slug, optional<uint32> version, bool enabled )ι->void;

		const PortType _port;
		up<Opc::Server::UAServer> _server;
		sp<Opc::Server::UAAccess::SessionList> _sessions;
		flat_map<string,Folder> _folders;//the server's thread's, once it runs.
		std::mutex _mutex;
		bool _stopping{};//under _mutex:  no post after it.
		std::atomic<uint> _writes{};
	};
}