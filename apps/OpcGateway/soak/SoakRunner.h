#pragma once
#include <jde/app/client/IAppClient.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::Opc::Gateway::Tests{ struct GatewayClientSocket; }
namespace Jde::Opc::Gateway::Soak{
	//One gateway server-connection driven by the soak: the config row from /soak/servers plus per-leg runtime state.
	struct ServerLeg{
		string Slug, Name, Description, CertificateUri, Url, User, Password;
		vector<NodeId> Nodes;
		sp<Tests::GatewayClientSocket> Socket;//legs without a User share the main session's socket; a User leg gets its own logged-in socket.
		SessionPK SessionId{};
		bool Subscribed{};//the current Socket holds an acked subscription for every node.  False after a reconnect until a subscribe succeeds - WriteCycle retries it.
		bool Reconnecting{};//a User leg's socket dropped and no reconnect has succeeded yet - one socketDrop per outage, not per attempt.  Main-session legs share SoakRunner::_mainReconnecting.
		uint Counter{}, WriteIndex{}, ConsecutiveFailures{};
		uint Writes{}, WriteFailures{}, WriteRetries{}, Pushes{}, Misses{}, MissRetries{};//WriteRetries/MissRetries: attempts beyond the first that a write / a push needed - a PASS shows how often it leaned on them.
		vector<uint32> LatenciesMs;
		flat_map<NodeId,uint> Latest;//guarded by SoakRunner::_mutex.
	};
	//Legs from /soak/servers whose `flag` is absent or present on the command line; -<flag>Url/Uri/User/Pwd= args override flagged entries.
	α ActiveServers()ε->vector<ServerLeg>;

	α Run( sp<App::Client::IAppClient> client )ε->int;//blocks for the configured duration; returns the process exit code.
	//createAcl granting this user AllAccess on the opc nodeIds resource. The resource must already exist (OpcServer's
	//first boot registers it) and OpcServer must be RESTARTED afterwards to load the acl - live acl events reach a
	//split-process OpcServer but never update its in-memory rights (see soak findings).
	α GrantWriteRights( const sp<App::Client::IAppClient>& client )ι->void;
}
