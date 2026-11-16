#pragma once
#include <openssl/x509v3.h>
#include <jde/fwk/crypto/digest.h>
#include <jde/fwk/crypto/OpenSslException.h>
#include "../testsDir.h"

namespace Jde::Crypto::Tests{
	//Both crypto fixtures used to mint into fixed absolute paths - /tmp/public.pem, or ProgramData/Jde-Cpp/<product>
	//on windows - so every build and both test workflows wrote the same files, and with cryptoTests.clear:false a
	//leftover from one run fed the next (#26).  TestsDir's per-workflow root, plus a per-suite leaf so the two fixtures
	//cannot collide with each other either.  Deliberately not process-scoped: clear:false means to reuse the key pair
	//across runs, and a pid in the path would silently defeat that.
	Ξ FixtureDir( sv suite )->fs::path{
		const auto dir = Jde::Tests::TestsDir( fs::path{"crypto"}/suite );
		if( !fs::exists(dir) )
			fs::create_directories( dir );
		return dir;
	}

	using X509Ptr = up<X509, decltype(&::X509_free)>;
	constexpr long Day{ 24*60*60 };
	Ξ parse( const vector<byte>& der )ε->X509Ptr{
		auto p = (const unsigned char*)der.data();
		return X509Ptr{ ::d2i_X509(nullptr, &p, (long)der.size()), ::X509_free };
	}
	Ξ toDer( const X509Ptr& cert )ε->vector<byte>{
		unsigned char* p{};
		const auto size = ::i2d_X509( cert.get(), &p );
		THROW_IFX( size<=0, OpenSslException("i2d_X509") );
		vector<byte> der{ (const byte*)p, (const byte*)p+size };
		::OPENSSL_free( p );
		return der;
	}
	//CreateKeyCertificate only mints fresh 365-day self-signed leaves with IssueCertificate's extensions; Verify's depth-0 time checks
	//and its chain path need expired, not-yet-valid and CA-issued inputs, and ReissueReason one with a SAN alone.  issuer==nullptr self-signs.
	Ξ mint( const char* commonName, EVP_PKEY* subjectKey, EVP_PKEY* issuerKey, X509* issuer, long notBefore, long notAfter, bool ca, const char* subjectAltName=nullptr )ε->X509Ptr{
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
		auto addExtension = [&]( int nid, const char* value ){
			X509V3_CTX ctx;
			X509V3_set_ctx_nodb( &ctx );
			X509V3_set_ctx( &ctx, issuer ? issuer : cert.get(), cert.get(), nullptr, nullptr, 0 );
			up<X509_EXTENSION, decltype(&::X509_EXTENSION_free)> ext{ ::X509V3_EXT_conf_nid(nullptr, &ctx, nid, value), ::X509_EXTENSION_free };
			THROW_IFX( !ext, OpenSslException(Ƒ("X509V3_EXT_conf_nid({})", ::OBJ_nid2sn(nid))) );
			::X509_add_ext( cert.get(), ext.get(), -1 );
		};
		if( ca )//without basicConstraints CA:TRUE X509_verify_cert rejects the anchor with X509_V_ERR_INVALID_CA.
			addExtension( NID_basic_constraints, "critical,CA:TRUE" );
		if( subjectAltName )
			addExtension( NID_subject_alt_name, subjectAltName );
		THROW_IFX( !::X509_sign(cert.get(), issuerKey, ::EVP_sha256()), OpenSslException("X509_sign") );
		return cert;
	}
}