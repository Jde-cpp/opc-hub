#include <jde/fwk/crypto/digest.h>
#include <openssl/rand.h>
#include "ossl.h"

namespace Jde{
	using namespace Crypto::Internal;

	α Crypto::CalcMd5( std::span<const byte> data )ε->MD5{
		if( data.empty() )
			return EmptyStringMd5;
		MDCtxPtr ctx{ ::EVP_MD_CTX_new(), ::EVP_MD_CTX_free }; OSSL_CHECK( ctx, SRCE_CUR );
		OSSL_CALL( ::EVP_DigestInit_ex(ctx.get(), ::EVP_md5(), nullptr), SRCE_CUR );
		OSSL_CALL( ::EVP_DigestUpdate(ctx.get(), data.data(), data.size()), SRCE_CUR );
		MD5 md5;
		unsigned int size;
		OSSL_CALL( ::EVP_DigestFinal_ex(ctx.get(), md5.data(), &size), SRCE_CUR );
		return md5;
	}

	α Crypto::Random( std::span<byte> bytes )ε->void{
		THROW_IFX( ::RAND_bytes((unsigned char*)bytes.data(), (int)bytes.size())!=1, OpenSslException(Ƒ("RAND_bytes({}) failed", bytes.size())) );
	}
}