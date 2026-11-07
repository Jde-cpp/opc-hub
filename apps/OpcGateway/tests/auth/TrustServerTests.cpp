//Clients phase 3, trust (#415):  the gateway's end of server-certificate trust, the mirror of RefusedCertificateTests' (which is the
//server rejecting OUR certificate).  A throwaway server whose certificate no trusted directory holds is rejected by our verifier
//(ServerTrust), which writes <thumbprint>.der under /gateway/rejectedCertDir;  the thumbprint is what rejectedCertificate{} answers,
//trustServer moves the file into the ssl/servers trusted dir - refused under a slug that never rejected it, clearing every one that
//did - and the next connect succeeds, a trust withdrawn by hand has the certificate rejected and offered again, and a non-admin's
//trustServer is refused.
//And a server that presents a trusted certificate under another applicationUri connects, with the SAN-URI warning logged (spec *Trust*,
//*Open questions* #14).
#include <fstream>
#include <absl/cleanup/cleanup.h>
#include <open62541/plugin/accesscontrol_default.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/tests/Capture.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/crypto/Certificate.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/issue.h>
#include <jde/opc/ServerTrust.h>
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
		y.CreateDirs();
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
		//The files a test leaves, removed by name - never a directory:  both are settings-derived, and without
		//gateway.serversDir the trusted one is the product's live ssl/servers trust store (reviews-issue/415-clients-trust.md #6).
		α TearDown()ι->void override{
			for( let& file : _made ){
				std::error_code ec;
				fs::remove( file, ec );
			}
			UAClient::SetRejectedThumbprint( Slug, {} );
			ClientFixture::TearDown();
		}
		//The two files a throwaway server's certificate can leave:  the <thumbprint>.der our verifier writes to the rejected
		//directory, and the trusted copy trustServer moves into the servers dir.
		α Made( const Crypto::CryptoSettings& ssl )ε->void{
			let file = Ƒ( "{}.der", Crypto::Certificate{Crypto::ReadCertificate(ssl.Certificate.Path)}.Thumbprint );
			_made = { ServerTrust::RejectedDir("/gateway")/file, ServerTrust::ServersDir("/gateway")/file };
		}
		vector<fs::path> _made;
		α Connect()ι->void{
			if( auto cached = UAClient::Find(Slug, Credential{}); cached )
				UAClient::RemoveClient( move(cached) );//trust is loaded per connect:  a cached client would skip the fresh scan.
			Attempt( Slug, Credential{} );
		}
	};

	//The whole flow:  a rejected certificate lands in the rejected directory with the thumbprint rejectedCertificate{} answers,
	//trustServer moves it and the next connect succeeds;  and the trusted copy deleted by hand - the help's way to withdraw it -
	//has the next connect rejected and the certificate written and offered again (reviews-issue/415-clients-trust.md #1).
	TEST_F( TrustServerTests, RejectedCertificateIsTrustedThenWithdrawn ){
		let dir = fs::temp_directory_path()/Ƒ( "trustServerTests-{}", Process::ProcessId() );
		absl::Cleanup removeDir = [&]{ std::error_code ec; fs::remove_all( dir, ec ); };
		let ssl = makePair( dir, "urn:jde:trustServerTests:untrusted" );
		Made( ssl );
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

		//refused under a slug whose standing rejection it is not (reviews-issue/415-clients-trust.md #4):  the log would credit, and
		//the reconnect name, a connection that never rejected it.
		try{
			Socket().QuerySync( "trustServer( opc:$opc, thumbprint:$thumbprint )", {{"opc", "opcTestsNoSuchSlug"}, {"thumbprint", thumbprint}} );
			ADD_FAILURE() << "trustServer moved a certificate under a slug that never rejected it";
		}
		catch( const GatewayErrorResponse& e ){
			EXPECT_NE( string{e.what()}.find("has no standing rejection"), string::npos ) << e.what();
		}
		ASSERT_TRUE( fs::exists(rejectedFile) ) << "a refused trust moved the file";

		//another connection to the same server rejected the same certificate, so shares the file:  a trust from either page clears both.
		let twin = Slug+"Twin";
		UAClient::SetRejectedThumbprint( twin, thumbprint );
		absl::Cleanup clearTwin = [&twin]{ UAClient::SetRejectedThumbprint( twin, {} ); };

		//trustServer moves it into the ssl/servers trusted dir (/gateway/serversDir) - found among trustedCertDirs though the list
		//spells it with a trailing separator (reviews-issue/415-clients-trust.md #8).
		{
			jarray original, slashed;
			for( let& dir : Settings::FindStringArray("/gateway/trustedCertDirs") ){
				original.emplace_back( dir );
				slashed.emplace_back( dir+"/" );
			}
			Settings::Set( "/gateway/trustedCertDirs", slashed );
			absl::Cleanup restore = [&original]{ try{ Settings::Set("/gateway/trustedCertDirs", original); }catch( const std::exception& ){} };
			Socket().QuerySync( "trustServer( opc:$opc, thumbprint:$thumbprint )", {{"opc", Slug}, {"thumbprint", thumbprint}} );
		}
		EXPECT_FALSE( fs::exists(rejectedFile) ) << "the rejected file was left behind";
		let trusted = ServerTrust::ServersDir( "/gateway" )/Ƒ( "{}.der", thumbprint );
		EXPECT_EQ( ServerTrust::RejectedDir("/gateway"), ServerTrust::ServersDir("/gateway")/"rejected-servers" ) << "the suite sets serversDir alone:  the rejected directory is its default";
		ASSERT_TRUE( fs::exists(trusted) ) << trusted;
		EXPECT_TRUE( UAClient::RejectedThumbprint(Slug).empty() ) << "the standing rejection was not cleared";
		EXPECT_TRUE( UAClient::RejectedThumbprint(twin).empty() ) << "the other connection that rejected it still holds a thumbprint whose file is gone";

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

		//withdrawn by hand:  rejected again, and written again - not skipped as a certificate this process has seen.
		ASSERT_TRUE( fs::remove(trusted) ) << trusted;
		Connect();
		ASSERT_TRUE( _exception ) << "connected after its certificate's trust was withdrawn";
		EXPECT_EQ( UAClient::RejectedThumbprint(Slug), thumbprint );
		EXPECT_TRUE( fs::exists(rejectedFile) ) << "the certificate rejected again was not written again";
		let again = Socket().QuerySync( "serverConnections( deleted:null ){ slug rejectedCertificate{ thumbprint } }", {} );
		let& arows = again.is_array() ? again.get_array() : Json::AsArray( again.as_object(), "serverConnections" );
		let arow = find_if( arows, [&]( let& r ){ return Json::AsSV(r.as_object(), "slug")==Slug; } );
		ASSERT_NE( arow, arows.end() ) << serialize( again );
		let rejectedAgain = arow->as_object().if_contains( "rejectedCertificate" );
		ASSERT_TRUE( rejectedAgain && rejectedAgain->is_object() ) << "the page's Trust panel would not appear:  " << serialize( *arow );
		EXPECT_EQ( Json::AsSV(rejectedAgain->as_object(), "thumbprint"), thumbprint ) << serialize( *arow );
	}

	//reviews-issue/415-clients-trust.md #9:  trustServer holds its target against the directories the verifier trusts - the test
	//seam's while one is set, not the setting behind it.  An empty override trusts nothing, so the move is refused and the file
	//stays.  No server:  the refusal comes before the file is read.
	TEST_F( TrustServerTests, TheTargetIsHeldAgainstTheOverride ){
		const string thumbprint{ "0123456789ABCDEF0123456789ABCDEF01234567" };
		let rejectedFile = ServerTrust::RejectedDir( "/gateway" )/Ƒ( "{}.der", thumbprint );
		_made = { rejectedFile, ServerTrust::ServersDir("/gateway")/Ƒ("{}.der", thumbprint) };
		fs::create_directories( rejectedFile.parent_path() );
		std::ofstream{ rejectedFile } << "never read:  the move is refused first";
		UAClient::SetRejectedThumbprint( Slug, thumbprint );
		ServerTrust::OverrideTrustedCertDirs( vector<fs::path>{} );
		absl::Cleanup restore = []{ ServerTrust::OverrideTrustedCertDirs( nullopt ); };
		try{
			Socket().QuerySync( "trustServer( opc:$opc, thumbprint:$thumbprint )", {{"opc", Slug}, {"thumbprint", thumbprint}} );
			ADD_FAILURE() << "trustServer moved a certificate into a directory the verifier does not trust";
		}
		catch( const GatewayErrorResponse& e ){
			EXPECT_NE( string{e.what()}.find("is not among"), string::npos ) << e.what();
		}
		EXPECT_TRUE( fs::exists(rejectedFile) ) << "a refused trust moved the file";
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

		Logging::Capture warnings{ ELogLevel::Warning };

		if( auto cached = UAClient::Find(Slug, Credential{}); cached )
			UAClient::RemoveClient( move(cached) );
		Attempt( Slug, Credential{} );
		ASSERT_FALSE( _exception ) << _exception->what();//trusted, so it connects - the SAN mismatch is a warning, not a refusal.
		EXPECT_TRUE( _client );

		let warned = Logging::Find( [&]( const Logging::Entry& e ){ let m = e.Message(); return e.Level==ELogLevel::Warning && m.contains("subjectAltName URI") && m.contains(advertised); } );
		EXPECT_FALSE( warned.empty() ) << "the SAN-URI mismatch was not logged at Warning";
	}
}