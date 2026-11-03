//Clients phase 3, trust (#415):  the gateway's end of server-certificate trust, the mirror of RefusedCertificateTests' (which is the
//server rejecting OUR certificate).  A throwaway server whose certificate no trusted directory holds is rejected by our verifier
//(ServerTrust), which writes <thumbprint>.der under /gateway/rejectedCertDir;  the thumbprint is what rejectedCertificate{} answers,
//trustServer moves the file into the ssl/servers trusted dir and the next connect succeeds, and a non-admin's trustServer is refused.
//And a server that presents a trusted certificate under another applicationUri connects, with the SAN-URI warning logged (spec *Trust*,
//*Open questions* #14).
#include <open62541/plugin/accesscontrol_default.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/crypto/Certificate.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/issue.h>
#include <jde/opc/ServerTrust.h>
#include <absl/cleanup/cleanup.h>
#include "../utils/ClientFixture.h"
#include "../utils/GatewayClientSocket.h"
#include "../utils/TestUaServer.h"
#include "../../src/UAClient.h"
#include "../../src/auth/OpcServerSession.h"
#include "../../src/ql/GatewayQL.h"
#include "../../src/ql/TrustServerQLAwait.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	//A self-signed pair in `dir`, its SAN URI `uri` and localhost/127.0.0.1 - a server certificate no trusted directory holds.
	Ω makePair( const fs::path& dir, sv uri )ε->Crypto::CryptoSettings{
		const jobject settings{
			{"certificate", jobject{ {"path", (dir/"server.pem").string()}, {"commonName", "trustServerTests.server"}, {"subjectAltName", Ƒ("URI:{},DNS:localhost,IP:127.0.0.1", uri)} }},
			{"privateKey", jobject{ {"path", (dir/"server.key").string()} }},
			{"publicKey", jobject{ {"path", (dir/"server.pub").string()} }}
		};
		Crypto::CryptoSettings y{ settings };
		y.CreateDirectories();
		Crypto::EnsureKeyCertificate( y );
		return y;
	}
	Ω anonymousSecured( UA_ServerConfig& config, const UA_ByteString* certificate, const UA_ByteString* privateKey )ε->void{
		UAε( UA_ServerConfig_addSecurityPolicyNone(&config, certificate) );
		UAε( UA_ServerConfig_addSecurityPolicyBasic256Sha256(&config, certificate, privateKey) );
		TestUaServer::Anonymous( config );//nothing here is about credentials:  the verifier is the gateway's, against the server's own certificate.
	}

	class TrustServerTests : public ClientFixture{
	protected:
		static constexpr EThrowawayPort Port{ EThrowawayPort::UntrustedServer };
		inline static const string Slug{ "opcTestsUntrustedServer" };
		Ω TearDownTestSuite()->void{ PurgeServerCnnctn( Slug ); }
		α TearDown()ι->void override{
			std::error_code ec;
			let rejectedDir = ServerTrust::RejectedDir( "/gateway" );
			fs::remove_all( rejectedDir, ec );//the <thumbprint>.der our verifier wrote, and the trusted copy trustServer moved beside it.
			fs::remove_all( rejectedDir.parent_path(), ec );
			UAClient::SetRejectedThumbprint( Slug, {} );
			ClientFixture::TearDown();
		}
		α Connect()ι->void{
			if( auto cached = UAClient::Find(Slug, Credential{}); cached )
				UAClient::RemoveClient( move(cached) );//trust is loaded per connect:  a cached client would skip the fresh scan.
			Attempt( Slug, Credential{} );
		}
	};

	//The whole flow:  a rejected certificate lands in the rejected directory with the thumbprint rejectedCertificate{} answers,
	//trustServer moves it and the next connect succeeds.
	TEST_F( TrustServerTests, RejectedCertificateIsTrustedAndTheNextConnectSucceeds ){
		let dir = fs::temp_directory_path()/Ƒ( "trustServerTests-{}", Process::ProcessId() );
		absl::Cleanup removeDir = [&]{ std::error_code ec; fs::remove_all( dir, ec ); };
		let ssl = makePair( dir, "urn:jde:trustServerTests:untrusted" );
		let serverUri = ssl.Certificate.SanUri();
		TestUaServer server{ Port, ssl, &anonymousSecured };
		GetConnection( Slug, Url(Port), serverUri );//certificateUri = the server's applicationUri:  a secured channel, so the server presents its certificate and our verifier runs.

		Connect();
		ASSERT_TRUE( _exception ) << "connected to a server whose certificate no trusted directory holds";
		EXPECT_FALSE( _client );
		EXPECT_NE( string{_exception->what()}.find("server certificate for"), string::npos ) << _exception->what();//our verifier's line, not the server rejecting ours.

		let thumbprint = UAClient::RejectedThumbprint( Slug );
		ASSERT_FALSE( thumbprint.empty() ) << "the rejection's thumbprint was not recorded for the slug";
		let rejectedFile = ServerTrust::RejectedDir( "/gateway" )/Ƒ( "{}.der", thumbprint );
		ASSERT_TRUE( fs::exists(rejectedFile) ) << rejectedFile;
		EXPECT_EQ( Crypto::Certificate{Crypto::ReadCertificate(rejectedFile)}.Thumbprint, thumbprint ) << "the file is named by the certificate's own SHA-1";
		EXPECT_EQ( Crypto::ReadCertificate(rejectedFile), Crypto::ReadCertificate(ssl.Certificate.Path) ) << "the DER the server presented, byte for byte";

		//the query answers that thumbprint (serverConnections{ rejectedCertificate{…} }, under the table's Read).
		let listed = Socket().QuerySync( "serverConnections( deleted:null ){ slug rejectedCertificate{ subject uri thumbprint } }", {} );
		let& rows = listed.is_array() ? listed.get_array() : Json::AsArray( listed.as_object(), "serverConnections" );
		let row = find_if( rows, [&]( let& r ){ return Json::AsSV(r.as_object(), "slug")==Slug; } );
		ASSERT_NE( row, rows.end() ) << serialize( listed );
		let& rejected = Json::AsObject( row->as_object(), "rejectedCertificate" );
		EXPECT_EQ( Json::AsSV(rejected, "thumbprint"), thumbprint ) << serialize( *row );
		EXPECT_EQ( Json::AsSV(rejected, "uri"), serverUri ) << serialize( *row );
		EXPECT_FALSE( Json::AsSV(rejected, "subject").empty() ) << serialize( *row );

		//trustServer moves it into the ssl/servers trusted dir (the rejected directory's parent).
		Socket().QuerySync( "trustServer( opc:$opc, thumbprint:$thumbprint )", {{"opc", Slug}, {"thumbprint", thumbprint}} );
		EXPECT_FALSE( fs::exists(rejectedFile) ) << "the rejected file was left behind";
		let trusted = ServerTrust::RejectedDir( "/gateway" ).parent_path()/Ƒ( "{}.der", thumbprint );
		ASSERT_TRUE( fs::exists(trusted) ) << trusted;
		EXPECT_TRUE( UAClient::RejectedThumbprint(Slug).empty() ) << "the standing rejection was not cleared";

		//the next connect trusts it - the list is loaded per connect.
		Connect();
		EXPECT_FALSE( _exception ) << _exception->what();
		EXPECT_TRUE( _client );

		//and clientCertificate{} reads the gateway's own issued certificate for the slug, the one it presents, PEM and all.
		let cnnctn = Socket().QuerySync( "serverConnections( deleted:null ){ slug clientCertificate{ path uri pem } }", {} );
		let& crows = cnnctn.is_array() ? cnnctn.get_array() : Json::AsArray( cnnctn.as_object(), "serverConnections" );
		let crow = find_if( crows, [&]( let& r ){ return Json::AsSV(r.as_object(), "slug")==Slug; } );
		ASSERT_NE( crow, crows.end() ) << serialize( cnnctn );
		let& client = Json::AsObject( crow->as_object(), "clientCertificate" );
		EXPECT_FALSE( Json::AsSV(client, "path").empty() ) << serialize( *crow );
		EXPECT_NE( Json::AsSV(client, "pem").find("-----BEGIN CERTIFICATE-----"), sv::npos ) << "the PEM for the page to download";
	}

	//A non-admin's trustServer is refused:  Administer on gateway/serverConnections, enforced, a user no grant names.  The gate is
	//checked before the file, so no rejected certificate need exist.
	TEST_F( TrustServerTests, ANonAdminIsRefused ){
		constexpr Jde::UserPK ungranted{ (Jde::UserPK::Type)0x08'0000 };//no acl, no role, no group names it.
		EnforceResource( "gateway", "serverConnections", true );
		absl::Cleanup open = []{ try{ EnforceResource("gateway", "serverConnections", false); }catch( const std::exception& ){} };
		let refused = WaitUntil( [&]{
			try{
				auto ql = QL::Parse( "trustServer( opc:\"x\", thumbprint:\"ABCD\" )", {}, Schemas(), true );
				BlockAwait<TrustServerQLAwait,jvalue>( TrustServerQLAwait{move(ql.Mutations().front()), QL::Creds{ungranted}} );
			}
			catch( const Exception& e ){
				return e.HttpStatus()==EHttpStatus::Unauthorized || e.HttpStatus()==EHttpStatus::Forbidden;
			}
			return false;
		}, std::chrono::seconds{5}, std::chrono::milliseconds{50} );
		EXPECT_TRUE( refused ) << "gateway/serverConnections is enforced and an ungranted user still ran trustServer";
	}

	//The SAN-URI warning (spec *Trust*, *Open questions* #14):  a server that presents a trusted certificate under another
	//applicationUri connects - the trust list is the gate - and the mismatch is logged at Warning, never refused.
	class TrustWarningTests : public ClientFixture{
	protected:
		static constexpr EThrowawayPort Port{ EThrowawayPort::MismatchedUri };
		inline static const string Slug{ "opcTestsMismatchedUri" };
		Ω TearDownTestSuite()->void{ PurgeServerCnnctn( Slug ); }
		α TearDown()ι->void override{ ServerTrust::OverrideTrustedCertDirs( nullopt ); ClientFixture::TearDown(); }
	};
	TEST_F( TrustWarningTests, AnotherApplicationUriConnectsWithTheWarningLogged ){
		let dir = fs::temp_directory_path()/Ƒ( "trustWarningTests-{}", Process::ProcessId() );
		absl::Cleanup removeDir = [&]{ std::error_code ec; fs::remove_all( dir, ec ); };
		let certUri = "urn:jde:trustWarningTests:cert";//the certificate's SAN URI
		let advertised = string{ "urn:jde:trustWarningTests:advertised" };//the applicationUri the server advertises - not its certificate's
		let ssl = makePair( dir, certUri );
		ServerTrust::OverrideTrustedCertDirs( vector<fs::path>{ dir } );//the gateway trusts exactly this server's certificate.
		absl::Cleanup restore = []{ ServerTrust::OverrideTrustedCertDirs( nullopt ); };

		TestUaServer server{ Port, ssl, [&advertised]( UA_ServerConfig& config, const UA_ByteString* certificate, const UA_ByteString* privateKey ){
			UA_String_clear( &config.applicationDescription.applicationUri );//the ssl ctor set it to the certificate's SAN;  advertise another, which open62541 only warns about.
			config.applicationDescription.applicationUri = UA_STRING_ALLOC( advertised.c_str() );
			anonymousSecured( config, certificate, privateKey );
		}};
		GetConnection( Slug, Url(Port), advertised );//certificateUri = the advertised applicationUri, so the endpoint filter matches.

		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		Logging::GetLogger<Logging::MemoryLog>().Clear();

		if( auto cached = UAClient::Find(Slug, Credential{}); cached )
			UAClient::RemoveClient( move(cached) );
		Attempt( Slug, Credential{} );
		ASSERT_FALSE( _exception ) << _exception->what();//trusted, so it connects - the SAN mismatch is a warning, not a refusal.
		EXPECT_TRUE( _client );

		let warned = Logging::Find( [&]( const Logging::Entry& e ){ let m = e.Message(); return e.Level==ELogLevel::Warning && m.contains("subjectAltName URI") && m.contains(advertised); } );
		EXPECT_FALSE( warned.empty() ) << "the SAN-URI mismatch was not logged at Warning";
	}
}