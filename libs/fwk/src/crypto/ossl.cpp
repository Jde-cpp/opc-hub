#include "ossl.h"
#include <jde/fwk/exceptions/IOException.h>

#define let const auto

namespace Jde::Crypto{
	α Internal::ReadFile( const fs::path& path, SL sl )ε->BioPtr{
		THROW_IFX( !fs::exists(path), IO::IOException(path, "File does not exist.") );
		BioPtr y{ ::BIO_new_file(path.string().c_str(), "rb"), ::BIO_free }; OSSL_CHECK( y, sl );
		return y;
	}
	α Internal::WriteFile( const fs::path& path, SL sl )ε->BioPtr{
		BioPtr y{ ::BIO_new_file(path.string().c_str(), "w"), ::BIO_free }; OSSL_CHECK( y, sl );
		return y;
	}
	α Internal::MemBio( std::span<const byte> bytes, SL sl )ε->BioPtr{
		BioPtr y{ ::BIO_new_mem_buf(bytes.data(), (int)bytes.size()), ::BIO_free }; OSSL_CHECK( y, sl );
		return y;
	}
	α Internal::ParseDer( std::span<const byte> der, SL sl )ε->X509Ptr{
		const unsigned char* p = (const unsigned char*)der.data();
		X509Ptr y{ ::d2i_X509(nullptr, &p, (long)der.size()), ::X509_free };
		THROW_IFX( !y, OpenSslException("d2i_X509", sl) );
		return y;
	}
	α Internal::ToDer( const X509* cert, SL sl )ε->vector<byte>{
		auto len = ::i2d_X509( cert, nullptr ); THROW_IFX( len<=0, OpenSslException("i2d_X509 failed", sl) );
		vector<byte> y( len );
		auto p = (unsigned char*)y.data();
		len = ::i2d_X509( cert, &p ); THROW_IFX( len<=0, OpenSslException("i2d_X509 failed", sl) );
		return y;
	}
	α Internal::ToBigNum( std::span<const byte> x, SL sl )ε->BNPtr{
		BNPtr y{ ::BN_bin2bn((const unsigned char*)x.data(), (int)x.size(), nullptr), ::BN_free }; OSSL_CHECK( y, sl );
		return y;
	}
	α Internal::ToString( const X509_NAME* name, SL sl )ε->string{
		BioPtr mem{ ::BIO_new(::BIO_s_mem()), ::BIO_free }; OSSL_CHECK( mem, sl );
		THROW_IFX( ::X509_NAME_print_ex(mem.get(), name, 0, XN_FLAG_RFC2253)<0, OpenSslException("X509_NAME_print_ex failed", sl) );
		char* data{};
		let len = ::BIO_get_mem_data( mem.get(), &data );
		return { data, (uint)len };
	}
}