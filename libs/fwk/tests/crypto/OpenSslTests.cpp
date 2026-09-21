#include <jde/fwk/crypto/OpenSsl.h>
#include "../../src/crypto/OpenSslInternal.h"
#include "cryptoFixture.h"
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/settings.h>
#include <fstream>

#define let const auto
namespace Jde::Crypto{
	constexpr ELogTags _tags = ELogTags::Test;
	using namespace Crypto::Internal;

	struct OpenSslTests : public ::testing::Test{
	protected:
		OpenSslTests() {}
		~OpenSslTests() override{}

		Ω SetUpTestCase()->void;
		Ω TearDownTestCase()->void;
		α SetUp()->void override{};
		α TearDown()->void override{}

		Ω ScratchDir( sv name )->fs::path;
		Ω GetModulusExponent( fs::path publicKey )ε->tuple<vector<unsigned char>,vector<unsigned char>>;
		Ω SslSettings( str publicKeyFile, str privateKeyFile, str certificateFile, sv commonName, sv subjectAltName="URI:urn:my.server.application" )ε->CryptoSettings;

		static flat_set<string> Scratch;
		static string HeaderPayload;
		static string passcode;
		static string PublicKeyFile;
		static string PrivateKeyFile;
		static string CertificateFile;
	};
	flat_set<string> OpenSslTests::Scratch;
	string OpenSslTests::HeaderPayload{ "secret stuff" };
	string OpenSslTests::passcode{ "123456789" };
	string OpenSslTests::PublicKeyFile;
	string OpenSslTests::PrivateKeyFile;
	string OpenSslTests::CertificateFile;


	α OpenSslTests::SslSettings( str publicKeyFile, str privateKeyFile, str certificateFile, sv commonName, sv subjectAltName )ε->CryptoSettings{
		return CryptoSettings{ jobject{
			{"certificate", jobject{{"path", certificateFile}, {"subjectAltName", subjectAltName}, {"company", "jde-cpp"}, {"country", "US"}, {"commonName", commonName}}},
			{"privateKey", jobject{{"path", privateKeyFile}, {"passcode", passcode}}},
			{"publicKey", jobject{{"path", publicKeyFile}}},
			{"dh", ""}
		}, {} };
	}

	α OpenSslTests::ScratchDir( sv name )->fs::path{
		let dir = Tests::FixtureDir( "openSsl" )/name;
		fs::remove_all( dir );
		Scratch.emplace( dir.string() );
		return dir;
	}

	//EnsureKeyCertificate re-issues on the key that is there, but reissueReason only weighs existence, expiry and
	//subjectAltName - never whether the certificate on disk was issued for that key - so it heals a missing or stale
	//certificate and leaves a mismatched one standing.  With clear:false a fixture left mismatched by an older build
	//would therefore stay mismatched forever, so the check has to happen here.  (The library has the same blind spot
	//for a real damaged install - see the note on #26.)
	Ω matchesKey( str certificateFile, str publicKeyFile )ι->bool{
		try{
			auto der = Crypto::ReadCertificate( certificateFile );
			return Crypto::ExtractPublicKey( der, SRCE_CUR )==Crypto::ReadPublicKey( publicKeyFile );
		}
		catch( const Exception& ){ return false; }//missing or unreadable either way - mint a fresh pair.
	}

	α OpenSslTests::TearDownTestCase()->void{
		for( str dir : Scratch )
			EXPECT_FALSE( fs::exists(dir) ) << dir << " was left behind - a test that mints a key+cert has to remove them (#25)";
		Scratch.clear();
	}

	α OpenSslTests::SetUpTestCase()->void{
		let dir = Tests::FixtureDir( "openSsl" );
		PublicKeyFile = ( dir/"public.pem" ).string();
		PrivateKeyFile = ( dir/"private.pem" ).string();
		CertificateFile = ( dir/"cert.pem" ).string();
		let clear = Settings::FindBool( "/cryptoTests/clear" ).value_or( true );
		INFO( "clear={}", clear );
		INFO( "HeaderPayload={}", HeaderPayload );
		let settings = SslSettings( PublicKeyFile, PrivateKeyFile, CertificateFile, "openSslTests" );//the CN is the identity slug - never "localhost".
		//key and certificate are one unit.  This used to be two independent exists() checks - keys re-created when
		//either key was missing, the certificate kept whenever it existed - so a surviving cert.pem could stand against
		//a freshly minted key pair: a certificate advertising a public key the private key cannot sign for, which is
		//the exact damaged install the EnsureKeyCertificate_* tests below guard against (#26).  EnsureKeyCertificate is
		//the library's own heal-as-a-unit path: it re-issues on the key that is there, or mints both when it is not.
		if( clear || !fs::exists(PublicKeyFile) || !matchesKey(CertificateFile, PublicKeyFile) )
			Crypto::CreateKeyCertificate( settings, SRCE_CUR );//a wiped fixture, a missing public key or an inherited mismatch - all mint the pair and the cert together.
		else
			Crypto::EnsureKeyCertificate( settings, SRCE_CUR );//consistent already: this is left to re-issue an expired one or reconcile the SAN.
		INFO( "fixture at {}", dir.string() );
	}

	TEST_F( OpenSslTests, Main ){
		let signature = Crypto::RsaSign( HeaderPayload, PrivateKeyFile, passcode );
		auto publicKey = Crypto::ReadPublicKey( PublicKeyFile );

		Crypto::Verify( publicKey, HeaderPayload, signature );
	}

	//a mismatch is an expected auth outcome, not a malfunction, and EVP_VerifyFinal reports both through one return
	//value - 0 for the first, negative for the second.  The rejected client only ever sees this message, so it has to
	//name the reason rather than echo a bare openssl rc.
	TEST_F( OpenSslTests, VerifyRejectsBadSignature ){
		let signature = Crypto::RsaSign( HeaderPayload, PrivateKeyFile, passcode );
		auto publicKey = Crypto::ReadPublicKey( PublicKeyFile );
		try{
			Crypto::Verify( publicKey, HeaderPayload+"!", signature );//right key, wrong payload.
			FAIL() << "Verify should have thrown.";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("Signature verification failed."), string::npos ) << e.what();
		}
	}

	TEST_F( OpenSslTests, Certificate ){
		auto bytes = ReadCertificate( CertificateFile );
		EXPECT_TRUE( ExtractPublicKey(bytes, SRCE_CUR)==Crypto::ReadPublicKey(PublicKeyFile) );
	}

	//install-issues #33:  this repo writes PEM, but every certificate it is *given* comes from somewhere else - an OPC UA
	//server publishes its instance certificate as DER (Kepware's kepserverex_ua_server.der), and a UA trust list is a
	//directory of .der by convention.  Both encodings carry the same X.509, so both must read back to the same bytes; the
	//return value is DER either way.  Junk in either encoding still throws, naming the file.
	TEST_F( OpenSslTests, ReadCertificateTakesPemOrDer ){
		let dir = ScratchDir( "readDer" );
		IO::CreateDirectories( dir );//nothing here issues into it - the files are written by hand.
		let pem = ReadCertificate( CertificateFile );//DER bytes, from the PEM fixture.
		let derFile = dir/"cert.der";
		IO::SaveBinary<const byte>( derFile, std::span{pem} );
		EXPECT_TRUE( ReadCertificate(derFile)==pem ) << "the same certificate read back differently for being DER on disk";

		let garbage = dir/"garbage.der";
		let junk = string{ "neither a PEM header nor a DER SEQUENCE" };
		IO::SaveBinary<const char>( garbage, std::span{junk} );
		try{
			ReadCertificate( garbage );
			ADD_FAILURE() << "a file that is neither encoding parsed";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find(garbage.string()), string::npos ) << e.what();//the path is what tells the operator which file is bad.
		}
		fs::remove_all( dir );
	}

	//reviews/m2-closing.md #10:  an OpenSslException took one error off the thread's queue and left the rest.  A file that is
	//neither encoding fails two parses, and the DER one queues more than one error, so every bad file in a trust directory -
	//loadAnchors reads them one by one and carries on - left its tail behind, and the next OpenSslException on that thread,
	//whatever it was about, led with it.  Nothing may be left, and an exception raised afterwards with nothing queued has
	//to say so rather than name the certificate's fault.
	TEST_F( OpenSslTests, AFailedParseLeavesNothingOnTheErrorQueue ){
		let dir = ScratchDir( "errQueue" );
		IO::CreateDirectories( dir );
		let garbage = dir/"pkcs7-or-anything.cer";
		//SEQUENCE{ OID pkcs7-signedData } - how a PKCS#7 .cer opens, and complete, which is what matters:  d2i_X509 gets as far as
		//the member that should be the tbsCertificate SEQUENCE and queues "wrong tag" and then "nested asn1 error" twice on the
		//way back out.  A *truncated* blob queues one error ("not enough data") and would prove nothing here.
		const vector<unsigned char> junk{ 0x30, 0x0b, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x02 };
		IO::SaveBinary<const unsigned char>( garbage, std::span{junk} );
		ERR_clear_error();
		EXPECT_THROW( ReadCertificate(garbage), OpenSslException );
		EXPECT_EQ( ERR_peek_error(), 0ul ) << "left on the queue for the next exception: " << OpenSslException::CurrentError( (uint32)ERR_peek_error() );

		const OpenSslException unrelated{ "an unrelated failure, with nothing queued" };
		EXPECT_EQ( unrelated.Code(), 0u ) << unrelated.what();
		EXPECT_EQ( string{unrelated.what()}.find("asn1"), string::npos ) << unrelated.what();
		fs::remove_all( dir );
	}

	//the one filter every certificate drop-directory scan now shares - ServerTrust (the gateway's and the emulator's trusted
	//servers), UATrust (the OpcServer's trusted clients) and the enrollment anchors - so the three cannot drift apart again
	//and re-open install-issues #33 one scan at a time.  `.der`/`.cer` are the encoding a third party publishes; the rest of
	//what lives in such a directory - a CRL, a key, a README, a backup - must still stay out of the parser.
	TEST_F( OpenSslTests, IsCertificateFileTakesTheFourExtensions ){
		for( let& name : {"server.pem", "server.crt", "kepserverex_ua_server.der", "server.cer"} )
			EXPECT_TRUE( Crypto::IsCertificateFile(fs::path{name}) ) << name;
		for( let& name : {"server.crl", "private.key", "README", "README.txt", "server.pem.bak", "der"} )
			EXPECT_FALSE( Crypto::IsCertificateFile(fs::path{name}) ) << name;
	}

	//what made #26 latent: no test asserted the fixture's certificate and key pair belong together, so the split
	//regeneration - keys re-created when either key was missing, the certificate kept whenever it existed - could leave
	//a cert advertising a public key its private key cannot sign for, and every test still passed.  That is the same
	//damaged install EnsureKeyCertificate_MissingKeyReissuesCert guards for a scratch dir; this guards the fixture.
	//reviews/m2-closing.md #11 - the once-only half of the shared filter:  the first sighting of a passed-over file is the one a
	//scan says out loud, however often it reruns;  another file is another first.
	TEST_F( OpenSslTests, FirstSkipIsTrueOncePerFile ){
		let dir = ScratchDir( "firstSkip" );
		EXPECT_TRUE( FirstSkip(dir/"client.pfx") );
		EXPECT_FALSE( FirstSkip(dir/"client.pfx") );
		EXPECT_FALSE( FirstSkip(dir/"client.pfx") );
		EXPECT_TRUE( FirstSkip(dir/"SERVER.DER") ) << "the filter is case-sensitive, so this is passed over too - and is its own first";
		EXPECT_FALSE( IsCertificateFile(dir/"SERVER.DER") );
	}

	TEST_F( OpenSslTests, FixtureCertificateMatchesItsKey ){
		auto der = ReadCertificate( CertificateFile );
		auto certKey = Crypto::ExtractPublicKey( der, SRCE_CUR );
		EXPECT_TRUE( certKey==Crypto::ReadPublicKey(PublicKeyFile) ) << "the fixture certificate was issued for a different key pair than public.pem";
		//and the private half really signs for the key the certificate carries - access_users is keyed by that modulus.
		EXPECT_NO_THROW( Crypto::Verify(certKey, HeaderPayload, Crypto::RsaSign(HeaderPayload, PrivateKeyFile, passcode)) );
	}
	TEST_F( OpenSslTests, ExtractInfo ){
		let dir = ScratchDir( "extractInfo" );
		let publicKeyFile = (dir/"public.pem").string(), privateKeyFile = (dir/"private.pem").string(), certificateFile = (dir/"cert.pem").string();
		Crypto::CreateKeyCertificate( SslSettings(publicKeyFile, privateKeyFile, certificateFile, "extract-info-cn", "email:tester@jde-cpp.com,otherName:1.3.6.1.4.1.311.20.2.3;UTF8:upn-tester@jde-cpp.com,URI:urn:my.server.application") );
		let info = Crypto::Certificate{ ReadCertificate(certificateFile) };
		EXPECT_EQ( info.CommonName, "extract-info-cn" );
		EXPECT_EQ( info.Email, "tester@jde-cpp.com" );
		EXPECT_EQ( info.Upn, "upn-tester@jde-cpp.com" );
		EXPECT_EQ( info.DistinguishedName, "CN=extract-info-cn,O=jde-cpp,C=US" );
		EXPECT_EQ( info.Issuer, info.DistinguishedName );//self-signed.
		//the SAN must come back in the openssl config syntax it was issued with, so a parsed cert can be re-issued.
		EXPECT_EQ( info.SubjectAltName, "email:tester@jde-cpp.com,otherName:msUPN;UTF8:upn-tester@jde-cpp.com,URI:urn:my.server.application" );
		EXPECT_EQ( info.SanUri(), "urn:my.server.application" );//not the whole SAN - that was the applicationUri bug.
		EXPECT_GT( info.Expiration, Clock::now() );//CreateCertificate issues 365-day certs.
		let plain = Crypto::Certificate{ ReadCertificate(CertificateFile) };//fixture cert has a URI-only SAN.
		EXPECT_TRUE( plain.Email.empty() );
		EXPECT_TRUE( plain.Upn.empty() );
		EXPECT_EQ( plain.SubjectAltName, "URI:"+plain.SanUri() );//single entry - SanUri is the whole thing bar the prefix.
		fs::remove_all( dir );
	}

	// access_users is keyed by (modulus, exponent) and Jwt caches by fingerprint, so a byte-order slip in
	// ExponentInt or a spelling/width change in ModulusHex re-enrolls every existing identity as a stranger.
	// None of these helpers had a caller in any test.
	TEST_F( OpenSslTests, PublicKeyIdentity ){
		auto key = Crypto::ReadPublicKey( PublicKeyFile );
		EXPECT_EQ( key.Modulus.size(), 256u ) << "CreateKey issues 2048-bit keys";
		EXPECT_EQ( key.ExponentInt(), 65537u ) << "the exponent bytes fold big-endian";
		EXPECT_EQ( key.ModulusHex(), Str::ToHex(key.Modulus) );
		EXPECT_EQ( key.ModulusHex().size(), 512u ) << "two hex chars per byte - the enrollment column's width";
		EXPECT_EQ( key.Hash32(), key.Hash32() ) << "OpcServerSession displays this per session";
		//deliberately a mirror of the implementation: what matters is that the displayed id stays the big-endian
		//fold of md5(modulus) across versions, not any particular value, which depends on the generated key.
		uint32_t folded{};
		for( let b : Crypto::CalcMd5(key.Modulus) )
			folded = ( folded<<8 ) | (uint32_t)b;
		EXPECT_EQ( key.Hash32(), folded );

		//through PublicKeyPath::Value rather than PublicKey{path} directly: the ctor and the ToBytes member are
		//declared but not exported (only the Φ members are), so Value is how an out-of-dll caller reaches them.
		auto settings = SslSettings( PublicKeyFile, PrivateKeyFile, CertificateFile, "openSslTests" );
		EXPECT_TRUE( settings.PublicKey.Value(SRCE_CUR)==key ) << "the configured path must resolve to the key ReadPublicKey returns";
		EXPECT_EQ( &settings.PublicKey.Value(SRCE_CUR), &settings.PublicKey.Value(SRCE_CUR) ) << "Value caches rather than re-reading";
		let bytes = Crypto::ToBytes( key );//modulus+exponent -> DER, which is what Fingerprint hashes.
		EXPECT_EQ( Crypto::Fingerprint(key), Crypto::CalcMd5(bytes) );

		//a second key must collide with the first on none of them.
		let dir = ScratchDir( "identity" );
		let publicKey2 = (dir/"public.pem").string(), privateKey2 = (dir/"private.pem").string();
		Crypto::CreateKey( SslSettings(publicKey2, privateKey2, CertificateFile, "identity-cn"), SRCE_CUR );
		auto other = Crypto::ReadPublicKey( publicKey2 );
		EXPECT_EQ( other.ExponentInt(), key.ExponentInt() ) << "same e - the modulus is what separates two identities";
		EXPECT_NE( other.ModulusHex(), key.ModulusHex() );
		EXPECT_NE( other.Hash32(), key.Hash32() );
		EXPECT_NE( Crypto::Fingerprint(other), Crypto::Fingerprint(key) );
		EXPECT_FALSE( other==key );
		fs::remove_all( dir );
	}

	//fwk-max #12 made Value() Ε: a missing public key has to throw rather than hand back a default-constructed
	//key, which would compare equal to every other empty one.
	TEST_F( OpenSslTests, PublicKeyPathValueThrowsWhenMissing ){
		let missing = (fs::path{PublicKeyFile}.parent_path()/"no-such-public.pem").string();
		ASSERT_FALSE( fs::exists(missing) );
		auto settings = SslSettings( missing, PrivateKeyFile, CertificateFile, "missing-public-key" );
		EXPECT_THROW( settings.PublicKey.Value(SRCE_CUR), IO::IOException );
	}

	//the CN is the enrollment identity (access_identities.slug, a unique natural key) so it must stay per-host, but
	//it is also the file stem - a hostname change would move the key pair, mint a new one and strand the old identity.
	TEST_F( OpenSslTests, FileStemDecouplesPathsFromCommonName ){
		let sslDir = Process::ProgramDataFolder()/Process::CompanyRootDir()/Process::ProductName()/"ssl";
		let names = []( sv fileName, sv commonName ){
			return jobject{ {"certificate", jobject{{"fileName", string{fileName}}, {"commonName", string{commonName}}}} };
		};
		let hostA = CryptoSettings{ names("gateway.web", "gateway.web.hostA") };
		let hostB = CryptoSettings{ names("gateway.web", "gateway.web.hostB") };

		EXPECT_EQ( hostA.PrivateKey.Path, hostB.PrivateKey.Path );//the key survives a rename - same modulus, same user.
		EXPECT_EQ( hostA.PublicKey.Path, hostB.PublicKey.Path );
		EXPECT_EQ( hostA.Certificate.Path, hostB.Certificate.Path );
		EXPECT_EQ( hostA.Certificate.Path, sslDir/"certs"/"gateway.web.pem" );
		EXPECT_NE( hostA.Certificate.CommonName, hostB.Certificate.CommonName );//identity still per-host.

		//no fileName - the CN remains the stem, so every other config is unaffected.
		let legacy = CryptoSettings{ jobject{ {"certificate", jobject{{"commonName", "legacy-cn"}}} } };
		EXPECT_EQ( legacy.Certificate.Path, sslDir/"certs"/"legacy-cn.pem" );
		EXPECT_EQ( legacy.PrivateKey.Path, sslDir/"private"/"legacy-cn.pem" );
	}
	//`productName` at the ssl level names the per-product tree for every path derived from the block.  It used to be
	//honored only by `dh`: sibling servers with different ssl-level productNames shared one cert file derived from
	//Process::ProductName(), while dh alone pointed at the configured tree.  A sub-object's own productName still
	//outranks the block's - the soak's issuedCerts block sets all three explicitly and must keep resolving as written.
	TEST_F( OpenSslTests, SslLevelProductNameNamesTheTree ){
		let companyDir = Process::ProgramDataFolder()/Process::CompanyRootDir();
		let a = CryptoSettings{ jobject{ {"productName", "ProductA"}, {"certificate", jobject{{"commonName", "product-tree-cn"}}} } };
		EXPECT_EQ( a.Certificate.Path, companyDir/"ProductA"/"ssl"/"certs"/"product-tree-cn.pem" );
		EXPECT_EQ( a.PrivateKey.Path, companyDir/"ProductA"/"ssl"/"private"/"product-tree-cn.pem" );
		EXPECT_EQ( a.PublicKey.Path, companyDir/"ProductA"/"ssl"/"public"/"product-tree-cn.pem" );
		EXPECT_EQ( a.DhPath, companyDir/"ProductA"/"ssl"/"dh.pem" );//dh always honored the ssl level - now consistent with its siblings.

		let overridden = CryptoSettings{ jobject{ {"productName", "ProductA"}, {"certificate", jobject{{"commonName", "product-tree-cn"}, {"productName", "ProductB"}}} } };
		EXPECT_EQ( overridden.Certificate.Path, companyDir/"ProductB"/"ssl"/"certs"/"product-tree-cn.pem" );//own wins.
		EXPECT_EQ( overridden.PrivateKey.Path, companyDir/"ProductA"/"ssl"/"private"/"product-tree-cn.pem" );//only the certificate overrode.

		let plain = CryptoSettings{ jobject{ {"certificate", jobject{{"commonName", "product-tree-cn"}}} } };
		EXPECT_EQ( plain.Certificate.Path, companyDir/Process::ProductName()/"ssl"/"certs"/"product-tree-cn.pem" );//no productName anywhere - the process default, unchanged.
	}

	//an unconfigured subjectAltName defaults to the localhost pair - a generated server cert without one is guaranteed
	//to fail host_name_verification, and the hand-spelled per-config line kept getting forgotten (finding: a fresh
	//deployment could never establish trust) or misspelled.  Explicit "" still opts out, any configured value wins.
	TEST_F( OpenSslTests, SubjectAltNameDefaultsToLocalhost ){
		let defaulted = CryptoSettings{ jobject{ {"certificate", jobject{{"commonName", "san-default-cn"}}} } };
		EXPECT_EQ( defaulted.Certificate.SubjectAltName, "DNS:localhost,IP:127.0.0.1" );
		let optedOut = CryptoSettings{ jobject{ {"certificate", jobject{{"commonName", "san-default-cn"}, {"subjectAltName", ""}}} } };
		EXPECT_TRUE( optedOut.Certificate.SubjectAltName.empty() );
		let overridden = CryptoSettings{ jobject{ {"certificate", jobject{{"commonName", "san-default-cn"}, {"subjectAltName", "URI:urn:x"}}} } };
		EXPECT_EQ( overridden.Certificate.SubjectAltName, "URI:urn:x" );
	}

	//certInstance is the OPC Slug, settable through the createServerConnection mutation, and the CN is the stem of
	//all three files - neither may escape the ssl tree, or CreateDirectories/IssueCertificate write a PEM anywhere the
	//service account can reach.
	TEST_F( OpenSslTests, PathComponentsCannotEscapeTheSslTree ){
		let sslDir = Process::ProgramDataFolder()/Process::CompanyRootDir()/Process::ProductName()/"ssl";
		let withCn = []( sv commonName ){ return jobject{ {"certificate", jobject{{"commonName", string{commonName}}}} }; };

		let benign = CryptoSettings{ withCn("escape-test"), "TestServer" };//the normal shape is unchanged.
		EXPECT_EQ( benign.Certificate.Path, sslDir/"certs"/"escape-test.TestServer.pem" );

		let evilTarget = CryptoSettings{ withCn("escape-test"), "../../../../etc/cron.d/x" };
		EXPECT_EQ( evilTarget.Certificate.Path.parent_path(), sslDir/"certs" );

		let evilCn = CryptoSettings{ withCn("../../../../etc/cron.d/y") };
		EXPECT_EQ( evilCn.Certificate.Path.parent_path(), sslDir/"certs" );
		EXPECT_EQ( evilCn.PrivateKey.Path.parent_path(), sslDir/"private" );
		EXPECT_EQ( evilCn.PublicKey.Path.parent_path(), sslDir/"public" );
		EXPECT_EQ( evilCn.Certificate.CommonName, "../../../../etc/cron.d/y" );//the CN itself stays intact - it is the X.509 subject and users.slug.
	}
	//key+cert are a unit: losing the key must re-issue the certificate, never leave the old one standing against a new
	//key.  A surviving cert would advertise a public key the new private key cannot sign for - and since access_users
	//is keyed by modulus, the process would also enroll as a second identity.
	TEST_F( OpenSslTests, EnsureKeyCertificate_MissingKeyReissuesCert ){
		let dir = ScratchDir( "ensureKeyCert" );
		let settings = SslSettings( (dir/"public.pem").string(), (dir/"private.pem").string(), (dir/"cert.pem").string(), "ensure-key-cert" );
		settings.CreateDirectories();
		Crypto::CreateKeyCertificate( settings );
		auto originalDer = ReadCertificate( settings.Certificate.Path );//lvalue - ExtractPublicKey takes a mutable span.
		let original = Crypto::ExtractPublicKey( originalDer, SRCE_CUR );

		fs::remove( settings.PrivateKey.Path );//damaged install: the key is gone but the certificate survives.
		Crypto::EnsureKeyCertificate( settings );

		auto reissuedDer = ReadCertificate( settings.Certificate.Path );
		let reissued = Crypto::ExtractPublicKey( reissuedDer, SRCE_CUR );
		EXPECT_FALSE( reissued==original );//the stale cert was replaced, not kept.
		EXPECT_TRUE( reissued==Crypto::ReadPublicKey(settings.PublicKey.Path) );//and it matches the key actually on disk.
		fs::remove_all( dir );
	}
	//the other half of MissingKeyReissuesCert, and the only reissueReason branch with no coverage: losing just the
	//certificate must re-issue it on the key already on disk.  Minting a fresh pair here would strand the enrollment
	//identity - access_users is keyed by the modulus, so the process would come back as a stranger to a db still
	//holding the old key, and every peer that anchored the old cert would have to re-trust it.
	TEST_F( OpenSslTests, EnsureKeyCertificate_MissingCertReissuesOnSameKey ){
		let dir = ScratchDir( "missingCert" );
		let settings = SslSettings( (dir/"public.pem").string(), (dir/"private.pem").string(), (dir/"cert.pem").string(), "missing-cert" );
		settings.CreateDirectories();
		Crypto::CreateKeyCertificate( settings );
		let originalKey = Crypto::ReadPublicKey( settings.PublicKey.Path );
		let originalDer = ReadCertificate( settings.Certificate.Path );

		fs::remove( settings.Certificate.Path );//damaged install: the certificate is gone, the key pair survives.
		Crypto::EnsureKeyCertificate( settings );

		ASSERT_TRUE( fs::exists(settings.Certificate.Path) );
		auto reissuedDer = ReadCertificate( settings.Certificate.Path );//lvalue - ExtractPublicKey takes a mutable span.
		EXPECT_FALSE( reissuedDer==originalDer );//genuinely re-issued: IssueCertificate draws a random 128-bit serial.
		EXPECT_TRUE( Crypto::ExtractPublicKey(reissuedDer, SRCE_CUR)==originalKey );//...but on the key that was already there.
		EXPECT_TRUE( Crypto::ReadPublicKey(settings.PublicKey.Path)==originalKey );//and the pair itself was left alone.
		fs::remove_all( dir );
	}
	//an unreadable certificate is a damaged install, not something to paper over by minting a replacement.
	TEST_F( OpenSslTests, EnsureKeyCertificate_BadCertThrows ){
		let dir = ScratchDir( "badCert" );
		let settings = SslSettings( (dir/"public.pem").string(), (dir/"private.pem").string(), (dir/"cert.pem").string(), "bad-cert" );
		settings.CreateDirectories();
		Crypto::CreateKeyCertificate( settings );
		constexpr sv garbage{ "-----BEGIN CERTIFICATE-----\ngarbage\n" };
		{ std::ofstream damaged{ settings.Certificate.Path, std::ios::trunc|std::ios::binary }; damaged << garbage; }//binary, so the read-back below compares the bytes that were written.

		EXPECT_THROW( Crypto::EnsureKeyCertificate(settings), OpenSslException );
		EXPECT_TRUE( fs::exists(settings.PrivateKey.Path) );//the key is left alone, so deleting the cert re-issues on the same modulus.
		EXPECT_EQ( IO::Load(settings.Certificate.Path), garbage ) << "the unreadable certificate was rewritten instead of being left for an operator to look at";
		fs::remove_all( dir );
	}
	//a cert issued before its config gained a SAN (or with a different one) must heal on startup - the CI runner kept a
	//SAN-less web cert failing host_name_verification until its config dir was hand-wiped.  The key pair must survive
	//the re-issue, and an equivalent config must leave the cert untouched - including conf-syntax slack and the msUPN
	//OID→short-name round-trip, either of which would otherwise re-issue on every start.
	//the expiry branch of ReissueReason, reachable only through IssueCertificate's validity seam - production can't mint an
	//expired certificate.  A day's margin: "expires tomorrow" re-issues as "expired" does; a fresh one stands.
	TEST_F( OpenSslTests, EnsureKeyCertificate_ReissuesExpired ){
		let dir = ScratchDir( "reissueExpired" );
		let publicFile = (dir/"public.pem").string(), privateFile = (dir/"private.pem").string(), certFile = (dir/"cert.pem").string();
		let settings = SslSettings( publicFile, privateFile, certFile, "reissue-expired", "DNS:localhost" );
		settings.CreateDirectories();
		Crypto::CreateKey( settings, SRCE_CUR );
		let key = Crypto::ReadPublicKey( publicFile );
		let expiration = [&]{ return Crypto::Certificate{ ReadCertificate(certFile) }.Expiration; };

		Crypto::IssueCertificate( settings, std::chrono::hours{-24} );//already expired
		ASSERT_LT( expiration(), Clock::now() );
		EXPECT_EQ( Crypto::ReissueReason(settings).substr(0, 7), "expired" );
		Crypto::EnsureKeyCertificate( settings );
		EXPECT_GT( expiration(), Clock::now()+std::chrono::days{364} );
		auto healedDer = ReadCertificate( certFile );
		EXPECT_TRUE( Crypto::ExtractPublicKey(healedDer, SRCE_CUR)==key );//same key pair - enrollment by modulus survives.

		Crypto::IssueCertificate( settings, std::chrono::hours{1} );//inside the day's margin
		EXPECT_EQ( Crypto::ReissueReason(settings).substr(0, 7), "expires" );
		Crypto::EnsureKeyCertificate( settings );
		EXPECT_GT( expiration(), Clock::now()+std::chrono::days{364} );

		let fresh = ReadCertificate( certFile );
		EXPECT_TRUE( Crypto::ReissueReason(settings).empty() );
		Crypto::EnsureKeyCertificate( settings );
		EXPECT_TRUE( ReadCertificate(certFile)==fresh );//a year out - no churn.
		fs::remove_all( dir );
	}

	TEST_F( OpenSslTests, EnsureKeyCertificate_ReconcilesSan ){
		let dir = ScratchDir( "reconcileSan" );
		let publicFile = (dir/"public.pem").string(), privateFile = (dir/"private.pem").string(), certFile = (dir/"cert.pem").string();
		let sanless = SslSettings( publicFile, privateFile, certFile, "reconcile-san", "" );
		sanless.CreateDirectories();
		Crypto::CreateKeyCertificate( sanless );
		EXPECT_TRUE( Crypto::Certificate{ ReadCertificate(certFile) }.SubjectAltName.empty() );

		let configured = SslSettings( publicFile, privateFile, certFile, "reconcile-san", "DNS:localhost,IP:127.0.0.1,otherName:1.3.6.1.4.1.311.20.2.3;UTF8:upn@jde-cpp.com" );
		Crypto::EnsureKeyCertificate( configured );//stale environment: the config gained a SAN after the cert was issued.
		auto healedDer = ReadCertificate( certFile );
		EXPECT_EQ( Crypto::Certificate{ healedDer }.SubjectAltName, "DNS:localhost,IP:127.0.0.1,otherName:msUPN;UTF8:upn@jde-cpp.com" );
		EXPECT_TRUE( Crypto::ExtractPublicKey(healedDer, SRCE_CUR)==Crypto::ReadPublicKey(publicFile) );//re-issued on the same key - enrollment by modulus survives.

		let slack = SslSettings( publicFile, privateFile, certFile, "reconcile-san", " DNS:localhost , IP:127.0.0.1 ,otherName:1.3.6.1.4.1.311.20.2.3;UTF8:upn@jde-cpp.com" );
		Crypto::EnsureKeyCertificate( slack );
		EXPECT_TRUE( ReadCertificate(certFile)==healedDer );//equivalent config - the cert stands, no re-issue loop.
		fs::remove_all( dir );
	}
	//certificate.managed:false - the operator's pair, a CA-issued one:  EnsureKeyCertificate uses it as found (no SAN reconciliation,
	//no re-issue), writes the public key file the app server's identity reads when the pair came without one, and refuses to
	//start on a missing file rather than mint a self-signed replacement for a certificate somebody vouched for.
	TEST_F( OpenSslTests, EnsureKeyCertificate_UnmanagedIsUsedAsFound ){
		let dir = ScratchDir( "unmanaged" );
		let publicFile = (dir/"public.pem").string(), privateFile = (dir/"private.pem").string(), certFile = (dir/"cert.pem").string();
		let issued = SslSettings( publicFile, privateFile, certFile, "unmanaged-cn", "DNS:issued" );
		issued.CreateDirectories();
		Crypto::CreateKeyCertificate( issued );//stands in for the operator's pair
		auto originalDer = ReadCertificate( certFile );
		fs::remove( publicFile );//a supplied pair comes without one
		auto unmanaged = [&]( sv san )->CryptoSettings{
			return CryptoSettings{ jobject{
				{"certificate", jobject{{"path", certFile}, {"managed", false}, {"subjectAltName", san}, {"commonName", "unmanaged-cn"}}},
				{"privateKey", jobject{{"path", privateFile}, {"passcode", issued.PrivateKey.Passcode}}},
				{"publicKey", jobject{{"path", publicFile}}},
				{"dh", ""}
			}, {} };
		};
		let drifted = unmanaged( "DNS:configured-elsewhere" );
		EXPECT_FALSE( drifted.Certificate.Managed );
		EXPECT_FALSE( Crypto::ReissueReason(drifted).empty() );//the SAN differs - a managed certificate would be re-issued here
		Crypto::EnsureKeyCertificate( drifted );
		EXPECT_TRUE( ReadCertificate(certFile)==originalDer );//used as found
		EXPECT_TRUE( Crypto::ReadPublicKey(publicFile)==Crypto::ExtractPublicKey(originalDer, SRCE_CUR) );//the public key file, derived from the private key
		fs::remove( certFile );
		EXPECT_THROW( Crypto::EnsureKeyCertificate(drifted), Exception );//nothing minted in its place
		fs::remove_all( dir );
	}

	TEST_F( OpenSslTests, PrivateKey ){
		Crypto::ReadPrivateKey( PrivateKeySettings{PrivateKeyFile, passcode} );
		//the key was created with a passcode - it must be encrypted at rest, i.e. unreadable without it.
		EXPECT_THROW( Crypto::ReadPrivateKey(PrivateKeySettings{PrivateKeyFile, string{}}), Exception );
	}
	TEST_F( OpenSslTests, Random ){
		array<unsigned char,16> a{}, b{};
		Crypto::Random( a.data(), a.size() );
		Crypto::Random( b.data(), b.size() );
		EXPECT_NE( a, b );//2⁻¹²⁸ false-failure odds.
		EXPECT_NE( a, (array<unsigned char,16>{}) );
		Crypto::Random<uint32_t>();
	}
}