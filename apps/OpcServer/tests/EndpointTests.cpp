#include <absl/cleanup/cleanup.h>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/plugin/certificategroup_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include "../src/globals.h"
#include "../src/UAServer.h"
#include "helpers.h"

#define let const auto
namespace Jde::Opc::Server::Tests{
	//security-matrix #4:  the server offers the three current RSA policies so a client takes the strongest it shares, and one
	//that carries Basic256Sha256 alone - most installed clients - still connects.  The PLC emulator was that client until it
	//took ClientSecurity's three, and nothing else here is (opcserver-refactor-review #6).
	struct EndpointTests : ::testing::Test{
	protected:
		Ω SetUpTestCase()ε->void{
			Server::Initialize( GetSchemaPtr() );
			GetUAServer().Run();
		}
	};

	//Pinned by securityPolicyUri, which is how an open62541 client takes that endpoint and no other:  the server sees what a
	//client that carries nothing else would ask for.  Its certificate is issued here, into /access/trustedCertDirs - a failed
	//verify rescans it - since none the suite has will do:  the app client's carries no URI to hold an applicationUri against,
	//and the server's own is in no trusted directory.
	TEST_F( EndpointTests, ABasic256Sha256ClientConnects ){
		constexpr sv basic256Sha256{ "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256" };
		let dirs = Settings::FindStringArray( "/access/trustedCertDirs" );
		ASSERT_FALSE( dirs.empty() );
		let keys = fs::temp_directory_path()/"EndpointTests";
		const Crypto::CryptoSettings ssl{ jobject{
			{"certificate", jobject{{"path", (fs::path{dirs.front()}/"EndpointTests.pem").string()}, {"subjectAltName", "URI:urn:jde:EndpointTests,DNS:localhost"}, {"commonName", "EndpointTests"}}},
			{"privateKey", jobject{{"path", (keys/"private.pem").string()}, {"passcode", ""}}},
			{"publicKey", jobject{{"path", (keys/"public.pem").string()}}}
		}};
		ssl.CreateDirectories();
		Crypto::CreateKeyCertificate( ssl );
		absl::Cleanup unstage = [&]{ std::error_code ec; fs::remove( ssl.Certificate.Path, ec ); fs::remove_all( keys, ec ); };
		UA_ClientConfig config{};
		auto certificate = ToUAByteString( Crypto::ReadCertificate(ssl.Certificate.Path) );
		auto pem = ToUAByteString( Crypto::ReadPrivateKey(ssl.PrivateKey) );
		UA_ByteString key = UA_BYTESTRING_NULL;
		UAε( UA_CertificateUtils_decryptPrivateKey(*pem, UA_BYTESTRING_NULL, &key) );
		let configured = UA_ClientConfig_setDefaultEncryption( &config, *certificate, key, nullptr, 0, nullptr, 0 );
		UA_ByteString_clear( &key );
		UAε( configured );
		config.certificateVerification.clear( &config.certificateVerification );
		UA_CertificateGroup_AcceptAll( &config.certificateVerification );
		config.securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
		UA_String_clear( &config.securityPolicyUri );
		config.securityPolicyUri = AllocUAString( basic256Sha256 );
		UA_String_clear( &config.clientDescription.applicationUri );
		config.clientDescription.applicationUri = AllocUAString( ssl.Certificate.SanUri() );
		auto token = UA_IssuedIdentityToken_new();
		token->tokenData = UA_BYTESTRING_ALLOC( Ƒ("{:x}", AppClient()->SessionId()).c_str() );
		UA_ExtensionObject_clear( &config.userIdentityToken );
		UA_ExtensionObject_setValue( &config.userIdentityToken, token, &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
		auto client = UA_Client_newWithConfig( &config );
		ASSERT_TRUE( client );
		absl::Cleanup _ = [client]{ UA_Client_disconnect( client ); UA_Client_delete( client ); };

		let url = Ƒ( "opc.tcp://127.0.0.1:{}", Settings::FindNumber<PortType>("/opcServer/port").value_or(4940) );
		let sc = UA_Client_connect( client, url.c_str() );
		ASSERT_EQ( sc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( sc );
		UA_Variant policy{}, mode{};
		absl::Cleanup clear = [&]{ UA_Variant_clear( &policy ); UA_Variant_clear( &mode ); };
		ASSERT_EQ( UA_Client_getConnectionAttributeCopy(client, UA_QUALIFIEDNAME(0, (char*)"securityPolicyUri"), &policy), UA_STATUSCODE_GOOD );
		ASSERT_EQ( UA_Client_getConnectionAttributeCopy(client, UA_QUALIFIEDNAME(0, (char*)"securityMode"), &mode), UA_STATUSCODE_GOOD );
		EXPECT_EQ( ToString(*(UA_String*)policy.data), basic256Sha256 );
		EXPECT_EQ( *(UA_MessageSecurityMode*)mode.data, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT );
	}
}