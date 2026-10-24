#pragma once
#include <span>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <jde/fwk/crypto/OpenSslException.h>

//the api return code goes in the description, not the exception Code - that slot is the packed ERR-queue code (lib|reason), a different domain than 1/0/-1 returns.
#define OSSL_CALL( call, sl ) if( int rc=call; rc!=1 ) throw Jde::Crypto::OpenSslException( Ƒ("{} -> {}", #call, rc), sl )
#define OSSL_CHECK( p, sl ) THROW_IFX( !(p), Jde::Crypto::OpenSslException("null returned", sl) )

namespace Jde::Crypto::Internal{
	using BioPtr = up<BIO, decltype(&::BIO_free)>;
	using KeyPtr = up<EVP_PKEY, decltype(&::EVP_PKEY_free)>;
	using CtxPtr = up<EVP_PKEY_CTX, decltype(&::EVP_PKEY_CTX_free)>;
	using BNPtr = up<BIGNUM, decltype(&::BN_free)>;
	using X509Ptr = up<X509, decltype(&::X509_free)>;
	using MDCtxPtr = up<EVP_MD_CTX, decltype(&::EVP_MD_CTX_free)>;

	α ReadFile( const fs::path& path, SL sl )ε->BioPtr;//a missing file is IOException(path);  "rb" - a Windows text-mode read ends a DER certificate at its first 0x1A byte.
	α WriteFile( const fs::path& path, SL sl )ε->BioPtr;//"w", text mode:  PEM only - DER goes through IO::SaveBinary.
	α MemBio( std::span<const byte> bytes, SL sl )ε->BioPtr;
	α ParseDer( std::span<const byte> der, SL sl )ε->X509Ptr;//the first certificate in `der`.
	α ToDer( const X509* cert, SL sl )ε->vector<byte>;
	α ToBigNum( std::span<const byte> x, SL sl )ε->BNPtr;
	α ToString( const X509_NAME* name, SL sl )ε->string;//RFC2253 one line.
}