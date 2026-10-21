#pragma once
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/opc/uatypes/Logger.h>

struct UA_ClientConfig;

//The security of this repo's UA clients - the gateway's UAClient and the PLC emulator's EmulatorClient - written once:  the
//one configuration the OpcServer is known to accept, from the client's certificate.
namespace Jde::Opc::ClientSecurity{
	struct Recipe final{
		const Crypto::CryptoSettings& Certificate;//the channel's when Secure;  the user token is encrypted with it either way.
		bool Secure;//Sign & Encrypt under a secured policy and nothing less;  false stays on SecurityPolicy None.
		sv ServerUri;//the endpoint filter, the server's applicationUri;  empty takes any endpoint.
		sv AdvertisedUri;//who this client says it is, the SAN uri of Certificate;  empty keeps open62541's placeholder.
		sv TrustRoot;//where ServerTrust reads trustedCertDirs and verifyServerCertificate:  "/gateway", "/emulator".
	};
	//Before the first connect, on a config with no policies yet:  the policies, both uris, the server-certificate verifier, and
	//UA_ClientConfig_setDefault last.  `h` and `url` name the client in the verifier's log.
	α Configure( UA_ClientConfig& config, const Recipe& recipe, Jde::Handle h, str url, SRCE )ε->void;
	α SecuredPolicyUris()ι->std::span<const sv>;//the secured policies a client carries.
}