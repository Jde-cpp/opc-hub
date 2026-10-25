#pragma once
#include <absl/container/flat_hash_map.h>
#include <jde/opc/pubsub/PubSub.h>
#include <jde/opc/server/INodeAccess.h>
#include <jde/opc/server/UAServer.h>

namespace Jde::App::Client{ struct IAppClient; }
namespace Jde::Opc::Emulator{
	//The PLC server's INodeAccess (servers plan Phase 6, the endpoint's Phase 2 shape):  every node open to every session - Read,
	//Write, browse - since the login is the gate:  a session is a hub user, by issued token or certificate, or a username from the
	//block's list, and anonymous is off unless the block turns it on.  No history, so nothing is editable.  NodeRights stops short
	//of Update, which is the user write mask and AddReferences (UAAccess):  a client writes a PLC's values, not its address space.
	struct PlcNodeAccess final : Server::INodeAccess{
		α NodeRights( const NodeId&, UserPK )ι->Access::ERights override{ return Access::ERights::Read | Access::ERights::Subscribe; }
		α EditRights( const NodeId&, UserPK )ι->Access::ERights override{ return Access::ERights::None; }
		α MayBrowse( const NodeId&, UserPK )ι->bool override{ return true; }
		α UserRights( const NodeId&, UserPK )ι->Server::EAccess override{ return Server::EAccess::Read | Server::EAccess::Write; }
	};

	//The emulated PLC's own OPC UA server - what a UA-enabled PLC is.  Holds the same nodeset the OpcServer loads, so its tags
	//mirror the server's, on the UA server library's endpoint (Jde.Opc.UAServer):  the secured policies, the trust list and the
	//hub login, so a gateway group collects from it as from a real PLC (docs/servers/spec.md *Emulator*).  Under -transport=pubsub
	//it also publishes the contract's fields from those nodes over PubSub;  under -transport=server there is no writer, and the
	//command tags arrive as clients' writes to its own nodes (Watch, Drain).  Run starts the server's thread;  Write is safe from
	//the emulator's.
	struct PlcServer final : noncopyable{
		//The /emulator/plc block as the library's settings:  port 4842, address (absent:  loopback;  "":  every interface), ssl (absent:  `deviceSsl`,
		//the device's channel pair, whose URI SAN is its applicationUri), trustedCertDirs, rejectedCertDir, none, tokenTypes, users,
		//userTokenPolicyUri and limits, each absent its default.
		Ω ReadSettings( const Crypto::CryptoSettings& deviceSsl, sv root="/emulator/plc" )ε->Server::UASettings;
		//appClient:  the hub, which every session's login asks (UAAccess).  contract:  the pubsub transport's, resolved on this server
		//and published by its writer;  none under the server transport.
		PlcServer( const Server::UASettings& settings, sp<App::Client::IAppClient> appClient, const fs::path& nodeset, optional<PubSub::Config>&& contract, SRCE )ε;
		~PlcServer();
		α Run()ε->void;//opens the endpoint, starts the server's thread, then adds the writer.  Once.
		α Namespace( sv uri, SRCE )ε->NsIndex;
		α Resolve( sv path, NsIndex defaultNs, SRCE )ε->NodeId;//a browse path from Objects, as the client resolves one on the OpcServer.
		α FindField( sv name )Ι->optional<uint>;//index into the contract's Fields, or none without a contract or when it does not publish it.
		//the local node's whole DataValue - value, quality and now as its source time - as the writer samples it and a session reads it.
		α Write( const NodeId& node, const UA_Variant& value, UA_StatusCode status=UA_STATUSCODE_GOOD, SRCE )ε->void;
		//`value` as `type`, ValueType's:  a Boolean is nonzero, an integer rounded and held to its range;  throws for any other type.
		α Write( const NodeId& node, const UA_DataType& type, double value, UA_StatusCode status=UA_STATUSCODE_GOOD, SRCE )ε->void;
		α Write( uint field, double value, UA_StatusCode status=UA_STATUSCODE_GOOD, SRCE )ε->void;//the contract's field, as its type.
		//The type a local write of `node` takes:  its DataType, Double for an abstract one (Number, BaseDataType), which a Double
		//satisfies;  throws for one the emulator doesn't write - Boolean, the integers, an enumeration, Float and Double.
		α ValueType( const NodeId& node, SRCE )ε->const UA_DataType&;
		//A command tag:  a client's Boolean write to `node` - the gateway's write-through of what the web UI wrote - is queued under
		//`key` with the writer's hub user, for the emulator's thread to Drain.  Returns the node's Boolean now, the command's starting
		//value;  throws for a node watched already.
		α Watch( const NodeId& node, uint key, SRCE )ε->optional<bool>;
		struct Command final{ uint Key; bool Value; UserPK User; };
		α Drain()ι->vector<Command>;
		α Contract()Ι->const optional<PubSub::Config>&{ return _contract; }
		α Port()Ι->PortType{ return _port; }
		α Ptr()Ι->UA_Server*{ return _server->Ptr(); }//the tests read its config (serverUrls) and its nodes back.
	private:
		Ω Written( UA_Server*, const UA_NodeId* sessionId, void* sessionContext, const UA_NodeId* nodeId, void* nodeContext, const UA_NumericRange* range, const UA_DataValue* data )ι->void;
		PortType _port;
		optional<PubSub::Config> _contract;
		up<Server::UAServer> _server;
		up<PubSub::Writer> _writer;
		std::mutex _mutex;
		absl::flat_hash_map<NodeId,uint> _watched;//node -> key;  under _mutex, with the commands Written queued and Drain takes.
		vector<Command> _commands;
	};
}