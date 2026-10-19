#include <open62541/plugin/accesscontrol_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include "../utils/ClientFixture.h"
#include "../utils/TestUaServer.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	//security-matrix #4:  more than one secured policy.  The gateway and the OpcServer each carried Basic256Sha256 alone;  both now
	//carry the three current RSA policies - Basic256Sha256, Aes128_Sha256_RsaOaep, Aes256_Sha256_RsaPss - and open62541 takes the
	//endpoint with the highest securityLevel the two sides share, so between the Jde products that is the strongest, and against
	//a server that offers Basic256Sha256 alone (External - ExternalServerTests) it is still that.  A server that shares none - the
	//deprecated Basic256 here - used to fail as a bare BadIdentityTokenRejected;  the connection error now names what the server
	//asked for and what the gateway carries.
	class SecurityPolicyTests : public ClientFixture{
	protected:
		static constexpr EThrowawayPort LegacyPort{ EThrowawayPort::SecurityPolicyLegacy }, LyingPort{ EThrowawayPort::SecurityPolicyLying }, DowngradePort{ EThrowawayPort::SecurityPolicyDowngrade };
		Ω TearDownTestSuite()->void{
			for( let& slug : {SharedSlug, LegacySlug, LyingSlug, DowngradeSlug} ){
				PurgeServerCnnctn( slug );
			}
		}
		α Attempt( str slug, Credential cred )ι->string{//what the connect failed with, "" when it connected.
			let type = TokenTypeName( cred.Type() );
			return ClientFixture::Attempt( slug, move(cred), Ƒ("secured|{}|{}", type, slug) );
		}
		inline static const string SharedSlug{ "opcTestsSharedPolicy" };
		inline static const string LegacySlug{ "opcTestsLegacyPolicy" };
		inline static const string LyingSlug{ "opcTestsLyingLevel" };
		inline static const string DowngradeSlug{ "opcTestsDowngradeOnly" };
	};


	TEST_F( SecurityPolicyTests, TheStrongestSharedPolicyIsNegotiated ){
		let urn = OpcUrn();
		let url = OpcUrl();
		let row = GetConnection( SharedSlug, url, urn );
		EXPECT_EQ( row.CertificateUri, urn );//security-matrix #7:  the row read back through the helper keeps its certificateUri.
		EXPECT_FALSE( row.IsDefault );
		let what = Attempt( SharedSlug, SessionCredential() );
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (Aes256_Sha256_RsaPss/SignAndEncrypt)", Negotiated(_client) );
	}

	TEST_F( SecurityPolicyTests, NoPolicyInCommonIsNamed ){
		let& ssl = *AppClient()->SslSettings;//the server's certificate has to be one the gateway trusts, and its own web certificate sits in the harness's trusted directory.
		let serverUri = ssl.Certificate.SanUri();
		ASSERT_FALSE( serverUri.empty() );
		TestUaServer legacy{ LegacyPort, ssl, [&]( UA_ServerConfig& config, const UA_ByteString* certificate, const UA_ByteString* privateKey ){
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, certificate) );//the discovery channel only - no None endpoint is added.
			UAε( UA_ServerConfig_addSecurityPolicyBasic256(&config, certificate, privateKey) );
			UA_UsernamePasswordLogin login{ UA_STRING_STATIC("legacy"), UA_STRING_STATIC("password") };
			UAε( UA_AccessControl_default(&config, false, nullptr, 1, &login) );
			UAε( UA_ServerConfig_addEndpoint(&config, UA_STRING((char*)"http://opcfoundation.org/UA/SecurityPolicy#Basic256"), UA_MESSAGESECURITYMODE_SIGNANDENCRYPT) );
		}};
		GetConnection( LegacySlug, Url(LegacyPort), serverUri );
		let what = Attempt( LegacySlug, Credential{User{"legacy", "password"}} );
		EXPECT_FALSE( _client );
		EXPECT_TRUE( what.contains("BadIdentityTokenRejected") ) << what;
		EXPECT_TRUE( what.contains("does not carry") ) << what;
		EXPECT_TRUE( what.contains("Basic256]") ) << what;//the policy the server asked for - and not Basic256Sha256, which the gateway has.
		EXPECT_TRUE( what.contains("Aes256_Sha256_RsaPss") ) << what;//what the gateway carries.
	}

	//reviews/m2-closing.md #1:  the server a downgrade needs.  Its None endpoint claims securityLevel 255, carries the certificate
	//the gateway trusts - a public file, and a None channel asks no proof of its key - and takes the password under
	//Basic256Sha256, so open62541's cleartext guard has nothing to refuse.  `secured` adds the honest endpoint, claiming 0.
	//The level is served as the config holds it (GetEndpoints copies the description), and it was all that chose the channel
	//until Configuration() pinned Sign & Encrypt for a connection with a certificateUri.
	Ω lyingServer( EThrowawayPort port, const Crypto::CryptoSettings& ssl, bool secured )ε->up<TestUaServer>{
		return mu<TestUaServer>( port, ssl, [&]( UA_ServerConfig& config, const UA_ByteString* certificate, const UA_ByteString* privateKey ){
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, certificate) );
			UAε( UA_ServerConfig_addSecurityPolicyBasic256Sha256(&config, certificate, privateKey) );//decrypts the token;  a channel policy only where `secured` gives it an endpoint.
			let tokenPolicy = UA_STRING( (char*)"http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256" );
			UA_UsernamePasswordLogin login{ UA_STRING_STATIC("level"), UA_STRING_STATIC("password") };
			UAε( UA_AccessControl_default(&config, false, &tokenPolicy, 1, &login) );//Username@Basic256Sha256 on every endpoint, the None one included.
			UAε( UA_ServerConfig_addEndpoint(&config, UA_SECURITY_POLICY_NONE_URI, UA_MESSAGESECURITYMODE_NONE) );
			if( secured )
				UAε( UA_ServerConfig_addEndpoint(&config, tokenPolicy, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT) );
			for( auto& ep : Iterable<UA_EndpointDescription>(config.endpoints, config.endpointsSize) )
				ep.securityLevel = ep.securityMode==UA_MESSAGESECURITYMODE_NONE ? 255 : 0;
		});
	}

	TEST_F( SecurityPolicyTests, ALyingSecurityLevelDoesNotChooseTheMode ){
		let& ssl = *AppClient()->SslSettings;
		let serverUri = ssl.Certificate.SanUri(); ASSERT_FALSE( serverUri.empty() );
		auto server = lyingServer( LyingPort, ssl, true );
		GetConnection( LyingSlug, Url(LyingPort), serverUri );
		let what = Attempt( LyingSlug, Credential{User{"level", "password"}} );
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (Basic256Sha256/SignAndEncrypt)", Negotiated(_client) );//"ok (None/None)" before the floor - 255 beat 0.
	}

	TEST_F( SecurityPolicyTests, ADowngradeOnlyServerIsRefused ){
		let& ssl = *AppClient()->SslSettings;
		let serverUri = ssl.Certificate.SanUri(); ASSERT_FALSE( serverUri.empty() );
		auto server = lyingServer( DowngradePort, ssl, false );//what an on-path answer to GetEndpoints looks like:  the None endpoint alone.
		GetConnection( DowngradeSlug, Url(DowngradePort), serverUri );
		let what = Attempt( DowngradeSlug, Credential{User{"level", "password"}} );
		EXPECT_FALSE( _client ) << Negotiated( _client );
		EXPECT_TRUE( what.contains("BadIdentityTokenRejected") ) << what;
		EXPECT_TRUE( what.contains("refused rather than downgraded") ) << what;
		let errors = UAClient::ConnectErrors();//serverConnections{connectionStatus} shows the same line.
		auto error = errors.find( DowngradeSlug ); ASSERT_NE( error, errors.end() );
		EXPECT_TRUE( error->second.contains("Sign & Encrypt") ) << error->second;
	}
}
