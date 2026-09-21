#include <jde/fwk/io/json.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include "Auth.h"
#include "../../src/auth/CertAwait.h"
#include "../../src/auth/OpcServerSession.h"
#include "../../src/GatewayAppClient.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	constexpr ELogTags _tags{ ELogTags::Test };

	//no connection needed - EnsureCertificate is a static that only touches the cert tree.
	struct CertFileTests : ::testing::Test{
		static constexpr sv Slug{ "certUriChangeTest" };
		α TearDown()ι->void override{
			std::error_code ec;
			fs::remove( UAClient::CryptoSettings(ServerCnnctnNK{Slug}).Certificate.Path, ec );
		}
	};
	//the cert file name keys on the slug, its SAN on the gateway's applicationUri (/gateway/issuedCerts - security-matrix #8;
	//the second argument here stands in for an edited config) - a changed uri must re-issue, or the gateway presents a stale
	//SAN forever and every session is refused BadCertificateUriInvalid.  It is also how the certificates issued before #8,
	//their SAN the server's uri, replace themselves.
	TEST_F( CertFileTests, ReissuesWhenApplicationUriChanges ){
		let path = UAClient::CryptoSettings( ServerCnnctnNK{Slug} ).Certificate.Path;
		let sanUri = [&]{ return Crypto::Certificate{ Crypto::ReadCertificate(path) }.SanUri(); };

		UAClient::EnsureCertificate( ServerCnnctnNK{Slug}, "urn:first.application" );
		ASSERT_TRUE( fs::exists(path) );
		EXPECT_EQ( sanUri(), "urn:first.application" );

		UAClient::EnsureCertificate( ServerCnnctnNK{Slug}, "urn:second.application" );//same file name, different uri.
		EXPECT_EQ( sanUri(), "urn:second.application" );

		//and it must NOT churn when nothing changed - re-issuing every connect would rotate a cert peers have trusted.
		let before = Crypto::ReadCertificate( path );
		UAClient::EnsureCertificate( ServerCnnctnNK{Slug}, "urn:second.application" );
		EXPECT_EQ( Crypto::ReadCertificate(path), before );
	}

	//web-certs3 #17: an issued OPC client certificate used to be presented until the peer rejected it as expired, with
	//deleting the PEM the only remedy.  The per-slug path now shares ReissueReason with the web certificates.
	TEST_F( CertFileTests, ReissuesWhenExpired ){
		constexpr sv slug{ "certExpiryTest" };
		constexpr sv uri{ "urn:expiry.application" };
		let settings = UAClient::CryptoSettings( ServerCnnctnNK{slug}, uri );
		let expiration = [&]{ return Crypto::Certificate{ Crypto::ReadCertificate(settings.Certificate.Path) }.Expiration; };

		UAClient::EnsureCertificate( ServerCnnctnNK{slug}, uri );
		Crypto::IssueCertificate( settings, std::chrono::hours{-24} );//what a year of uptime leaves on disk.
		ASSERT_LT( expiration(), Clock::now() );

		UAClient::EnsureCertificate( ServerCnnctnNK{slug}, uri );
		EXPECT_GT( expiration(), Clock::now()+std::chrono::days{364} );
		EXPECT_EQ( Crypto::Certificate{ Crypto::ReadCertificate(settings.Certificate.Path) }.SanUri(), uri );//re-issued for the same slug.

		std::error_code ec;
		fs::remove( settings.Certificate.Path, ec );//the key is per CN and shared with the rest of the suite - leave it.
	}

	//web-certs3 #9: the passcode has to travel from /gateway/issuedCerts/privateKey/passcode into the key file.  It once sat a
	//level up and jsonnet-hidden, so every issued OPC client key silently went to disk in cleartext while the config read as
	//encrypted - this pins the plumbing, not openssl (OpenSslTests already proves CreateKey encrypts when asked).
	TEST_F( CertFileTests, PasscodeEncryptsTheIssuedKey ){
		constexpr sv slug{ "passcodeTest" };
		let saved = Settings::FindDefaultObject( "/gateway/issuedCerts" );
		Settings::Set( "/gateway/issuedCerts/privateKey/passcode", "test-passcode" );
		Settings::Set( "/gateway/issuedCerts/certificate/commonName", "passcodeTest" );//its own key pair:  the key file is per CN and shared by every slug, and the rest of the suite opens it without a passcode.
		let settings = UAClient::CryptoSettings( ServerCnnctnNK{slug} );
		try{
			UAClient::EnsureCertificate( ServerCnnctnNK{slug}, "urn:passcode.test" );
		}
		catch( ... ){
			Settings::Set( "/gateway/issuedCerts", saved );
			throw;
		}
		Settings::Set( "/gateway/issuedCerts", saved );

		EXPECT_NE( IO::Load(settings.PrivateKey.Path).find("ENCRYPTED"), string::npos ) << settings.PrivateKey.Path.string();
		EXPECT_NO_THROW( Crypto::ReadPrivateKey(settings.PrivateKey) );//opens with the configured passcode - what UAClient::Configuration does.
		EXPECT_THROW( Crypto::ReadPrivateKey(Crypto::PrivateKeySettings{settings.PrivateKey.Path, ""}), std::exception );//and not without it.

		std::error_code ec;
		for( let& path : {settings.Certificate.Path, settings.PrivateKey.Path, settings.PublicKey.Path} )
			fs::remove( path, ec );
	}

	//reviews/m2-closing.md #7: certificate.managed:false - the operator's own pair, one a CA issued - was parsed on
	///gateway/issuedCerts and then ignored:  this path took EnsureKeyCertificate's re-issue predicate without the guard in
	//front of it, so a pair whose SAN was not the block's was overwritten in place with a self-signed one.  A changed uri is
	//the drift that re-issues a managed certificate (the first test);  it must leave this one exactly as found.  And a pair
	//that is not there is the operator's to supply - said with the file's name, since it carries the slug.
	TEST_F( CertFileTests, AnUnmanagedPairIsLeftAsFound ){
		constexpr sv missing{ "unmanagedMissingTest" };
		let path = UAClient::CryptoSettings( ServerCnnctnNK{Slug} ).Certificate.Path;
		UAClient::EnsureCertificate( ServerCnnctnNK{Slug}, "urn:operator.supplied" );//stands in for the pair the operator brought.
		let before = Crypto::ReadCertificate( path );

		let saved = Settings::FindDefaultObject( "/gateway/issuedCerts" );
		struct Restore final{ const jobject& Saved; ~Restore(){ try{ Settings::Set("/gateway/issuedCerts", Saved); }catch( const std::exception& ){} } } restore{ saved };//every later client in the process reads it.
		Settings::Set( "/gateway/issuedCerts/certificate/managed", false );
		ASSERT_FALSE( UAClient::CryptoSettings(ServerCnnctnNK{Slug}).Certificate.Managed );

		EXPECT_NO_THROW( UAClient::EnsureCertificate(ServerCnnctnNK{Slug}, "urn:what.the.config.says") );
		EXPECT_EQ( Crypto::ReadCertificate(path), before ) << "the operator's certificate was replaced";
		EXPECT_EQ( Crypto::Certificate{Crypto::ReadCertificate(path)}.SanUri(), "urn:operator.supplied" );

		let absent = UAClient::CryptoSettings( ServerCnnctnNK{missing} ).Certificate.Path;
		std::error_code ec;
		fs::remove( absent, ec );//what a run without the guard leaves behind - in the directory the suite trusts.
		struct Remove final{ const fs::path& Path; ~Remove(){ std::error_code ec; fs::remove( Path, ec ); } } remove{ absent };
		try{
			UAClient::EnsureCertificate( ServerCnnctnNK{missing}, "urn:what.the.config.says" );
			ADD_FAILURE() << "no pair for the connection, and nothing said so";
		}
		catch( const std::exception& e ){
			EXPECT_TRUE( string{e.what()}.contains(absent.string()) ) << e.what();//the file to supply, by name.
		}
		EXPECT_FALSE( fs::exists(absent) ) << "a certificate was issued for a block that says not to";
	}

	//server-side counterpart to ReissuesWhenApplicationUriChanges: the OpcServer must trust a transport cert re-issued
	//AFTER its startup snapshot (UATrust rescans on a failed verify) - pre-fix every secured connect fails
	//BadCertificateUntrusted until the server restarts. IssuedToken auth, not Certificate: certAuth swaps the transport
	//cert to AppClient()->SslSettings (Configuration()), while every other credential presents the per-slug issued
	//file - the production scenario. (Not Username - the test server doesn't offer that policy.)
	class TrustReloadTests : public Auth{
	protected:
		TrustReloadTests()ι:Auth{ETokenType::IssuedToken}{}
		Ω SetUpTestCase()ε->void{ _jwt = BlockAwait<Web::Client::ClientSocketAwait<Jde::Web::Jwt>,Web::Jwt>( AppClient()->Jwt() ); }
		α TearDown()ι->void override{
			if( _client )
				UAClient::RemoveClient( move(_client) );
		}
		α Connect( atomic_flag& flag )ι->ConnectAwait::Task;
		static optional<Web::Jwt> _jwt;
		up<Exception> _exception;
		sp<UAClient> _client;
	};
	optional<Web::Jwt> TrustReloadTests::_jwt;

	α TrustReloadTests::Connect( atomic_flag& flag )ι->ConnectAwait::Task{
		try{
			_exception = nullptr;
			_client = co_await UAClient::GetClient( Connection->Slug, Credential{_jwt->Payload()} );//same credential as TokenTests.Authenticate.
		}
		catch( Exception& e ){
			_exception = e.Move();
		}
		flag.test_and_set();
		flag.notify_all();
	}

	TEST_F( TrustReloadTests, ServerReloadsReissuedCert ){
		atomic_flag first;
		Connect( first );
		first.wait( false );
		ASSERT_FALSE( _exception ) << _exception->what();//startup snapshot trusts the pre-created cert (tests/main.cpp).
		ASSERT_TRUE( _client );
		UAClient::RemoveClient( move(_client) );//the next connect builds a fresh UAClient => full OPN handshake.

		//in-place re-issue: same SAN+key, new serial/validity => a DER the server's snapshot has never seen.  The settings are
		//the ones Configuration() issues with - the config block's SAN, the gateway's own applicationUri (security-matrix #8) -
		//so ReissueReason sees no drift at the next connect and the DER presented is the one written here.
		ASSERT_FALSE( Connection->CertificateUri.empty() );//a secured connection:  the issued certificate goes on the channel.
		Crypto::IssueCertificate( UAClient::CryptoSettings(Connection->Slug) );

		atomic_flag second;
		Connect( second );//no server restart - the verify shim must rescan trustedCertDirs and trust the new file.
		second.wait( false );
		EXPECT_FALSE( _exception ) << _exception->what();
		EXPECT_TRUE( _client );
	}

	class CertTests : public Auth{
	protected:
		CertTests()ι:Auth{ETokenType::Certificate}{}
		~CertTests()override{}
		Ω SetUpTestCase()ε->void;
		α TearDown()ι->void override{
			if( _client ){
				UAClient::RemoveClient( move(_client) );
				_client = nullptr;
			}
		}
		Ω TearDownTestSuite();

		α Connect( atomic_flag& flag, char id )ι->ConnectAwait::Task;
		optional<Credential> _cred;
		up<Exception> _exception;
		sp<UAClient> _client;
	};

	α CertTests::SetUpTestCase()ε->void{

	}
	α CertTests::TearDownTestSuite(){
		Auth::TearDownTestSuite();
	}

	α CertTests::Connect( atomic_flag& flag, char id )ι->ConnectAwait::Task{
		try{
			TRACE( "Call {}", id );
			_client = co_await UAClient::GetClient( Connection->Slug, Credential{Crypto::PublicKey{}} );
			ASSERT( _client );
			//co_await CertAwait{ Client->Slug, "localhost", true };
			TRACE( "{} returned", id );
		}
		catch( Exception& e ){
			TRACE( "{} failed", id );
			_exception = e.Move();
		}
		flag.test_and_set();
		flag.notify_all();
	}

	TEST_F( CertTests, Authenticate ){
		string opcId{ Connection->Slug };
		atomic_flag a,b,c,d;
		Connect( a, 'a' );//test Connection.
		Connect( b, 'b' );//test waiting for a.
		a.wait( false );
		b.wait( false );
		Connect( c, 'c' );//test already have connection.
		Connect( d, 'd' );
		c.wait( false );
		d.wait( false );
		EXPECT_FALSE( _exception );
		//security-matrix #8:  certificate authentication presents the app client's own certificate, so the name the gateway gives
		//is that certificate's uri - the gateway's, not the server's, which stays the endpoint filter.
		ASSERT_TRUE( _client );
		let own = Crypto::Certificate{ Crypto::ReadCertificate(AppClient()->SslSettings->Certificate.Path) }.SanUri();
		EXPECT_FALSE( own.empty() );
		EXPECT_EQ( _client->AdvertisedUri(), own );
		EXPECT_EQ( _client->ApplicationUri(), Connection->CertificateUri );
		EXPECT_NE( _client->AdvertisedUri(), _client->ApplicationUri() );
	}

	TEST_F( CertTests, Authenticate_Bad ){
		//the bad cert must live OUTSIDE trustedCertDirs: the server rescans them on a failed verify (UATrust), so a
		//cert dropped into an anchored dir is trusted by design - the old ssl-dir swap put it exactly there.
		let root = Process::AppDataFolder();
		auto& ssl = *AppClient()->SslSettings;
		auto bad = ssl;
		bad.Certificate.Path = root/"ssl_badTest"/"certs"/bad.Certificate.Path.filename();
		bad.PrivateKey.Path = root/"ssl_badTest"/"private"/bad.PrivateKey.Path.filename();
		bad.PublicKey.Path = root/"ssl_badTest"/"public"/bad.PublicKey.Path.filename();
		Crypto::EnsureKeyCertificate( bad );//no-op when a previous run left the tree behind.
		let badCertificate = bad.Certificate.Path.string();
		struct Restore final{ //the real settings have to come back even when the body throws - otherwise every later test connects with the bad cert.
			Crypto::CryptoSettings Good; Crypto::CryptoSettings& Live;
			~Restore(){ Live = move(Good); }
		} restore{ ssl, ssl };
		ssl = move( bad );

		atomic_flag flag;
		Connect( flag, 'a' );
		flag.wait( false );

		EXPECT_TRUE( _exception );
		EXPECT_TRUE( _exception && string{_exception->what()}.contains("BadSecurityChecksFailed") );
		EXPECT_TRUE( _exception && string{_exception->what()}.contains(badCertificate) ) << "the refusal names the certificate that was presented (security-matrix #12)";
		EXPECT_FALSE( _client );
		DBG( "{}", _exception ? _exception->what() : "Error no exception." );
	}
}