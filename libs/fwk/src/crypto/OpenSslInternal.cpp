#include "OpenSslInternal.h"
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/exceptions/IOException.h>

#define let const auto

namespace Jde::Crypto{
	template<typename T, typename D>
	α MakeHandle( T* p, D deleter, SRCE )ε{
		THROW_IFX( !p, Crypto::OpenSslException("MakeHandle returned null", sl) );
		return up<T,D>{ p, deleter };
	}

	using namespace Jde::Crypto::Internal;
	α Internal::File( const fs::path& path, bool write, SL sl )ε->BioPtr{
		THROW_IFX( !write && !fs::exists(path), IO::IOException(path, "File does not exist.") );
		auto p = BIO_new_file(path.string().c_str(), write ? "w" : "rb"); CHECK_NULL( p );//"rb":  a Windows text-mode read ends a DER certificate at its first 0x1A byte.
		return BioPtr{ p, ::BIO_free };
	}
	α Internal::ReadFile( const fs::path& path, SL sl )ε->BioPtr{ return File( path, false, sl ); }
	α Internal::ReadPublicKey( const fs::path& path, SL sl )ε->KeyPtr{
		EVP_PKEY* pkey = ::PEM_read_bio_PUBKEY( ReadFile(path, sl).get(), nullptr, nullptr, nullptr );
		THROW_IFX( !pkey, Crypto::OpenSslException(Ƒ("Could not parse public key '{}'", path.string()), sl) );//not CHECK_NULL - name the file, as ReadCertificate does.
		return { pkey, ::EVP_PKEY_free };
	}
	α Internal::ReadPrivateKey( const fs::path& path, str passcode, SL sl )ε->KeyPtr{
		EVP_PKEY* pkey = ::PEM_read_bio_PrivateKey( ReadFile(path, sl).get(), nullptr, nullptr, (void*)passcode.c_str() );
		THROW_IFX( !pkey, Crypto::OpenSslException(Ƒ("Could not read private key '{}'{}", path.string(), passcode.empty() ? " (no passcode configured - an encrypted key needs privateKey.passcode)" : " (wrong passcode, or the key is not encrypted with this one)"), sl) );
		return { pkey, ::EVP_PKEY_free };
	}
	α Internal::ReadPrivateKey( BioPtr&& p, str passcode, SL sl )ε->KeyPtr{
		EVP_PKEY* pkey = ::PEM_read_bio_PrivateKey( p.get(), nullptr, nullptr, (void*)passcode.c_str() ); CHECK_NULL( pkey );
		return { pkey, ::EVP_PKEY_free };
	}
	α Internal::WritePrivateKey( const fs::path& path, KeyPtr&& key, str passcode, SL sl )ε->void{
		auto enc = passcode.empty() ? nullptr : EVP_aes_256_cbc();//openssl silently ignores the passphrase & writes cleartext when the cipher is null.
		CALL( ::PEM_write_bio_PrivateKey(File(path, true, sl).get(), key.get(), enc, passcode.empty() ? nullptr : (unsigned char*)passcode.c_str(), (int)passcode.size(), nullptr, nullptr) );
	}

	α Internal::NewRsaCtx(SL sl)ε->CtxPtr{
		auto pctx = MakeHandle( EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr), EVP_PKEY_CTX_free, sl );
		EVP_PKEY_keygen_init( pctx.get() );
		return pctx;
	}
	α Internal::NewCtx( const KeyPtr& key, SL sl )ε->CtxPtr{
		return MakeHandle( EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free, sl );
	}
	α Internal::ToBigNum( const vector<unsigned char>& x, SL sl )ε->BNPtr{
		auto p = MakeHandle( BN_bin2bn( (unsigned char*)x.data(), (int)x.size(), nullptr ), ::BN_free, SRCE_CUR ); CHECK_NULL( p.get() );
		return p;
	}
	α Internal::ToBio( std::span<byte> bytes, SL sl )ε->BioPtr{
		auto p = MakeHandle( BIO_new_mem_buf( bytes.data(), (int)bytes.size() ), ::BIO_free, SRCE_CUR ); CHECK_NULL( p.get() );
		return p;
	}

	α Internal::ToString( const X509_NAME* name, SL sl )ε->string{
		BioPtr mem{ ::BIO_new(::BIO_s_mem()), ::BIO_free }; CHECK_NULL( mem.get() );
		THROW_IFX( ::X509_NAME_print_ex(mem.get(), name, 0, XN_FLAG_RFC2253)<0, Crypto::OpenSslException("X509_NAME_print_ex failed", sl) );
		char* data{};
		let len = ::BIO_get_mem_data( mem.get(), &data );
		return { data, (uint)len };
	}
}