#pragma once
#include <span>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <jde/opc/server/UAServer.h>
#include "../hist/HistGroupHost.h"
#include "../types/ServerCnnctn.h"
#include "../UAClient.h"

namespace Jde::Opc::Gateway{
	struct GatewayNodeAccess;
	//The gateway's opc.tcp endpoint (docs/servers/spec.md, plan Phases 2 and 3a):  the UA server library over the /gateway/server block,
	//in the hub and standalone alike.  Without the block there is none.  It starts at the end of Gateway::Connect, after HistHost
	//has loaded the groups, and stops before the historian does:  its shutdown function is registered ahead of the historian's.
	//The address space:  one folder per server_connections row, named by its slug and there while the row is, each holding a
	//`_Connection` object with Connected, Since, Url, LastError and Reconnects (spec *Address space*), which follow UAClient's
	//registry through its ConnectionChange listener and the rows through OpcQLHook;  and under it the served nodes, the members of
	//the connection's groups, each at its path on its source with FolderType objects along the way, in a namespace of the endpoint's
	//own per connection and remote URI (spec *Namespaces*), with the attributes its row holds, Historizing, its HA Configuration from
	//its resolved thresholds, and its group's rights through GatewayNodeAccess.  They follow the groups through HistHost's hooks and
	//HistGroupHost::Describe.  Everything lands here off its callers' threads, on one strand in the order it was posted, and every
	//UA_Server_* call runs there under _mutex, never under a client's strand or a host's lock (spec *Threading*).  The served values
	//and their history are Phases 3b and 4.
	struct GatewayServer final : std::enable_shared_from_this<GatewayServer>{
		GatewayServer( Opc::Server::UASettings settings )ι;
		//Reads the block and starts the endpoint, folders built from the rows first and the groups' nodes from HistHost;  nothing
		//without a block.  The listener and Instance() come before the rows are read, so a change while they load is held, not
		//missed.  Blocks on the rows:  Gateway::Connect's thread.  `processSsl`:  the process's own pair, the endpoint's when the block names none.
		Ω Start( const Crypto::CryptoSettings& processSsl )ε->void;
		Ω Stop()ι->void;//the server and its thread go;  the registry's listener first.
		Ω Instance()ι->sp<GatewayServer>;//null without a block, before Start reads the rows and after Stop.
		//The block as a UASettings, nullopt without one.  Its own function for the OpcServer's test of the None switch and the gateway's of the block.
		Ω ReadSettings( const Crypto::CryptoSettings& processSsl )ε->optional<Opc::Server::UASettings>;
		Ω FolderId( const ServerCnnctnNK& slug )ι->NodeId;//ns=1;s=<slug>
		Ω ConnectionId( const ServerCnnctnNK& slug )ι->NodeId;//ns=1;s=<slug>/_Connection
		Ω VariableId( const ServerCnnctnNK& slug, sv name )ι->NodeId;//ns=1;s=<slug>/_Connection/<name>
		Ω PathFolderId( const ServerCnnctnNK& slug, std::span<const HistPathSegment> path )ι->NodeId;//ns=1;s=<slug>/<name>/…, the folder at `path`.
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

		//HistHost's hooks and HistGroupHost::Describe, each after the host has the change:  the served nodes keep step with the groups
		//(spec *Address space*).  A group loaded - at Start, and restored - has every member served, those already served left alone;  a
		//deleted or purged one loses its nodes, and the path folders they leave empty, its connection folder staying with the row.  A
		//node joins once its row exists, under the connection folder by its id until its source describes it, when it moves to its path
		//with the source's attributes;  described again, its attributes are rewritten in place.  A threshold change rewrites its
		//MinimumSamplingInterval and HA Configuration.
		α GroupLoaded( const HistGroupHost& host )ι->void;
		α GroupRemoved( HistGroupPK group )ι->void;
		α NodeJoined( const HistGroupHost& host, HistMember member )ι->void;
		α NodeLeft( HistGroupPK group, HistGroupNodePK index )ι->void;
		α NodeDescribed( HistGroupPK group, HistGroupNodePK index, HistNodeAttributes attributes )ι->void;
		α ThresholdsChanged( HistGroupPK group, vector<std::pair<HistGroupNodePK,Opc::Hist::Thresholds>> thresholds )ι->void;
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
		//A served node:  the member as it is served, its variable's id - the source's identifier in the mapped namespace - and its HA Configuration's.
		struct Served final{ HistMember Member; NodeId Id; NodeId Configuration; };
		struct Group final{ string Guid; ServerCnnctnNK Slug; flat_map<HistGroupNodePK,Served> Nodes; };
		//`f` on _strand under _mutex with _ua set.  Until Start has built the folders it is held, and Start runs the held ones
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
		α Serve( HistGroupPK id, string guid, ServerCnnctnNK slug, vector<HistMember> members )ι->void;
		α Namespace( const ServerCnnctnNK& slug, sv remote )ι->UA_UInt16;
		α AddNode( Group& group, HistMember member )ι->void;
		α RemoveNode( Group& group, flat_map<HistGroupNodePK,Served>::iterator served )ι->void;
		α RemoveGroup( flat_map<HistGroupPK,Group>::iterator group )ι->flat_map<HistGroupPK,Group>::iterator;//the next.
		α Rewrite( Served& served, const HistNodeAttributes& attributes )ι->void;//the source's attributes in place, the path unchanged.
		α Prune( const ServerCnnctnNK& slug, std::span<const HistPathSegment> parents )ι->void;//the path folders left empty, from the deepest up.
		mutable std::mutex _mutex;
		up<Opc::Server::UAServer> _ua;
		Opc::Server::UASettings _settings;
		flat_map<ServerCnnctnNK,Connection> _connections;
		flat_map<HistGroupPK,Group> _groups;
		sp<GatewayNodeAccess> _access;//the mirror's map, node to group guid, which the rights callbacks read.
		boost::asio::strand<boost::asio::io_context::executor_type> _strand;
		vector<absl::AnyInvocable<void()>> _held;//Serial's before Start has the folders;  under _mutex.
	};
}