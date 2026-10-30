#include <open62541/plugin/accesscontrol_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/co/BlockAwait.h>
#include "../utils/ClientFixture.h"
#include "../utils/GatewayClientSocket.h"
#include "../utils/TestUaServer.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"
#include "../../src/auth/UM.h"
#include "../../src/ql/GatewayQL.h"

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
			for( let& slug : {SharedSlug, LegacySlug, LyingSlug, DowngradeSlug, SignSlug, PolicySlug, TimeoutSlug} ){
				PurgeServerCnnctn( slug );
			}
		}
		α Attempt( str slug, Credential cred )ι->string{//what the connect failed with, "" when it connected.
			let type = TokenTypeName( cred.Type() );
			return ClientFixture::Attempt( slug, move(cred), Ƒ("secured|{}|{}", type, slug) );
		}
		//A row against the embedded OpcServer, its urn as the certificateUri, with the spec's connection settings `args` set (#413):
		//created through the QL, as the page saves one, when absent - the provider row first, as GetConnection makes it.
		Ω PinnedConnection( str slug, sv args )ε->ServerCnnctn{
			if( auto existing = SelectServerCnnctn({slug}); existing )
				return *existing;
			BlockAwait( ProviderMAwait{slug, false} );
			let create = Ƒ( "mutation createServerConnection( slug:\"{}\", name:\"{}\", url:\"{}\", certificateUri:\"{}\", isDefault:false, {} ){{id}}", slug, slug, OpcUrl(), OpcUrn(), args );
			let id = Json::AsNumber<ServerCnnctnPK>( QL().QuerySync<jobject>(create, {}, {UserPK::System}), "id" );
			return *SelectServerCnnctn( id );
		}
		inline static const string SharedSlug{ "opcTestsSharedPolicy" };
		inline static const string LegacySlug{ "opcTestsLegacyPolicy" };
		inline static const string LyingSlug{ "opcTestsLyingLevel" };
		inline static const string DowngradeSlug{ "opcTestsDowngradeOnly" };
		inline static const string SignSlug{ "opcTestsPinnedSign" };
		inline static const string PolicySlug{ "opcTestsPinnedPolicy" };
		inline static const string TimeoutSlug{ "opcTestsPinnedTimeouts" };
		static constexpr sv Basic256Sha256{ "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256" };
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

	//#413 (docs/clients/spec.md *Connection settings*):  a row pinned to Sign under Basic256Sha256 connects on that endpoint of the
	//embedded OpcServer, which offers Sign and Sign & Encrypt under each of its policies, and reads Sign back - where the floor alone
	//took Aes256_Sha256_RsaPss/SignAndEncrypt (TheStrongestSharedPolicyIsNegotiated).  Open question #3:  Sign alone, where the row names it.
	TEST_F( SecurityPolicyTests, APinnedModeAndPolicyAreTaken ){
		let row = PinnedConnection( SignSlug, Ƒ("securityMode:\"Sign\", securityPolicy:\"{}\"", Basic256Sha256) );
		EXPECT_EQ( row.EffectiveMode(), ServerCnnctn::ModeSign );
		let what = Attempt( SignSlug, SessionCredential() );
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (Basic256Sha256/Sign)", Negotiated(_client) );
		EXPECT_EQ( _client->Row().SecurityMode, "Sign" );
	}

	//A policy pinned alone, one the embedded OpcServer offers but not its strongest:  the floor a certificateUri sets stays
	//Sign & Encrypt, under the policy named rather than the strongest the two share.
	TEST_F( SecurityPolicyTests, APinnedPolicyKeepsTheFloor ){
		PinnedConnection( PolicySlug, "securityPolicy:\"http://opcfoundation.org/UA/SecurityPolicy#Aes128_Sha256_RsaOaep\"" );
		let what = Attempt( PolicySlug, SessionCredential() );
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (Aes128_Sha256_RsaOaep/SignAndEncrypt)", Negotiated(_client) );
	}

	//testConnection builds its throwaway client as a saved row's is built:  the mode and the policy passed as arguments pin the
	//channel as the row would, and the save's rule - Sign needs the certificate URI - refuses the test as it would the save.
	TEST_F( SecurityPolicyTests, TestConnectionTakesTheSettings ){
		const string slug{ "opcTestsPinnedTest" };//not saved:  the point of the mutation.
		ASSERT_FALSE( SelectServerCnnctn({slug}) );
		const string q{ "testConnection( opc: $opc, url: $url, certificateUri: $certificateUri, securityMode: $securityMode, securityPolicy: $securityPolicy, sessionTimeout: $sessionTimeout ){ securityPolicyUri securityMode }" };
		jobject vars{ {"opc", slug}, {"url", OpcUrl()}, {"certificateUri", OpcUrn()}, {"securityMode", "Sign"}, {"securityPolicy", Basic256Sha256}, {"sessionTimeout", nullptr} };
		let value = Socket().QuerySync( string{q}, vars );
		let& o = value.as_object();
		EXPECT_EQ( Json::AsSV(o, "securityMode"), "Sign" ) << serialize( value );
		EXPECT_EQ( Json::AsSV(o, "securityPolicyUri"), Basic256Sha256 ) << serialize( value );
		EXPECT_FALSE( UAClient::ConnectionCounts().contains(slug) );
		vars["certificateUri"] = "";
		try{
			Socket().QuerySync( string{q}, vars );
			ADD_FAILURE() << "Sign without a certificateUri was tested";
		}
		catch( const GatewayErrorResponse& e ){
			EXPECT_TRUE( sv{e.what()}.contains("certificate URI") ) << e.what();
		}
	}

	//The timeouts, the keep-alive, the TTL and the locales reach the client:  the row's values in its open62541 config, the ping at
	//half of a session timeout set alone, and the gateway's own values on a row that sets none.
	TEST_F( SecurityPolicyTests, TimeoutsReachTheClient ){
		PinnedConnection( TimeoutSlug, "sessionTimeout:60000, requestTimeout:2000, ttl:30000, locale:\"en-US, de\"" );
		let what = Attempt( TimeoutSlug, SessionCredential() );
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		let config = UA_Client_getConfig( _client->UAPointer() );//written once, at construction;  read-only since.
		EXPECT_EQ( config->requestedSessionTimeout, 60000u );
		EXPECT_EQ( config->timeout, 2000u );
		ASSERT_EQ( config->sessionLocaleIdsSize, 2u );
		EXPECT_EQ( ToString(config->sessionLocaleIds[0]), "en-US" );
		EXPECT_EQ( ToString(config->sessionLocaleIds[1]), "de" );
		EXPECT_EQ( _client->PingInterval(), Duration{30s} );//half the session timeout:  no ping_interval set.
		EXPECT_EQ( _client->Ttl(), Duration{30s} );
		UAClient::RemoveClient( move(_client) );

		GetConnection( SharedSlug, OpcUrl(), OpcUrn() );//sets none of them
		let what2 = Attempt( SharedSlug, SessionCredential() );
		ASSERT_TRUE( what2.empty() ) << what2;
		ASSERT_TRUE( _client );
		let defaults = UA_Client_getConfig( _client->UAPointer() );
		EXPECT_EQ( defaults->timeout, 10000u );
		EXPECT_EQ( defaults->requestedSessionTimeout, 1200000u );//open62541's, setDefault's back-fill of 0.
		EXPECT_EQ( defaults->sessionLocaleIdsSize, 0u );
		EXPECT_EQ( _client->PingInterval(), Settings::FindDuration("/gateway/pingInterval").value_or(Duration{30s}) );
		EXPECT_EQ( _client->Ttl(), Settings::FindDuration("/gateway/ttl").value_or(Duration{2min}) );
	}
}