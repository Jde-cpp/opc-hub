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
	class ApplicationUriTests : public ::testing::Test{
	protected:
		static constexpr uint16_t Port{ 4859 };//4855-4858 the other throwaway servers.
		Ω SetUpTestSuite()->void;
		Ω TearDownTestSuite()->void{
			_server = nullptr;
			for( let& slug : {SecuredSlug, NoneSlug} ){
				if( auto row = SelectServerCnnctn(slug); row )
					PurgeServerCnnctn( row->Id );
			}
			lg _{ _sessionMutex };
			UA_NodeId_clear( &_sessionId );
		}
		α TearDown()ι->void override{
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		α Connect( str slug )ι->ConnectAwait::Task;
		α Attempt( str slug )ι->void;
		Ω HeardByTheServer()ε->string;//the applicationUri of the last activated session's clientDescription, as the server holds it.
		inline static const string SecuredSlug{ "opcTestsApplicationUri" };
		inline static const string NoneSlug{ "opcTestsApplicationUriNone" };
		inline static up<TestUaServer> _server;
		inline static string _serverUri;
		up<Exception> _exception;
		sp<UAClient> _client;
		atomic_flag _done;
	};
	α ApplicationUriTests::SetUpTestSuite()->void{
		let sslSettings = Settings::FindObject( "/opcServer/ssl" ); THROW_IF( !sslSettings, "no /opcServer/ssl" );
		const Crypto::CryptoSettings ssl{ *sslSettings };//as UAConfig reads it.
		Crypto::EnsureKeyCertificate( ssl );//there already when the OpcServer is embedded - this is what it starts with.
		auto certificate = ToUAByteString( Crypto::ReadCertificate(ssl.Certificate.Path) );
		auto privateKey = ToUAByteString( Crypto::ReadPrivateKey(ssl.PrivateKey) );
		_serverUri = ssl.Certificate.SanUri();
		_server = mu<TestUaServer>( Port, [&]( UA_ServerConfig& config ){
			UA_String_clear( &config.applicationDescription.applicationUri );
			config.applicationDescription.applicationUri = UA_STRING_ALLOC( _serverUri.c_str() );//open62541 holds the certificate's SAN uri against it at startup.
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, certificate.get()) );
			UAε( UA_ServerConfig_addSecurityPolicyBasic256Sha256(&config, certificate.get(), privateKey.get()) );
			UAε( UA_AccessControl_default(&config, true, nullptr, 0, nullptr) );//anonymous:  nothing here is about credentials.
			_activateSession = config.accessControl.activateSession;
			config.accessControl.activateSession = recordSession;
			UAε( UA_ServerConfig_addAllEndpoints(&config) );//None, and Basic256Sha256 signed and encrypted;  any client certificate is taken (setBasics' AcceptAll).
		});
	}
	α ApplicationUriTests::Connect( str slug )ι->ConnectAwait::Task{
		try{
			_client = co_await UAClient::GetClient( slug, Credential{} );
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		_done.test_and_set();
		_done.notify_all();
	}
	α ApplicationUriTests::Attempt( str slug )ι->void{
		Connect( slug );
		_done.wait( false );
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

		GetConnection( SecuredSlug, Ƒ("opc.tcp://127.0.0.1:{}", Port), _serverUri );
		Attempt( SecuredSlug );
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
		GetConnection( NoneSlug, Ƒ("opc.tcp://127.0.0.1:{}", Port), "" );
		Attempt( NoneSlug );
		ASSERT_FALSE( _exception ) << _exception->what();
		ASSERT_TRUE( _client );
		let heard = HeardByTheServer();
		INFO( "MATRIX|applicationUri|none|filter={}|advertised={}|heard={}|{}", _client->ApplicationUri(), _client->AdvertisedUri(), heard, Negotiated(_client) );
		EXPECT_EQ( "ok (None/None)", Negotiated(_client) );
		EXPECT_TRUE( _client->ApplicationUri().empty() );//no certificateUri, no filter.
		EXPECT_EQ( heard, own );
	}
}
