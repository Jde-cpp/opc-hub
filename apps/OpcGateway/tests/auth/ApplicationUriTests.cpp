#include <open62541/plugin/accesscontrol_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/issue.h>
#include "../utils/ClientFixture.h"
#include "../utils/TestUaServer.h"
#include "../../src/UAClient.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/auth/OpcServerSession.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };
	//security-matrix #8:  two uris, two jobs.  A connection's certificateUri is the SERVER's applicationUri and only filters its
	//endpoints;  what the gateway tells the server it is - clientDescription.applicationUri - is the gateway's own, the uri in
	//the SAN of the certificate it presents (/gateway/issuedCerts, urn:$(HostName):Jde-Cpp:$(PRODUCT_NAME) as shipped).  Until
	//09-18 both came from the certificateUri and the gateway introduced itself as the server it was calling;  no server minded,
	//since a server holds the advertised uri against the client certificate's SAN and that carried the same value, but the
	//certificate an operator trusted in a third-party server was labelled with that server's own name.  The suite's server
	//borrows the embedded OpcServer's certificate - one the gateway trusts, and whose uri is NOT the gateway's - and the test
	//reads what it was told from the session itself.
	namespace{
		//activateSession is a C callback with nowhere to put a capture:  the wrapper parks the last activated session's id here.
		std::mutex _sessionMutex;
		UA_NodeId _sessionId{};
		UA_StatusCode (*_activateSession)( UA_Server*, UA_AccessControl*, const UA_EndpointDescription*, const UA_ByteString*, const UA_NodeId*, const UA_ExtensionObject*, void** ){};
		UA_StatusCode recordSession( UA_Server* server, UA_AccessControl* ac, const UA_EndpointDescription* endpoint, const UA_ByteString* remoteCertificate, const UA_NodeId* sessionId, const UA_ExtensionObject* token, void** sessionContext ){
			let sc = _activateSession( server, ac, endpoint, remoteCertificate, sessionId, token, sessionContext );
			if( !sc ){
				lg _{ _sessionMutex };
				UA_NodeId_clear( &_sessionId );
				UA_NodeId_copy( sessionId, &_sessionId );
			}
			return sc;
		}
	}
	class ApplicationUriTests : public ClientFixture{
	protected:
		static constexpr EThrowawayPort Port{ EThrowawayPort::ApplicationUri };
		Ω SetUpTestSuite()->void;
		Ω TearDownTestSuite()->void{
			_server = nullptr;
			for( let& slug : {SecuredSlug, NoneSlug} ){
				PurgeServerCnnctn( slug );
			}
			lg _{ _sessionMutex };
			UA_NodeId_clear( &_sessionId );
		}
		Ω HeardByTheServer()ε->string;//the applicationUri of the last activated session's clientDescription, as the server holds it.
		inline static const string SecuredSlug{ "opcTestsApplicationUri" };
		inline static const string NoneSlug{ "opcTestsApplicationUriNone" };
		inline static up<TestUaServer> _server;
		inline static string _serverUri;
	};
	α ApplicationUriTests::SetUpTestSuite()->void{
		let sslSettings = Settings::FindObject( "/opcServer/ssl" ); THROW_IF( !sslSettings, "no /opcServer/ssl" );
		const Crypto::CryptoSettings ssl{ *sslSettings };//as UAConfig reads it.
		Crypto::EnsureKeyCertificate( ssl );//there already when the OpcServer is embedded - this is what it starts with.
		_serverUri = ssl.Certificate.SanUri();
		_server = mu<TestUaServer>( Port, ssl, [&]( UA_ServerConfig& config, const UA_ByteString* certificate, const UA_ByteString* privateKey ){
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, certificate) );
			UAε( UA_ServerConfig_addSecurityPolicyBasic256Sha256(&config, certificate, privateKey) );
			TestUaServer::Anonymous( config );//nothing here is about credentials:  None, and Basic256Sha256 signed and encrypted, any client certificate taken (setBasics' AcceptAll).
			_activateSession = config.accessControl.activateSession;
			config.accessControl.activateSession = recordSession;
		});
	}
	α ApplicationUriTests::HeardByTheServer()ε->string{
		UA_NodeId id;
		{
			lg _{ _sessionMutex };
			UAε( UA_NodeId_copy(&_sessionId, &id) );
		}
		UA_Variant value; UA_Variant_init( &value );
		let read = UA_Server_getSessionAttributeCopy( _server->Server(), &id, UA_QUALIFIEDNAME(0, (char*)"clientDescription"), &value );
		UA_NodeId_clear( &id );
		UAε( read );
		let heard = UA_Variant_hasScalarType( &value, &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION] ) ? ToString( ((UA_ApplicationDescription*)value.data)->applicationUri ) : string{};
		UA_Variant_clear( &value );
		return heard;
	}

	TEST_F( ApplicationUriTests, TheGatewayGivesItsOwnNameOnASecuredChannel ){
		let own = UAClient::CryptoSettings( SecuredSlug ).Certificate.SanUri();//what the config gives the gateway.
		ASSERT_FALSE( own.empty() );
		ASSERT_FALSE( _serverUri.empty() );
		ASSERT_NE( own, _serverUri ) << "the test means nothing when the two are the same uri.";
		EXPECT_TRUE( own.starts_with("urn:") && own.contains(":Jde-Cpp:") ) << own;

		GetConnection( SecuredSlug, Url(Port), _serverUri );
		Attempt( SecuredSlug, Credential{} );
		ASSERT_FALSE( _exception ) << _exception->what();
		ASSERT_TRUE( _client );
		let heard = HeardByTheServer();
		INFO( "MATRIX|applicationUri|secured|filter={}|advertised={}|heard={}|{}", _client->ApplicationUri(), _client->AdvertisedUri(), heard, Negotiated(_client) );
		EXPECT_EQ( "ok (Basic256Sha256/SignAndEncrypt)", Negotiated(_client) );//the server held the name against the certificate's SAN, and took it.
		EXPECT_EQ( _client->ApplicationUri(), _serverUri );//the filter:  the server's, the connection's certificateUri.
		EXPECT_EQ( _client->AdvertisedUri(), own );
		EXPECT_EQ( heard, own );//what the server was told - not its own name.
		EXPECT_EQ( Crypto::Certificate{ Crypto::ReadCertificate(UAClient::CryptoSettings(SecuredSlug).Certificate.Path) }.SanUri(), own );//and the certificate it trusts is labelled the same.
	}

	TEST_F( ApplicationUriTests, AndOnAnUnsecuredOne ){
		let own = UAClient::CryptoSettings( NoneSlug ).Certificate.SanUri();
		GetConnection( NoneSlug, Url(Port), "" );
		Attempt( NoneSlug, Credential{} );
		ASSERT_FALSE( _exception ) << _exception->what();
		ASSERT_TRUE( _client );
		let heard = HeardByTheServer();
		INFO( "MATRIX|applicationUri|none|filter={}|advertised={}|heard={}|{}", _client->ApplicationUri(), _client->AdvertisedUri(), heard, Negotiated(_client) );
		EXPECT_EQ( "ok (None/None)", Negotiated(_client) );
		EXPECT_TRUE( _client->ApplicationUri().empty() );//no certificateUri, no filter.
		EXPECT_EQ( heard, own );
	}
}