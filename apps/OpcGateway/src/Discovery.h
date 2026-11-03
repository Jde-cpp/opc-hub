#pragma once
#include <open62541/client.h>
#include "auth/OpcServerSession.h"

//The discovery services (docs/clients/spec.md *Discovery*):  FindServers at a discovery url and GetEndpoints at a server url, each
//on a throwaway client of open62541's own, SecurityPolicy None, built as the connect diagnostics build theirs
//(UAClient::LogServerEndpoints, which reads its endpoints through here).  Every server answers both on a None channel.  Synchronous
//and blocking - the name lookup, the connect, the service - so a caller runs them off the executor:  the `discover` mutation
//(ql/DiscoverQLAwait.h) on the discovery pool, the connect diagnostics on the blocking pool (BlockingPool.h).
namespace Jde::Opc::Gateway::Discovery{
	//A server FindServers lists - its UA_ApplicationDescription, as serverDescription answers a connected server's.
	struct Server final{
		string ApplicationUri;
		string ProductUri;
		string ApplicationName;
		UA_ApplicationType Type;
		string GatewayServerUri;
		string DiscoveryProfileUri;
		vector<string> DiscoveryUrls;
	};
	//An endpoint GetEndpoints lists:  its url;  the server's applicationUri, which a connection's certificateUri filters on;  the
	//channel's policy and mode and the server's own ranking of it (securityLevel);  every token policy, with the policy that encrypts
	//the token - its own securityPolicyUri, or the channel's when that is unset (open62541 matchUserTokenPolicy) - and its id;  and
	//the certificate the server presents on it, its DER, empty on an endpoint that carries none.
	struct Endpoint final{
		struct TokenPolicy final{ ETokenType Type; string Policy; string PolicyId; };
		string Url;
		string ApplicationUri;
		string PolicyUri;
		UA_MessageSecurityMode Mode;
		uint8 SecurityLevel;
		vector<TokenPolicy> TokenPolicies;
		vector<byte> Certificate;
	};
	//An opc.tcp url's host name, port and path - nullopt for an address, which has nothing to look up, or a url that isn't opc.tcp.
	struct UrlName final{ string Host; uint16 Port; string Path; };
	α ParseUrl( str url )ι->optional<UrlName>;
	//FindServers at `url`:  a Local Discovery Server's, or any server's, which answers with itself.  `timeout` bounds the connect
	//and the call, as a client's requestTimeout does (UA_ClientConfig::timeout);  `h` names the client in the log.  What failed is
	//a UAClientException with the service's status and the reason, which an http caller sees beside it as Test Connection's;  or an
	//Exception for a name that doesn't resolve - looked up first, within 3 s or `timeout` when that is shorter, since the lookup
	//open62541 makes has no deadline and a dead nameserver held a pool thread (#353 review #8).
	α FindServers( str url, Duration timeout, Jde::Handle h, SRCE )ε->vector<Server>;
	//GetEndpoints at `url`, a server's, on the same terms.
	α GetEndpoints( str url, Duration timeout, Jde::Handle h, SRCE )ε->vector<Endpoint>;
	//A log handle for a url with no connection row - its CRC-32C in the top 32 bits, where UAClient's handles carry the row's id.
	α UrlHandle( str url )ι->Jde::Handle;
	α ApplicationTypeName( UA_ApplicationType type )ι->sv;//"Server", "Client", "ClientAndServer", "DiscoveryServer".
}