#include <jde/fwk/crypto/TrustStore.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>
#include <jde/fwk/crypto/Certificate.h>
#include <jde/fwk/crypto/OpenSslException.h>
#include <jde/fwk/crypto/issue.h>
#include "cryptoFixture.h"

#define let const auto
namespace Jde::Crypto{
	using X509Ptr = up<X509, decltype(&::X509_free)>;
	using KeyPtr = up<EVP_PKEY, decltype(&::EVP_PKEY_free)>;
	constexpr long Day{ 24*60*60 };

	Ω parse( const vector<byte>& der )ε->X509Ptr{
		auto p = (const unsigned char*)der.data();
		return X509Ptr{ ::d2i_X509(nullptr, &p, (long)der.size()), ::X509_free };
	}
	Ω toDer( const X509Ptr& cert )ε->vector<byte>{
		unsigned char* p{};
		let size = ::i2d_X509( cert.get(), &p );
		THROW_IFX( size<=0, OpenSslException("i2d_X509") );
		vector<byte> der{ (const byte*)p, (const byte*)p+size };
		::OPENSSL_free( p );
		return der;
	}
	Ω newKey()ε->KeyPtr{
		KeyPtr key{ ::EVP_RSA_gen(2048), ::EVP_PKEY_free };
		THROW_IFX( !key, OpenSslException("EVP_RSA_gen(2048)") );
		return key;
	}
	//CreateKeyCertificate only mints fresh 365-day self-signed leaves; Verify's depth-0 time checks and its chain path need expired, not-yet-valid and CA-issued inputs.  issuer==nullptr self-signs.
	Ω mint( const char* commonName, EVP_PKEY* subjectKey, EVP_PKEY* issuerKey, X509* issuer, long notBefore, long notAfter, bool ca )ε->X509Ptr{
		X509Ptr cert{ ::X509_new(), ::X509_free };
		THROW_IFX( !cert, OpenSslException("X509_new") );
		::X509_set_version( cert.get(), 2 );//X509v3
		::ASN1_INTEGER_set( ::X509_get_serialNumber(cert.get()), (long)(Random<uint32_t>() & 0x7fffffff) );//see IssueCertificate: same-DN+same-serial certs are indistinguishable to an X509_STORE.
		::X509_gmtime_adj( ::X509_getm_notBefore(cert.get()), notBefore );
		::X509_gmtime_adj( ::X509_getm_notAfter(cert.get()), notAfter );
		::X509_set_pubkey( cert.get(), subjectKey );
		auto name = ::X509_get_subject_name( cert.get() );
		::X509_NAME_add_entry_by_txt( name, "CN", MBSTRING_ASC, (const unsigned char*)commonName, -1, -1, 0 );
		::X509_set_issuer_name( cert.get(), issuer ? ::X509_get_subject_name(issuer) : name );
		if( ca ){//without basicConstraints CA:TRUE X509_verify_cert rejects the anchor with X509_V_ERR_INVALID_CA.
			X509V3_CTX ctx;
			X509V3_set_ctx_nodb( &ctx );
			X509V3_set_ctx( &ctx, issuer ? issuer : cert.get(), cert.get(), nullptr, nullptr, 0 );
			up<X509_EXTENSION, decltype(&::X509_EXTENSION_free)> ext{ ::X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:TRUE"), ::X509_EXTENSION_free };
			THROW_IFX( !ext, OpenSslException("X509V3_EXT_conf_nid(basicConstraints)") );
			::X509_add_ext( cert.get(), ext.get(), -1 );
		}
		THROW_IFX( !::X509_sign(cert.get(), issuerKey, ::EVP_sha256()), OpenSslException("X509_sign") );
		return cert;
	}

	struct TrustStoreTests : public ::testing::Test{
	protected:
		Ω SetUpTestCase()->void;
		Ω TearDownTestCase()->void;
		Ω SslSettings( str publicKeyFile, str privateKeyFile, str certificateFile )ε->CryptoSettings;

		static string passcode;
		static string PublicKeyFile;
		static string PrivateKeyFile;
		static string CertificateFile;
		static string PublicKeyFile2;
		static string PrivateKeyFile2;
		static string CertificateFile2;
		static vector<byte> Der;
		static vector<byte> Der2;//distinct key pair, same subject DN as Der.
	};
	string TrustStoreTests::passcode{ "123456789" };
	string TrustStoreTests::PublicKeyFile;
	string TrustStoreTests::PrivateKeyFile;
	string TrustStoreTests::CertificateFile;
	string TrustStoreTests::PublicKeyFile2;
	string TrustStoreTests::PrivateKeyFile2;
	string TrustStoreTests::CertificateFile2;
	vector<byte> TrustStoreTests::Der;
	vector<byte> TrustStoreTests::Der2;

	α TrustStoreTests::SslSettings( str publicKeyFile, str privateKeyFile, str certificateFile )ε->CryptoSettings{//both pairs share the subject DN - SameSubjectAnchors depends on it.
		return CryptoSettings{ jobject{
			{"certificate", jobject{{"path", certificateFile}, {"subjectAltName", "URI:urn:my.server.application"}, {"company", "jde-cpp"}, {"country", "US"}, {"commonName", "trustStoreTests"}}},
			{"privateKey", jobject{{"path", privateKeyFile}, {"passcode", passcode}}},
			{"publicKey", jobject{{"path", publicKeyFile}}},
			{"dh", ""}//explicit, or CreateDirectories makes <programData>/Jde-Cpp/<productName>/ssl for a file no test reads (#27).
		}, {} };
	}

	α TrustStoreTests::SetUpTestCase()->void{
		let dir = Tests::FixtureDir( "trustStore" );
		PublicKeyFile = ( dir/"a-public.pem" ).string();
		PrivateKeyFile = ( dir/"a-private.pem" ).string();
		CertificateFile = ( dir/"a-cert.pem" ).string();
		PublicKeyFile2 = ( dir/"b-public.pem" ).string();
		PrivateKeyFile2 = ( dir/"b-private.pem" ).string();
		CertificateFile2 = ( dir/"b-cert.pem" ).string();
		Crypto::CreateKeyCertificate( SslSettings(PublicKeyFile, PrivateKeyFile, CertificateFile) );
		Der = Crypto::ReadCertificate( CertificateFile );
		Crypto::CreateKeyCertificate( SslSettings(PublicKeyFile2, PrivateKeyFile2, CertificateFile2) );
		Der2 = Crypto::ReadCertificate( CertificateFile2 );
	}

	α TrustStoreTests::TearDownTestCase()->void{
		std::error_code ec;//a teardown must not throw over whatever the tests reported.
		fs::remove_all( fs::path{PublicKeyFile}.parent_path(), ec );//the whole fixture dir - SetUpTestCase mints both pairs unconditionally, so none of it is state a later run reuses (#25).
	}

	TEST_F( TrustStoreTests, SelfSignedNotTrusted ){
		TrustStore store;
		EXPECT_FALSE( store.IsTrusted(Der) );
		try{
			store.Verify( Der );
			FAIL() << "Verify should have thrown.";
		}catch( const Exception& e ){
			EXPECT_EQ( e.Code(), (uint32)X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT );
		}
	}

	TEST_F( TrustStoreTests, AddCertificateTrusts ){
		TrustStore store{ false };
		EXPECT_FALSE( store.IsTrusted(Der) );
		store.AddCertificate( Der );
		store.Verify( Der );
		EXPECT_TRUE( store.IsTrusted(Der) );
	}

	TEST_F( TrustStoreTests, SameSubjectAnchors ){//OpenSSL's issuer-name lookup can't distinguish same-DN anchors on its own - the exact-match path must trust both.
		TrustStore store{ false };
		store.AddCertificate( Der );
		store.AddCertificate( Der2 );
		store.Verify( Der );
		store.Verify( Der2 );
		EXPECT_TRUE( store.IsTrusted(Der) );
		EXPECT_TRUE( store.IsTrusted(Der2) );
	}

	TEST_F( TrustStoreTests, ExpiredAnchorRejected ){//the exact-anchor shortcut skips chain verification, so it has to re-run the validity window itself.
		let key = newKey();
		let der = toDer( mint("trustStoreExpired", key.get(), key.get(), nullptr, -2*Day, -Day, false) );
		TrustStore store{ false };
		store.AddCertificate( der );
		EXPECT_FALSE( store.IsTrusted(der) );
		try{
			store.Verify( der );
			FAIL() << "Verify should have thrown.";
		}catch( const Exception& e ){
			EXPECT_EQ( e.Code(), (uint32)X509_V_ERR_CERT_HAS_EXPIRED );
		}
	}

	TEST_F( TrustStoreTests, NotYetValidAnchorRejected ){
		let key = newKey();
		let der = toDer( mint("trustStoreFuture", key.get(), key.get(), nullptr, Day, 2*Day, false) );
		TrustStore store{ false };
		store.AddCertificate( der );
		EXPECT_FALSE( store.IsTrusted(der) );
		try{
			store.Verify( der );
			FAIL() << "Verify should have thrown.";
		}catch( const Exception& e ){
			EXPECT_EQ( e.Code(), (uint32)X509_V_ERR_CERT_NOT_YET_VALID );
		}
	}

	TEST_F( TrustStoreTests, LeafChainsToAddedCa ){//the header's contract - trusted iff it chains to a root; only the failure direction was covered.
		let caKey = newKey(), leafKey = newKey();
		let ca = mint( "trustStoreTestsCa", caKey.get(), caKey.get(), nullptr, 0, Day, true );
		let leafDer = toDer( mint("trustStoreTestsLeaf", leafKey.get(), caKey.get(), ca.get(), 0, Day, false) );
		TrustStore noAnchor{ false };
		try{
			noAnchor.Verify( leafDer );
			FAIL() << "Verify should have thrown.";
		}catch( const Exception& e ){
			EXPECT_EQ( e.Code(), (uint32)X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY );
		}
		TrustStore store{ false };
		store.AddCertificate( toDer(ca) );//only the issuer - the leaf is trusted through the chain, not by exact match.
		store.Verify( leafDer );
		EXPECT_TRUE( store.IsTrusted(leafDer) );
	}

	TEST_F( TrustStoreTests, MalformedDerRejected ){//parseDer's throw is otherwise unreachable through the public api.
		let truncated = vector<byte>{ Der.begin(), Der.begin()+Der.size()/2 };
		TrustStore store{ false };
		EXPECT_THROW( store.AddCertificate(truncated), OpenSslException );
		EXPECT_THROW( store.Verify(truncated), OpenSslException );
		EXPECT_FALSE( store.IsTrusted(truncated) );
		EXPECT_THROW( store.Verify(vector<byte>{}), OpenSslException );
	}

	TEST_F( TrustStoreTests, SerialsRandomized ){//IssueCertificate must never reuse a constant serial - same-DN+same-serial re-issues are indistinguishable by subject in an X509_STORE.
		let cert = parse( Der ), cert2 = parse( Der2 );
		ASSERT_TRUE( cert && cert2 );
		let sn = ::X509_get_serialNumber( cert.get() ), sn2 = ::X509_get_serialNumber( cert2.get() );
		EXPECT_NE( ::ASN1_INTEGER_cmp(sn, sn2), 0 );
		up<ASN1_INTEGER, decltype(&::ASN1_INTEGER_free)> one{ ::ASN1_INTEGER_new(), ::ASN1_INTEGER_free };
		::ASN1_INTEGER_set( one.get(), 1 );
		EXPECT_NE( ::ASN1_INTEGER_cmp(sn, one.get()), 0 );//the historical hardcoded value.
		EXPECT_NE( ::ASN1_INTEGER_cmp(sn2, one.get()), 0 );
	}

	//Deliberately a load-does-not-throw smoke test on linux, and CertCount cannot make it more than that: it counts
	//loaded objects, and X509_STORE_set_default_paths leaves the hash-dir lookup lazy, so a perfectly working
	//SSL_CERT_DIR-only install legitimately reports 0.  EXPECT_GT would false-fail on one and a GTEST_SKIP on 0 would
	//skip on one.  A real linux assertion needs a different observable - Verify of a cert known to chain to the system
	//store - which this suite has no offline source for.  The WARN that used to stand in for an assertion here is gone:
	//it only repeated the one TrustStore's own ctor already emits on the same condition (#30).
	TEST_F( TrustStoreTests, OsStoreLoads ){
		TrustStore store;//on linux, the ctor throwing is the whole of what this covers.
		if( _msvc )
			EXPECT_GT( store.CertCount(), (uint)0 );//winstore load is eager and ROOT is always populated.
	}
}