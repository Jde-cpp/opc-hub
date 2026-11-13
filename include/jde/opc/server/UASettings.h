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
		//Client certificates admitted as the directories' are, each named by its file, read at the start and again on a failed
		//verify:  a server whose clients are its own host's (docs/open-protocol/spec.md *The driver server*).  A file not there
		//yet, one its host issues later, joins when it is.
		vector<fs::path> TrustedCerts;
		//Where a client certificate the endpoint refuses is written, named by its thumbprint, for the admin to move into a trusted
		//dir (spec *Trust*).  Empty:  nowhere, the OpcServer's.
		fs::path RejectedCertDir;
		//A None endpoint beside the secured ones.  false keeps the None policy for discovery alone (securityPolicyNoneDiscoveryOnly):
		//GetEndpoints and FindServers answer on a None channel, a session needs a secured endpoint (spec *Limits*).
		bool None{ true };
		//One secured policy and endpoint, Basic256Sha256 at Sign & Encrypt, where false carries each secured policy at Sign and at
		//Sign & Encrypt:  a server whose every client is its host's own, which asks for nothing less.
		bool EncryptOnly{};
		//The own-certificate rule:  a certificate token whose key is Ssl's opens its session as this user, the one the host's pair
		//logs in as, with no lookup.  For a server its host runs for itself, whose clients log in with the host's own pair:  it
		//admits the host as itself, with nothing asked under the service lock and no hub needed.  None asks the hub for every
		//certificate, the OpcServer's and the gateway's endpoint's.
		optional<UserPK> OwnCertificate;
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
		//How long a client's Write holds for a callback value source's answer, open62541's asyncOperationTimeout (servers spec
		//*Writes*):  past it the client is answered Bad_Timeout and the operation is cancelled, so an answer after finds nothing to set.
		//Absent keeps open62541's own;  the gateway reads /gateway/server/writeTimeout, the OpcServer, whose values are stored, sets none.
		optional<Duration> WriteTimeout;
		α ReadWriteTimeout( sv path )ε->void;//WriteTimeout from the host's `path`, absent left as it is;  throws for one that isn't positive.
		//open62541's own limits (spec *Limits*):  each absent keeps its default, as the OpcServer always has.  0 is unlimited
		//where open62541 reads it so (maxMonitoredItems, maxAsyncOperations).
		struct Limits final{
			optional<uint16> MaxSessions;
			optional<uint16> MaxSecureChannels;//raised to MaxSessions where it is below:  a session per channel.
			optional<uint32> MaxMonitoredItems;
			optional<Duration> SessionTimeout;//maxSessionTimeout.
			optional<Duration> MinSamplingInterval;//the floor of samplingIntervalLimits.
			optional<uint> MaxAsyncOperations;//maxAsyncOperationQueueSize.
			//The deepest queue a monitored item is given, the ceiling of queueSizeLimits:  raised to this where it is past
			//open62541's own, and never lowered.  The host's to set, for a burst it writes inside one publishing interval;  no key reads it.
			optional<uint> MaxQueueSize;
		};
		Limits Limit;
		α ReadLimits( sv root )ε->void;//Limit from `root`/limits - maxSessions, maxSecureChannels, maxMonitoredItems, sessionTimeout, minSamplingInterval, maxAsyncOperations.
	};
}