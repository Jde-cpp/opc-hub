#pragma once
#include <span>
#include <absl/container/flat_hash_map.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <jde/opc/server/UAServer.h>
#include "../hist/HistGroupHost.h"
#include "../types/ServerCnnctn.h"
#include "../UAClient.h"

namespace Jde::Opc::Gateway{
	struct GatewayNodeAccess; struct GatewayHistoryHost; struct ServedValues;
	//The gateway's opc.tcp endpoint (docs/servers/spec.md, plan Phases 2, 3a and 3b):  the UA server library over the /gateway/server block,
	//in the hub and standalone alike.  Without the block there is none.  It listens before HistHost loads the groups, starts at the
	//end of Gateway::Connect, after they are loaded, and stops before the historian does:  its shutdown function is registered ahead
	//of the historian's.
	//The address space:  one folder per server_connections row, named by its slug and there while the row is, each holding a
	//`_Connection` object with Connected, Since, Url, LastError and Reconnects (spec *Address space*), which follow UAClient's
	//registry through its ConnectionChange listener and the rows through OpcQLHook;  and under it the served nodes, the members of
	//the connection's groups, each at its path on its source, in a namespace of the endpoint's own per connection and remote URI
	//(spec *Namespaces*), every node along the way its source node's id there:  a served member's variable, its children its
	//components as on the source, else a FolderType object standing in for it.  Each with the attributes its row holds,
	//Historizing, its HA Configuration from its resolved thresholds, and its group's rights through GatewayNodeAccess.  They follow
	//the groups as the hosts' IHistListener, from Listen to Stop.  Everything lands here off its callers' threads, on the endpoint's
	//own thread in the order it was posted, and every UA_Server_* call runs there under _mutex, never on the executor, a client's
	//strand or under a host's lock (spec *Threading*).  Each variable's value is a callback source over ServedValues, the mirror's
	//cache (spec *Live values*):  the hosts' tap stores every value and loss there as it comes, under the host's lock and the
	//mirror's alone, and the read callbacks copy it out remapped into the endpoint's namespaces (spec *Namespaces*), which every URI
	//of a connection's NamespaceArray has once its collector has read it.  Their history is the library's UAHistory over their groups
	//through GatewayHistoryHost (spec *History*):  HistoryRead in its four modes and HistoryUpdate's edits, as the OpcServer serves its
	//one group, the capabilities, the aggregates and the archive starts from the library, each page remapped as a live value is.
	struct GatewayServer final : std::enable_shared_from_this<GatewayServer>, IHistListener{
		GatewayServer( Opc::Server::UASettings settings )ι;
		~GatewayServer();//lets the thread go, for one Stop never joined:  a Start that threw before it ran.
		//Reads the block and makes the endpoint, nothing without one;  Instance() and the listeners, the hosts' and the registry's,
		//from here, before HistHost loads the groups, so a member's values are kept from its first and a change before Start is held,
		//not missed.  `processSsl`:  the process's own pair, the endpoint's when the block names none.
		Ω Listen( const Crypto::CryptoSettings& processSsl )ε->void;
		//Starts the endpoint Listen made, folders built from the rows first and the groups' nodes from HistHost, then what was held.
		//Blocks on the rows:  Gateway::Connect's thread.
		Ω Start()ε->void;
		Ω Stop()ι->void;//the server and its thread go;  the registry's listener first.
		Ω Instance()ι->sp<GatewayServer>;//null without a block, before Listen and after Stop.
		//The block as a UASettings, nullopt without one.  Its own function for the OpcServer's test of the None switch and the gateway's of the block.
		Ω ReadSettings( const Crypto::CryptoSettings& processSsl )ε->optional<Opc::Server::UASettings>;
		Ω FolderId( const ServerCnnctnNK& slug )ι->NodeId;//ns=1;s=<slug>
		Ω ConnectionId( const ServerCnnctnNK& slug )ι->NodeId;//ns=1;s=<slug>/_Connection
		Ω VariableId( const ServerCnnctnNK& slug, sv name )ι->NodeId;//ns=1;s=<slug>/_Connection/<name>
		//The endpoint's namespace for a connection's remote URI:  urn:jde:<slug>:<remote>, or the remote itself when it already carries the slug - a driver row's own (spec *Namespaces*, 2026-10-21).
		Ω NamespaceUri( const ServerCnnctnNK& slug, sv remote )ι->string;

		α Url()Ι->string{ return Ƒ( "opc.tcp://{}:{}", _settings.Address.empty() ? Process::HostName() : _settings.Address, _settings.Port ); }
		α Settings()Ι->const Opc::Server::UASettings&{ return _settings; }
		α Server()ι->Opc::Server::UAServer*;//the tests read the address space through it;  null once stopped.

		//OpcQLHook's, after the row is written:  the folder for a new row, the url of an edited one, the folder of a purged one gone.
		//A delete or restore changes nothing:  the folder stays with the row.  Keyed as the mutation was, by id or slug.
		α ConnectionCreated( ServerCnnctn row )ι->void;
		α ConnectionUrlChanged( const DB::Key& key, string url )ι->void;
		α ConnectionPurged( const DB::Key& key )ι->void;

		//The hosts' IHistListener, told where each host's state changes, after it has it:  the served nodes keep step with the groups
		//(spec *Address space*).  A group loaded - at Start, and restored - has every member served, those already served left alone;  a
		//deleted or purged one loses its nodes, and the path folders they leave empty, its connection folder staying with the row.  A
		//node joins once its row exists, under the connection folder by its id until its source describes it, when it moves to its path
		//with the source's attributes;  described again, its attributes are rewritten in place.  A threshold change rewrites its
		//MinimumSamplingInterval and HA Configuration.
		α GroupLoaded( const HistGroupHost& host )ι->void override;
		α GroupRemoved( HistGroupPK group )ι->void override;
		α NodeJoined( const HistGroupHost& host, HistMember member )ι->void override;
		α NodeLeft( HistGroupPK group, HistGroupNodePK index )ι->void override;
		α NodeDescribed( HistGroupPK group, HistGroupNodePK index, HistNodeAttributes attributes )ι->void override;
		α ThresholdsChanged( HistGroupPK group, vector<std::pair<HistGroupNodePK,Opc::Hist::Thresholds>> thresholds )ι->void override;
		//The tap's (spec *Live values*):  into the mirror's cache on the caller's thread, under its lock alone - no post, no UA_Server_*
		//call - so a value is served as soon as the group has it.  The NamespaceArray the same way, ahead of the values it brings, and
		//each URI's namespace posted.
		α ValueChanged( HistGroupPK group, HistGroupNodePK index, const UA_DataValue& value )ι->void override;
		α NodesLost( HistGroupPK group, const vector<HistGroupNodePK>& indexes )ι->void override;
		α NamespacesRead( HistGroupPK group, const ServerCnnctnNK& slug, const vector<string>& uris )ι->void override;
		α Values()Ι->const sp<ServedValues>&{ return _values; }//the mirror's cache, which the history host remaps a page through.
	private:
		struct Connection final{
			ServerCnnctnPK Id;
			string Url;
			bool Connected{};
			TimePoint Since{ Process::StartTime() };
			string LastError;
			uint32 Reconnects{};
			bool Outage{};//the slug's last session went with its connection (a Lost, Down) and none has come back:  the next activation is a reconnect.
		};
		//A served node:  the member as it is served, its variable's id - the source's identifier in the mapped namespace - its HA
		//Configuration's, and every node of that configuration, which its group's rights cover as the variable's.
		struct Served final{ HistMember Member; NodeId Id; NodeId Configuration; vector<NodeId> Configured; };
		//A group's nodes, with the library's group that keeps their history, the host's, which the history host answers a read with.
		struct Group final{ HistGroupPK Id; string Guid; ServerCnnctnNK Slug; sp<Opc::Hist::Group> History; flat_map<HistGroupNodePK,Served> Nodes; };
		//`f` on the endpoint's thread under _mutex with _ua set.  Until Start has built the folders it is held, and Start runs the held ones
		//in order before it opens the endpoint:  nothing posted from the listener's install on is lost or applied out of order.
		α Serial( absl::AnyInvocable<void()> f )ι->void;
		α OnChange( const ConnectionChange& change )ι->void;//the registry's listener.
		α Apply( const ConnectionChange& change )ι->void;//Serial's.
		α AddFolder( const ServerCnnctnNK& slug, const Connection& connection )ι->void;//_mutex held, _ua set.
		α RemoveFolder( const ServerCnnctnNK& slug )ι->void;
		α Write( const ServerCnnctnNK& slug, sv name, const void* scalar, const UA_DataType& type )ι->void;
		α WriteAll( const ServerCnnctnNK& slug, const Connection& connection )ι->void;
		α Find( const DB::Key& key )ι->flat_map<ServerCnnctnNK,Connection>::iterator;//_mutex held
		//The served nodes, _mutex held, _ua set.  Serve:  every member not served yet.  Namespace:  the index of NamespaceUri(slug, remote), added when new.
		α Serve( HistGroupPK id, string guid, ServerCnnctnNK slug, sp<Opc::Hist::Group> history, vector<HistMember> members )ι->void;
		α Namespace( const ServerCnnctnNK& slug, sv remote )ι->UA_UInt16;
		α Register( const ServerCnnctnNK& slug, const vector<string>& uris )ι->void;//a namespace for each URI of a connection's NamespaceArray.
		α Mapped( const ServerCnnctnNK& slug, const ExNodeId& source )ι->NodeId;//the source's identifier in its namespace here.
		//The node at `path`'s end:  its source node's, Mapped, as a variable is (spec *Namespaces*);  ns=1;s=<slug>/<name>/… for a segment
		//without an id - none Describe writes.
		α PathFolderId( const ServerCnnctnNK& slug, std::span<const HistPathSegment> path )ι->NodeId;
		α AddNode( Group& group, HistMember member )ι->void;
		α Unserve( const Group& group, const Served& served, bool added=true, bool prune=true )ι->bool;//false when the variable stays.  `added` false:  AddNode threw before the variable was.  `prune` false:  RemoveGroup prunes once.
		α RemoveNode( Group& group, HistGroupNodePK index, bool prune=true )ι->bool;//false when the variable stays, kept in Nodes.
		//The members served under `node` - each ancestor of their path checked - out, deepest first, each with its group, shallowest first
		//for Restore to put back once `node` has changed:  a served variable come or gone, or the folder that stood in for it.
		α Lift( const ServerCnnctnNK& slug, const NodeId& node )ι->vector<std::pair<HistGroupPK,HistMember>>;
		α Restore( vector<std::pair<HistGroupPK,HistMember>> lifted )ι->void;
		α RemoveGroup( flat_map<HistGroupPK,Group>::iterator group )ι->flat_map<HistGroupPK,Group>::iterator;//the next.
		α Rewrite( Served& served, const HistNodeAttributes& attributes )ι->void;//the source's attributes in place, the path and shape unchanged;  one it dropped as AddNode's default.
		α Prune( const ServerCnnctnNK& slug, std::span<const HistPathSegment> parents )ι->void;//the path folders left empty, from the deepest up to a served variable.
		mutable std::mutex _mutex;
		//From construction:  the tap reaches it from the listener's install on, before Start has a server.  Before _ua, whose thread's
		//callbacks read it, so it outlives them.
		sp<ServedValues> _values;
		up<Opc::Server::UAServer> _ua;
		Opc::Server::UASettings _settings;
		flat_map<ServerCnnctnNK,Connection> _connections;
		flat_map<HistGroupPK,Group> _groups;
		flat_map<ServerCnnctnNK,absl::flat_hash_map<string,UA_UInt16>> _namespaces;//Namespace's, by slug and remote URI:  open62541 never drops one.
		sp<GatewayNodeAccess> _access;//node to group guid, which the rights callbacks read.
		sp<GatewayHistoryHost> _history;//node to group and index, which the history callbacks read;  the historian's share the backend keeps.
		vector<absl::AnyInvocable<void()>> _held;//Serial's before Start has the folders;  under _mutex.
		//Serial's queue, run in order on a thread of the endpoint's own, not the executor's:  a UA_Server_* call waits for open62541's
		//service lock, which ActivateSession holds while the hub's session lookup waits for an executor thread (UAAccess's issued).
		//Last:  the thread is joined before what it runs goes.
		boost::asio::io_context _context{ 1 };
		boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _work{ boost::asio::make_work_guard(_context) };
		std::jthread _thread{ [this]{ _context.run(); } };
	};
}