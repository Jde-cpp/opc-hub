#include <jde/opc/ClientSecurity.h>
#include <open62541/client_config_default.h>
#include <open62541/plugin/securitypolicy_default.h>
#include <jde/opc/ServerTrust.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/opcHelpers.h>

#define let const auto
namespace Jde::Opc{
	namespace{
		constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Client };
		struct SecuredPolicy final{ UA_StatusCode(*Ctor)( UA_SecurityPolicy*, const UA_ByteString, const UA_ByteString, const UA_Logger* ); sv Uri; };
		//open62541 takes the endpoint with the highest securityLevel among the policies it finds here, so a server that offers an
		//Aes policy gets it, and one that offers Basic256Sha256 alone - External - gets that (reviews/security-matrix.md #4;
		//SecurityPolicyTests).  The deprecated ones (Basic128Rsa15, Basic256) are not carried.
		constexpr array<SecuredPolicy,3> _securedPolicies{{
			{ &UA_SecurityPolicy_Basic256Sha256, "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256" },
			{ &UA_SecurityPolicy_Aes128Sha256RsaOaep, "http://opcfoundation.org/UA/SecurityPolicy#Aes128_Sha256_RsaOaep" },
			{ &UA_SecurityPolicy_Aes256Sha256RsaPss, "http://opcfoundation.org/UA/SecurityPolicy#Aes256_Sha256_RsaPss" }
		}};
		constexpr array<sv,3> _securedUris{ _securedPolicies[0].Uri, _securedPolicies[1].Uri, _securedPolicies[2].Uri };
	}
	α ClientSecurity::SecuredPolicyUris()ι->std::span<const sv>{ return _securedUris; }

	α ClientSecurity::Configure( UA_ClientConfig& config, const Recipe& recipe, Jde::Handle h, str url, SL sl )ε->void{
		ASSERT( !config.securityPoliciesSize );
		let& path = recipe.Certificate.Certificate.Path;
		let der = Crypto::ReadCertificate( path, sl );
		auto certificate = ToUAByteString( der );
		auto privateKey = ToUAByteString( Crypto::ReadPrivateKey(recipe.Certificate.PrivateKey) );
		//Two uris, two jobs (reviews/security-matrix.md #8; ApplicationUriTests).  config->applicationUri only FILTERS the server's
		//endpoints (matchEndpoint), so it is the server's.  clientDescription's is who this client says it is, and a server holds it
		//against the SAN of the certificate the client presents - against each other, never against its own uri.  Until #8 the
		//gateway set both from one string and introduced itself as the server it was calling.
		if( recipe.ServerUri.size() ){
			UA_String_clear( &config.applicationUri );
			config.applicationUri = AllocUAString( recipe.ServerUri );
		}
		string advertised{ recipe.AdvertisedUri };
		if( advertised.empty() ){//the file's, not the settings':  an operator's own pair was made elsewhere.
			advertised = Crypto::Certificate{ der, sl }.SanUri();
			if( advertised.empty() && recipe.Secure )
				WARN( "[{}]'{}' carries no URI in its subjectAltName, so this client has no applicationUri of its own to advertise - advertising '{}', the server's.", hex(h), path.string(), recipe.ServerUri );
			if( advertised.empty() )
				advertised = recipe.ServerUri;
		}
		if( advertised.size() ){
			UA_String_clear( &config.clientDescription.applicationUri );
			config.clientDescription.applicationUri = AllocUAString( advertised );
		}
		const uint size = recipe.Secure ? 1+_securedPolicies.size() : 1;
		uint initialized{};//constructed so far:  on a throw before the config owns them, the deleter clears these - UA_free alone would leak each one's internals.
		auto clearPolicies = [&initialized]( UA_SecurityPolicy* p )ι{ for( uint i=0; i<initialized; ++i ) p[i].clear( &p[i] ); UA_free( p ); };
		up<UA_SecurityPolicy, decltype(clearPolicies)> policies{ (UA_SecurityPolicy*)UA_malloc(sizeof(UA_SecurityPolicy)*size), clearPolicies };
		Check( policies ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADOUTOFMEMORY, "securityPolicies", EOpcLogTags::Client, sl );
		Check( UA_SecurityPolicy_None(&policies.get()[0], UA_BYTESTRING_NULL, config.logging), "UA_SecurityPolicy_None", EOpcLogTags::Client, sl );//the discovery channel's.
		++initialized;
		if( recipe.Secure ){
			//The floor (reviews/m2-closing.md #1), and open62541's only mode filter (matchEndpoint: configuredSM > 0).  Unset, the
			//None policy the discovery channel needs also matches a None *endpoint*, and whoever answers GetEndpoints - the server,
			//or anything on-path - takes the channel to None/None by claiming securityLevel 255 on it.  securityLevel, the server's
			//claim off an unauthenticated channel, only ever ranks the endpoints that meet the floor.
			config.securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
			for( let& policy : _securedPolicies ){
				Check( policy.Ctor(&policies.get()[initialized], *certificate, *privateKey, config.logging), policy.Uri, EOpcLogTags::Client, sl );
				++initialized;
			}
		}
		//The auth policies, which encrypt the user token on any channel, None included:  open62541 builds one only from a local
		//certificate, though the token itself is encrypted to the server's.
		auto grown = (UA_SecurityPolicy*)UA_realloc( config.authSecurityPolicies, sizeof(UA_SecurityPolicy)*(config.authSecurityPoliciesSize+_securedPolicies.size()) );
		Check( grown ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADOUTOFMEMORY, "authSecurityPolicies", EOpcLogTags::Client, sl );//a failed realloc leaves the old block, still the config's.
		config.authSecurityPolicies = grown;
		for( let& policy : _securedPolicies ){
			Check( policy.Ctor(&config.authSecurityPolicies[config.authSecurityPoliciesSize], *certificate, *privateKey, config.logging), policy.Uri, EOpcLogTags::Client, sl );
			++config.authSecurityPoliciesSize;//one at a time:  what is constructed is the config's to clear, whatever throws next.
		}
		config.securityPolicies = policies.release();
		config.securityPoliciesSize = size;
		config.secureChannelLifeTime = 60*60*1000;
		ServerTrust::Install( config, recipe.TrustRoot, h, url, sl );//before setDefault, which would install AcceptAll.
		//Last:  setDefault back-fills only what is unset, and installs its own None policy only into a config with none.
		Check( UA_ClientConfig_setDefault(&config), "UA_ClientConfig_setDefault", EOpcLogTags::Client, sl );
	}
}