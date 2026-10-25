#pragma once
#include <jde/fwk/crypto/CryptoSettings.h>

namespace Jde::Opc::Server{
	//What the host reads from its config for a UA server, so the library reads no setting of its own:  the OpcServer's keys
	//sit under /opcServer, /opc and /access/trustedCertDirs (ReadUASettings), the gateway's under /gateway/server, so no
	//prefix would cover both (docs/servers/spec.md *The server library*).
	struct UASettings final{
		Crypto::CryptoSettings Ssl;//the endpoint's certificate and key, made when absent;  its URI SAN is the applicationUri.
		string ApplicationName;
		PortType Port{ 4840 };
		string Address;//the interface the endpoint listens on;  empty:  every interface, open62541's own default.
		vector<fs::path> TrustedCertDirs;//the client certificates admitted on a secured channel and as user tokens.
		//Where a client certificate the endpoint refuses is written, named by its thumbprint, for the admin to move into a trusted
		//dir (spec *Trust*).  Empty:  nowhere, the OpcServer's.
		fs::path RejectedCertDir;
		//A None endpoint beside the secured ones.  false keeps the None policy for discovery alone (securityPolicyNoneDiscoveryOnly):
		//GetEndpoints and FindServers answer on a None channel, a session needs a secured endpoint (spec *Limits*).
		bool None{ true };
		struct TokenTypes final{ bool Anonymous{}; bool Certificate{ true }; bool Issued{ true }; };
		TokenTypes Tokens;
		vector<std::pair<string,string>> Users;//the username token's logins, name and password;  none offers no username token.
		string UserTokenPolicyUri{ "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256" };//"" names each security policy for itself.
		//Tokens, Users and UserTokenPolicyUri from tokenTypes, users and userTokenPolicyUri under the host's `root` - /opc for the
		//OpcServer, /gateway/server for the gateway's endpoint - each absent its default above.
		α ReadTokens( sv root )ε->void;
		//How long a HistoryUpdate waits for the flush that writes it:  open62541's client timeout, past which the client has
		//given up.  The historian itself is the host's (IHistoryHost).
		Duration EditTimeout{ 5s };
		α ReadEditTimeout( sv path )ε->void;//EditTimeout from the host's `path`, absent its default;  throws for one that isn't positive.
		//open62541's own limits (spec *Limits*):  each absent keeps its default, as the OpcServer always has.  0 is unlimited
		//where open62541 reads it so (maxMonitoredItems, maxAsyncOperations).
		struct Limits final{
			optional<uint16> MaxSessions;
			optional<uint32> MaxMonitoredItems;
			optional<Duration> SessionTimeout;//maxSessionTimeout.
			optional<Duration> MinSamplingInterval;//the floor of samplingIntervalLimits.
			optional<uint> MaxAsyncOperations;//maxAsyncOperationQueueSize.
		};
		Limits Limit;
	};
}