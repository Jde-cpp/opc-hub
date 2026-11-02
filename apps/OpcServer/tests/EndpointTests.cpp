#include <absl/cleanup/cleanup.h>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/plugin/certificategroup_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/crypto/issue.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/opcHelpers.h>
#include "../src/globals.h"
#include <jde/opc/server/UAServer.h>
#include "helpers.h"

#define let const auto
namespace Jde::Opc::Server::Tests{
	constexpr sv basic256Sha256{ "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256" };
	//security-matrix #4:  the server offers the three current RSA policies so a client takes the strongest it shares, and one
	//that carries Basic256Sha256 alone - most installed clients - still connects.  The PLC emulator was that client until it
	//took ClientSecurity's three, and nothing else here is (opcserver-refactor-review #6).
	struct EndpointTests : ::testing::Test{
	protected:
		Ω SetUpTestCase()ε->void{
			Server::Initialize( GetSchemaPtr() );
			PublishDataTypes( GetUAServer() );
			GetUAServer().Run();
		}
	};

	//A client pinned to one endpoint by securityPolicyUri, logged in with an issued token:  the app client's session id.  Its
	//certificate is issued here, into /access/trustedCertDirs - a failed verify rescans it - since none the suite has will do:
	//the app client's carries no URI to hold an applicationUri against, and the server's own is in no trusted directory.
	struct TrustedClient final : noncopyable{
		TrustedClient( sv name, sv securityPolicyUri )ε:
			_ssl{ settings(name) }{
			_ssl.CreateDirectories();
			Crypto::CreateKeyCertificate( _ssl );
			try{
				UA_ClientConfig config{};
				auto certificate = ToUAByteString( Crypto::ReadCertificate(_ssl.Certificate.Path) );
				auto pem = ToUAByteString( Crypto::ReadPrivateKey(_ssl.PrivateKey) );
				UA_ByteString key = UA_BYTESTRING_NULL;
				UAε( UA_CertificateUtils_decryptPrivateKey(*pem, UA_BYTESTRING_NULL, &key) );
				let configured = UA_ClientConfig_setDefaultEncryption( &config, *certificate, key, nullptr, 0, nullptr, 0 );
				UA_ByteString_clear( &key );
				UAε( configured );
				config.certificateVerification.clear( &config.certificateVerification );
				UA_CertificateGroup_AcceptAll( &config.certificateVerification );
				config.securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
				UA_String_clear( &config.securityPolicyUri );
				config.securityPolicyUri = AllocUAString( securityPolicyUri );
				UA_String_clear( &config.clientDescription.applicationUri );
				config.clientDescription.applicationUri = AllocUAString( _ssl.Certificate.SanUri() );
				UA_ExtensionObject_clear( &config.userIdentityToken );
				UA_ExtensionObject_setValue( &config.userIdentityToken, issued(Ƒ("{:x}", AppClient()->SessionId())), &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
				Ptr = UA_Client_newWithConfig( &config );
				THROW_IF( !Ptr, "UA_Client_newWithConfig failed." );
			}
			catch( ... ){
				unstage();
				throw;
			}
		}
		~TrustedClient(){
			if( Ptr ){
				UA_Client_disconnect( Ptr );
				UA_Client_delete( Ptr );
			}
			unstage();
		}
		α Connect()ι->UA_StatusCode{
			let url = Ƒ( "opc.tcp://127.0.0.1:{}", Settings::FindNumber<PortType>("/opcServer/port").value_or(4940) );
			return UA_Client_connect( Ptr, url.c_str() );
		}
		//ActivateSession again, on the session Connect created, with `token` as the issued token's data.
		α Reactivate( str token )ι->UA_StatusCode{
			auto& identity = UA_Client_getConfig( Ptr )->userIdentityToken;
			UA_ExtensionObject_clear( &identity );
			UA_ExtensionObject_setValue( &identity, issued(token), &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
			return UA_Client_activateCurrentSession( Ptr );
		}
		UA_Client* Ptr{};
	private:
		Ω settings( sv name )ε->Crypto::CryptoSettings{
			let dirs = Settings::FindStringArray( "/access/trustedCertDirs" );
			THROW_IF( dirs.empty(), "/access/trustedCertDirs is empty." );
			let keys = fs::temp_directory_path()/name;
			return Crypto::CryptoSettings{ jobject{
				{"certificate", jobject{{"path", (fs::path{dirs.front()}/Ƒ("{}.pem", name)).string()}, {"subjectAltName", Ƒ("URI:urn:jde:{},DNS:localhost", name)}, {"commonName", string{name}}}},
				{"privateKey", jobject{{"path", (keys/"private.pem").string()}, {"passcode", ""}}},
				{"publicKey", jobject{{"path", (keys/"public.pem").string()}}}
			}};
		}
		Ω issued( str token )ι->UA_IssuedIdentityToken*{
			auto y = UA_IssuedIdentityToken_new();
			y->tokenData = UA_BYTESTRING_ALLOC( token.c_str() );
			return y;
		}
		α unstage()ι->void{
			std::error_code ec;
			fs::remove( _ssl.Certificate.Path, ec );
			fs::remove_all( _ssl.PrivateKey.Path.parent_path(), ec );
		}
		Crypto::CryptoSettings _ssl;
	};

	//The server sees what a client that carries nothing else would ask for.
	TEST_F( EndpointTests, ABasic256Sha256ClientConnects ){
		TrustedClient client{ "EndpointTests", basic256Sha256 };
		let sc = client.Connect();
		ASSERT_EQ( sc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( sc );
		UA_Variant policy{}, mode{};
		absl::Cleanup clear = [&]{ UA_Variant_clear( &policy ); UA_Variant_clear( &mode ); };
		ASSERT_EQ( UA_Client_getConnectionAttributeCopy(client.Ptr, UA_QUALIFIEDNAME(0, (char*)"securityPolicyUri"), &policy), UA_STATUSCODE_GOOD );
		ASSERT_EQ( UA_Client_getConnectionAttributeCopy(client.Ptr, UA_QUALIFIEDNAME(0, (char*)"securityMode"), &mode), UA_STATUSCODE_GOOD );
		EXPECT_EQ( ToString(*(UA_String*)policy.data), basic256Sha256 );
		EXPECT_EQ( *(UA_MessageSecurityMode*)mode.data, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT );
	}

	//open62541 1.5.10 compares an issued token's user id on re-activation, and leaves it empty, so a different token reaches
	//UAAccess::ActivateSession, which refuses another user's itself (AccessTests.ReactivatingAsAnotherUserIsRefused).  The
	//same user's jwt after its session id is a different token:  a stack that starts comparing issued tokens answers
	//BadIdentityChangeNotSupported here, and that check and its comment are due a look.
	TEST_F( EndpointTests, ReactivatingWithAnotherIssuedTokenReachesUAAccess ){
		TrustedClient client{ "EndpointReactivation", basic256Sha256 };
		let connected = client.Connect();
		ASSERT_EQ( connected, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( connected );
		let jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() );
		let sc = client.Reactivate( jwt.Payload() );
		EXPECT_EQ( sc, UA_STATUSCODE_GOOD ) << UA_StatusCode_name( sc );
	}
}