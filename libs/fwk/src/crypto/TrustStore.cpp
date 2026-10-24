#include <jde/fwk/crypto/TrustStore.h>
#ifdef _WIN32
	#include <windows.h>
	#include <wincrypt.h>
#endif
#include <openssl/x509_vfy.h>
#include <jde/fwk/crypto/Certificate.h>
#include "ossl.h"

#define let const auto

namespace Jde::Crypto{
	[[maybe_unused]] constexpr ELogTags _tags{ ELogTags::Crypto };//WARN only fires on the linux path.
	using namespace Internal;

	TrustStore::TrustStore( bool loadOsStore, SL sl )ε:
		_store{ ::X509_STORE_new(), ::X509_STORE_free }{
		OSSL_CHECK( _store, sl );
		if( !loadOsStore )
			return;
#ifdef _WIN32
		//OpenSSL's winstore loader only serves by-subject searches (providers/implementations/storemgmt/winstore_store.c) - enumerate ROOT directly for an eager snapshot.
		let winStore = ::CertOpenSystemStoreW( 0, L"ROOT" );
		THROW_IFX( !winStore, OpenSslException("CertOpenSystemStoreW(ROOT) failed", sl) );
		for( PCCERT_CONTEXT winCert{}; (winCert = ::CertEnumCertificatesInStore(winStore, winCert))!=nullptr; ){
			const unsigned char* p = winCert->pbCertEncoded;
			if( X509Ptr cert{ ::d2i_X509(nullptr, &p, (long)winCert->cbCertEncoded), ::X509_free }; cert )
				::X509_STORE_add_cert( _store.get(), cert.get() );//skip unparseable entries; duplicates return success.
		}
		::CertCloseStore( winStore, 0 );
#else
		OSSL_CALL( ::X509_STORE_set_default_paths(_store.get()), sl );
		if( CertCount()==0 && !std::getenv("SSL_CERT_FILE") && !std::getenv("SSL_CERT_DIR") ){
			for( const char* bundle : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt"} ){
				if( fs::exists(bundle) && ::X509_STORE_load_locations(_store.get(), bundle, nullptr)==1 )
					break;
			}
			if( CertCount()==0 )
				WARN( "OS trust store is empty - set SSL_CERT_FILE/SSL_CERT_DIR if OpenSSL's default paths are wrong." );
		}
#endif
	}

	α TrustStore::AddCertificate( std::span<const byte> der, SL sl )ε->void{
		let cert = ParseDer( der, sl );
		OSSL_CALL( ::X509_STORE_add_cert(_store.get(), cert.get()), sl );//up-refs the cert.
	}

	α TrustStore::Verify( std::span<const byte> der, SL sl )Ε->void{
		let cert = ParseDer( der, sl );
		//OpenSSL resolves anchors by subject name and X509_likely_issued never checks signatures, so same-DN anchors (every CreateCertificate cert is CN=localhost) can bind the wrong "issuer" and fail depth-zero-self-signed. An exact anchor match is a depth-0 chain - check it first, keeping the time checks chain verification would have run.
		auto anchors = ::X509_STORE_get1_all_certs( _store.get() );
		bool anchored{};
		for( int i = 0; !anchored && i<sk_X509_num(anchors); ++i )
			anchored = ::X509_cmp( sk_X509_value(anchors, i), cert.get() )==0;
		::sk_X509_pop_free( anchors, ::X509_free );
		if( anchored ){
			let err = ::X509_cmp_current_time(::X509_get0_notBefore(cert.get()))>0 ? X509_V_ERR_CERT_NOT_YET_VALID
				: ::X509_cmp_current_time(::X509_get0_notAfter(cert.get()))<0 ? X509_V_ERR_CERT_HAS_EXPIRED
				: X509_V_OK;
			if( err==X509_V_OK )
				return;
			throw OpenSslException{ ::X509_verify_cert_error_string(err), "Certificate not trusted. depth: 0", {ELogLevel::Warning, ELogTags::Crypto, (uint32)err}, sl };
		}
		up<X509_STORE_CTX, decltype(&::X509_STORE_CTX_free)> ctx{ ::X509_STORE_CTX_new(), ::X509_STORE_CTX_free };
		OSSL_CHECK( ctx, sl );
		OSSL_CALL( ::X509_STORE_CTX_init(ctx.get(), _store.get(), cert.get(), nullptr), sl );
		if( ::X509_verify_cert(ctx.get())!=1 ){//verify error lives in the ctx, not the ERR queue.
			let err = ::X509_STORE_CTX_get_error( ctx.get() );
			Certificate{ der, sl }.Log( Ƒ("Certificate verification failed with error: {}", ::X509_verify_cert_error_string(err)) );
			throw OpenSslException{ ::X509_verify_cert_error_string(err), Ƒ("Certificate not trusted. depth: {}", ::X509_STORE_CTX_get_error_depth(ctx.get())), {ELogLevel::Warning, ELogTags::Crypto, (uint32)err}, sl };
		}
	}

	α TrustStore::IsTrusted( std::span<const byte> der )Ι->bool{
		try{
			Verify( der );
			return true;
		}catch( const std::exception& ){
			return false;
		}
	}

	α TrustStore::CertCount()Ι->uint{
		return (uint)sk_X509_OBJECT_num( ::X509_STORE_get0_objects(_store.get()) );
	}
}