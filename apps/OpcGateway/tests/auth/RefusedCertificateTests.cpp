#include <open62541/plugin/accesscontrol_default.h>
#include <open62541/plugin/certificategroup_default.h>
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
	//security-matrix #12:  the other direction of trust.  When the gateway rejects a server's certificate its verifier says which
	//server and what to do (ServerTrustTests).  When a server rejects the GATEWAY's, it answers the OPN with a status -
	//BadSecurityChecksFailed from the Jde OpcServer and from External alike - and nothing a client can show, so the operator saw a
	//bare status and had to know which file the server wanted.  The connection error now names it:  the connection's issued
	//certificate here, the app client's own under certificate authentication (CertTests.Authenticate_Bad).  The embedded
	//OpcServer cannot play the refusing server - it trusts the whole directory the issued certificates land in, and rescans it -
	//so the suite brings one that trusts a single certificate, its own.
	class RefusedCertificateTests : public ::testing::Test{
	protected:
		static constexpr uint16_t Port{ 4857 };//4855 PlaintextPasswordTests, 4856 SecurityPolicyTests.
		Ω TearDownTestSuite()->void{
			if( auto row = SelectServerCnnctn(Slug); row )
				PurgeServerCnnctn( row->Id );
		}
		α TearDown()ι->void override{
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		α Connect()ι->ConnectAwait::Task;
		inline static const string Slug{ "opcTestsRefusedCertificate" };
		up<Exception> _exception;
		sp<UAClient> _client;
		atomic_flag _done;
	};
	α RefusedCertificateTests::Connect()ι->ConnectAwait::Task{
		try{
			_client = co_await UAClient::GetClient( Slug, Credential{User{"operator", "password"}} );
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		_done.test_and_set();
		_done.notify_all();
	}

	TEST_F( RefusedCertificateTests, TheIssuedCertificateIsNamed ){
		let& ssl = *AppClient()->SslSettings;//the server's certificate has to be one the gateway trusts, and its own web certificate sits in the harness's trusted directory.
		auto certificate = ToUAByteString( Crypto::ReadCertificate(ssl.Certificate.Path) );
		auto privateKey = ToUAByteString( Crypto::ReadPrivateKey(ssl.PrivateKey) );
		let serverUri = ssl.Certificate.SanUri();
		ASSERT_FALSE( serverUri.empty() );
		TestUaServer distrustful{ Port, [&]( UA_ServerConfig& config ){
			UA_String_clear( &config.applicationDescription.applicationUri );
			config.applicationDescription.applicationUri = UA_STRING_ALLOC( serverUri.c_str() );//open62541 holds the certificate's SAN uri against it at startup.
			//A trust list of one - the server's own certificate - so the connection's issued certificate, a different one, is not on it.
			UA_TrustListDataType list; UA_TrustListDataType_init( &list );
			list.specifiedLists |= UA_TRUSTLISTMASKS_TRUSTEDCERTIFICATES;
			UAε( UA_Array_copy(certificate.get(), 1, (void**)&list.trustedCertificates, &UA_TYPES[UA_TYPES_BYTESTRING]) );
			list.trustedCertificatesSize = 1;
			UA_KeyValuePair params[2];
			params[0].key = UA_QualifiedName{ 0, UA_STRING_STATIC("max-trust-listsize") };
			UA_Variant_setScalar( &params[0].value, &config.maxTrustListSize, &UA_TYPES[UA_TYPES_UINT32] );
			params[1].key = UA_QualifiedName{ 0, UA_STRING_STATIC("max-rejected-listsize") };
			UA_Variant_setScalar( &params[1].value, &config.maxRejectedListSize, &UA_TYPES[UA_TYPES_UINT32] );
			UA_KeyValueMap paramsMap{ 2, params };
			if( config.secureChannelPKI.clear )
				config.secureChannelPKI.clear( &config.secureChannelPKI );
			UA_NodeId group = UA_NODEID_NUMERIC( 0, UA_NS0ID_SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTAPPLICATIONGROUP );
			let created = UA_CertificateGroup_Memorystore( &config.secureChannelPKI, &group, &list, config.logging, &paramsMap );
			UA_TrustListDataType_clear( &list );
			UAε( created );//not `sc`: the macro declares its own, and would initialize it from itself.
			UAε( UA_ServerConfig_addSecurityPolicyNone(&config, certificate.get()) );//the discovery channel.
			UAε( UA_ServerConfig_addSecurityPolicyBasic256Sha256(&config, certificate.get(), privateKey.get()) );
			UA_UsernamePasswordLogin login{ UA_STRING_STATIC("operator"), UA_STRING_STATIC("password") };
			UAε( UA_AccessControl_default(&config, false, nullptr, 1, &login) );
			UAε( UA_ServerConfig_addEndpoint(&config, UA_STRING((char*)"http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256"), UA_MESSAGESECURITYMODE_SIGNANDENCRYPT) );
		}};
		GetConnection( Slug, Ƒ("opc.tcp://127.0.0.1:{}", Port), serverUri );
		Connect();
		_done.wait( false );
		ASSERT_TRUE( _exception ) << "a server that trusts only itself took the gateway's certificate";
		EXPECT_FALSE( _client );
		let what = string{ _exception->what() };
		INFO( "MATRIX|refused|Username|{}|{}", Slug, what );
		EXPECT_TRUE( what.contains("refused the secure channel") ) << what;
		EXPECT_TRUE( what.contains(UAClient::CryptoSettings(Slug).Certificate.Path.string()) ) << what;//the file the server has to trust...
		EXPECT_TRUE( what.contains("trust that file in the server") ) << what;//...and that it is the server's move.
		EXPECT_TRUE( what.find("server certificate for")==string::npos ) << what;//not OUR verifier's line - that is the other direction.
	}

	//open62541-1.5.9 review #8:  a 1.5.9 server answers a certificate with no subjectAltName at all with BadCertificateInvalid,
	//not BadCertificateUriInvalid, so the URI advice keyed on the status alone missed it and told the operator to trust the
	//file - which cannot help.  The certificate decides now;  an untrusted one is still refused at the OPN, before the URI.
	TEST( RefusedCertificateDetailTests, TheCertificateDecides ){
		let detail = []( StatusCode sc, optional<string> san ){ return RefusedCertificateDetail( sc, "opc.tcp://server:4840", "/certs/gateway.pem", san, "urn:gateway" ); };
		let noSan = "carries no URI in its subjectAltName";
		let trust = "trust that file in the server";

		let invalid = detail( UA_STATUSCODE_BADCERTIFICATEINVALID, string{} );
		EXPECT_TRUE( invalid.contains(noSan) ) << invalid;
		EXPECT_FALSE( invalid.contains(trust) ) << invalid;
		EXPECT_TRUE( invalid.contains("/certs/gateway.pem") && invalid.contains("urn:gateway") ) << invalid;
		EXPECT_TRUE( detail(UA_STATUSCODE_BADCERTIFICATEURIINVALID, string{}).contains(noSan) );

		let untrusted = detail( UA_STATUSCODE_BADSECURITYCHECKSFAILED, string{} );
		EXPECT_TRUE( untrusted.contains(trust) ) << "refused at the OPN:  trust comes first";
		EXPECT_TRUE( untrusted.contains(noSan) ) << "and the URI it will be refused for next";

		let withSan = detail( UA_STATUSCODE_BADSECURITYCHECKSFAILED, "urn:gateway" );
		EXPECT_TRUE( withSan.contains(trust) );
		EXPECT_FALSE( withSan.contains("subjectAltName") ) << withSan;
		EXPECT_TRUE( detail(UA_STATUSCODE_BADCERTIFICATEINVALID, "urn:gateway").contains(trust) ) << "a SAN URI:  not the URI's fault";

		let unread = detail( UA_STATUSCODE_BADCERTIFICATEURIINVALID, nullopt );
		EXPECT_TRUE( unread.contains("could not be read") ) << unread;
		EXPECT_FALSE( unread.contains(noSan) ) << "an unreadable file is not one without a SAN";

		//open62541-1.5.9 review #9:  the gateway advertises the URI it read from the same file, so the two agree, and the
		//message printed one URI twice and told the operator to re-issue the certificate with the URI it already carries.
		let policy = detail( UA_STATUSCODE_BADCERTIFICATEURIINVALID, "urn:gateway" );
		EXPECT_TRUE( policy.contains("policy of its own") ) << policy;
		EXPECT_EQ( policy.find("urn:gateway"), policy.rfind("urn:gateway") ) << "the one URI, once:  " << policy;
		let changed = detail( UA_STATUSCODE_BADCERTIFICATEURIINVALID, "urn:other" );
		EXPECT_TRUE( changed.contains("its subjectAltName URI is 'urn:other'") && changed.contains("advertised 'urn:gateway'") ) << changed;
		EXPECT_TRUE( changed.contains("Connect again") ) << "a file replaced since the connection was configured";
	}
}
