#pragma once
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <jde/opc/server/UAServer.h>
#include "../types/ServerCnnctn.h"
#include "../UAClient.h"

namespace Jde::Opc::Gateway{
	//The gateway's opc.tcp endpoint (docs/servers/spec.md, plan Phase 2):  the UA server library over the /gateway/server block,
	//in the hub and standalone alike.  Without the block there is none.  It starts at the end of Gateway::Connect, after HistHost
	//has loaded the groups, and stops before the historian does:  its shutdown function is registered ahead of the historian's.
	//The address space at this phase is one folder per server_connections row, named by its slug and there while the row is,
	//each holding a `_Connection` object with Connected, Since, Url, LastError and Reconnects (spec *Address space*).  They
	//follow UAClient's registry through its ConnectionChange listener, and the rows through OpcQLHook;  both land here off
	//their callers' threads, on one strand in the order they were posted, and every UA_Server_* call runs there under _mutex,
	//never under a client's strand or a host's lock (spec *Threading*).  The served nodes, their values and their history are
	//Phases 3a to 4.
	struct GatewayServer final : std::enable_shared_from_this<GatewayServer>{
		GatewayServer( Opc::Server::UASettings settings )ι;
		//Reads the block and starts the endpoint, folders built from the rows first;  nothing without a block.  The listener and
		//Instance() come before the rows are read, so a change while they load is held, not missed.  Blocks on the rows:
		//Gateway::Connect's thread.  `processSsl`:  the process's own pair, the endpoint's when the block names none.
		Ω Start( const Crypto::CryptoSettings& processSsl )ε->void;
		Ω Stop()ι->void;//the server and its thread go;  the registry's listener first.
		Ω Instance()ι->sp<GatewayServer>;//null without a block, before Start reads the rows and after Stop.
		//The block as a UASettings, nullopt without one.  Its own function for the OpcServer's test of the None switch and the gateway's of the block.
		Ω ReadSettings( const Crypto::CryptoSettings& processSsl )ε->optional<Opc::Server::UASettings>;
		Ω FolderId( const ServerCnnctnNK& slug )ι->NodeId;//ns=1;s=<slug>
		Ω ConnectionId( const ServerCnnctnNK& slug )ι->NodeId;//ns=1;s=<slug>/_Connection
		Ω VariableId( const ServerCnnctnNK& slug, sv name )ι->NodeId;//ns=1;s=<slug>/_Connection/<name>

		α Url()Ι->string{ return Ƒ( "opc.tcp://{}:{}", _settings.Address.empty() ? Process::HostName() : _settings.Address, _settings.Port ); }
		α Settings()Ι->const Opc::Server::UASettings&{ return _settings; }
		α Server()ι->Opc::Server::UAServer*;//the tests read the address space through it;  null once stopped.

		//OpcQLHook's, after the row is written:  the folder for a new row, the url of an edited one, the folder of a purged one gone.
		//A delete or restore changes nothing:  the folder stays with the row.  Keyed as the mutation was, by id or slug.
		α ConnectionCreated( ServerCnnctn row )ι->void;
		α ConnectionUrlChanged( const DB::Key& key, string url )ι->void;
		α ConnectionPurged( const DB::Key& key )ι->void;
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
		mutable std::mutex _mutex;
		up<Opc::Server::UAServer> _ua;
		Opc::Server::UASettings _settings;
		flat_map<ServerCnnctnNK,Connection> _connections;
		boost::asio::strand<boost::asio::io_context::executor_type> _strand;
		vector<absl::AnyInvocable<void()>> _held;//Serial's before Start has the folders;  under _mutex.
	};
}