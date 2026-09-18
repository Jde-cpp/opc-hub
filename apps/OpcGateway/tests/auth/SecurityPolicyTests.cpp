#include <open62541/plugin/accesscontrol_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include "../utils/helpers.h"
#include "../utils/TestUaServer.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	//security-matrix #4:  more than one secured policy.  The gateway and the OpcServer each carried Basic256Sha256 alone;  both now
	//carry the three current RSA policies - Basic256Sha256, Aes128_Sha256_RsaOaep, Aes256_Sha256_RsaPss - and open62541 takes the
	//endpoint with the highest securityLevel the two sides share, so between the Jde products that is the strongest, and against
	//a server that offers Basic256Sha256 alone (Kepware - ExternalServerTests) it is still that.  A server that shares none - the
	//deprecated Basic256 here - used to fail as a bare BadIdentityTokenRejected;  the connection error now names what the server
	//asked for and what the gateway carries.
	class SecurityPolicyTests : public ::testing::Test{
	protected:
		static constexpr uint16_t LegacyPort{ 4856 };//PlaintextPasswordTests holds 4855, RefusedCertificateTests 4857.
		Ω SetUpTestSuite()->void{ _jwt = BlockAwait<Web::Client::ClientSocketAwait<Web::Jwt>,Web::Jwt>( AppClient()->Jwt() ); }
		Ω TearDownTestSuite()->void{
			for( let& slug : {SharedSlug, LegacySlug} ){
				if( auto row = SelectServerCnnctn(slug); row )
					PurgeServerCnnctn( row->Id );
			}
		}
		α TearDown()ι->void override{
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		α Connect( str slug, Credential cred )ι->ConnectAwait::Task;
		α Attempt( str slug, Credential cred )ε->string;//what the connect failed with, "" when it connected.
		inline static const string SharedSlug{ "opcTestsSharedPolicy" };
		inline static const string LegacySlug{ "opcTestsLegacyPolicy" };
		inline static optional<Web::Jwt> _jwt;
		up<Exception> _exception;
		sp<UAClient> _client;
		atomic_flag _done;
	};

	α SecurityPolicyTests::Connect( str slug, Credential cred )ι->ConnectAwait::Task{
		try{
			_client = co_await UAClient::GetClient( slug, move(cred) );
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		_done.test_and_set();
		_done.notify_all();
	}
	α SecurityPolicyTests::Attempt( str slug, Credential cred )ε->string{
		let type = TokenTypeName( cred.Type() );
		_exception = nullptr; _done.clear();
		Connect( slug, move(cred) );
		_done.wait( false );
		let what = _exception ? string{_exception->what()} : string{};
		INFO( "MATRIX|secured|{}|{}|{}", type, slug, what.empty() ? Negotiated(_client) : what );
		return what;
	}

	TEST_F( SecurityPolicyTests, TheStrongestSharedPolicyIsNegotiated ){
		let urn = string{ Settings::FindSV("/opc/urn").value_or("urn:open62541.server.application") };
		let url = string{ Settings::FindSV("/opc/url").value_or("opc.tcp://127.0.0.1:4840") };
		let row = GetConnection( SharedSlug, url, urn );
		EXPECT_EQ( row.CertificateUri, urn );//security-matrix #7:  the row read back through the helper keeps its certificateUri.
		EXPECT_FALSE( row.IsDefault );
		let what = Attempt( SharedSlug, Credential{_jwt->Payload()} );
		ASSERT_TRUE( what.empty() ) << what;
		ASSERT_TRUE( _client );
		EXPECT_EQ( "ok (Aes256_Sha256_RsaPss/SignAndEncrypt)", Negotiated(_client) );
	}

	TEST_F( SecurityPolicyTests, NoPolicyInCommonIsNamed ){
		let& ssl = *AppClient()->SslSettings;//the server's certificate has to be one the gateway trusts, and its own web certificate sits in the harness's trusted directory.
		auto certificate = ToUAByteString( Crypto::ReadCertificate(ssl.Certificate.Path) );
		auto privateKey = ToUAByteString( Crypto::ReadPrivateKey(ssl.PrivateKey) );
		let serverUri = ssl.Certificate.SanUri();
		ASSERT_FALSE( serverUri.empty() );
		TestUaServer legacy{ LegacyPort, [&]( UA_ServerConfig& config ){
			UA_String_clear( &config.applicationDescription.applicationUri );
			config.applicationDescription.applicationUri = UA_STRING_ALLOC( serverUri.c_str() );//open62541 holds the certificate's SAN uri against it at startup.
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, certificate.get()) );//the discovery channel only - no None endpoint is added.
			UAε( UA_ServerConfig_addSecurityPolicyBasic256(&config, certificate.get(), privateKey.get()) );
			UA_UsernamePasswordLogin login{ UA_STRING_STATIC("legacy"), UA_STRING_STATIC("password") };
			UAε( UA_AccessControl_default(&config, false, nullptr, 1, &login) );
			UAε( UA_ServerConfig_addEndpoint(&config, UA_STRING((char*)"http://opcfoundation.org/UA/SecurityPolicy#Basic256"), UA_MESSAGESECURITYMODE_SIGNANDENCRYPT) );
		}};
		GetConnection( LegacySlug, Ƒ("opc.tcp://127.0.0.1:{}", LegacyPort), serverUri );
		let what = Attempt( LegacySlug, Credential{User{"legacy", "password"}} );
		EXPECT_FALSE( _client );
		EXPECT_TRUE( what.contains("BadIdentityTokenRejected") ) << what;
		EXPECT_TRUE( what.contains("does not carry") ) << what;
		EXPECT_TRUE( what.contains("Basic256]") ) << what;//the policy the server asked for - and not Basic256Sha256, which the gateway has.
		EXPECT_TRUE( what.contains("Aes256_Sha256_RsaPss") ) << what;//what the gateway carries.
	}
}
