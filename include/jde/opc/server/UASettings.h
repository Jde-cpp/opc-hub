#pragma once
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/historian/Historian.h>

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
		struct TokenTypes final{ bool Anonymous{}; bool Certificate{ true }; bool Issued{ true }; };
		TokenTypes Tokens;
		vector<std::pair<string,string>> Users;//the username token's logins, name and password;  none offers no username token.
		string UserTokenPolicyUri{ "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256" };//"" names each security policy for itself.
		optional<Hist::Settings> Hist;//the server's own historian;  none keeps no history.
		Duration EditTimeout{ 5s };//how long a HistoryUpdate waits for the flush that writes it:  open62541's client timeout, past which the client has given up.
	};
}