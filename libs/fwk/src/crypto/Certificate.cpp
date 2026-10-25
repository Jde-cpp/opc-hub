#include <jde/fwk/crypto/Certificate.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/str.h>
#include "ossl.h"

#define let const auto

namespace Jde{
	using namespace Crypto::Internal;
	namespace Crypto{
		Ω asn1String( const ASN1_STRING* s )ι->string{ return {(const char*)::ASN1_STRING_get0_data(s), (uint)::ASN1_STRING_length(s)}; }
		Ω ipString( const ASN1_OCTET_STRING* s )ι->string{
			let n = (uint)::ASN1_STRING_length( s );
			let p = ::ASN1_STRING_get0_data( s );
			if( n==4 )
				return Ƒ( "{}.{}.{}.{}", p[0], p[1], p[2], p[3] );
			string y;//ipv6 - colon-separated hextets.
			for( uint i=0; i+1<n; i+=2 )
				y += Ƒ( "{}{:x}", y.empty() ? "" : ":", (uint)((p[i]<<8)|p[i+1]) );
			return y;
		}
		//renders one SAN entry back into openssl config syntax; empty for types we can't round-trip, which are then dropped.
		Ω sanEntry( const GENERAL_NAME& gn )ι->string{
			switch( gn.type ){
			case GEN_EMAIL: return "email:"+asn1String( gn.d.rfc822Name );
			case GEN_DNS: return "DNS:"+asn1String( gn.d.dNSName );
			case GEN_URI: return "URI:"+asn1String( gn.d.uniformResourceIdentifier );
			case GEN_IPADD: return "IP:"+ipString( gn.d.iPAddress );
			case GEN_OTHERNAME:{
				let other = gn.d.otherName;//only the ms UPN form, the one IssueCertificate emits.
				return ::OBJ_obj2nid( other->type_id )==NID_ms_upn && other->value && other->value->type==V_ASN1_UTF8STRING
					? "otherName:"+string{SN_ms_upn}+";UTF8:"+asn1String( other->value->value.utf8string )
					: string{};
			}
			default: return {};
			}
		}

		Certificate::Certificate( std::span<const byte> der, SL sl )ε{
			let cert = ParseDer( der, sl );
			Issuer = Internal::ToString( ::X509_get_issuer_name(cert.get()), sl );
			auto subject = ::X509_get_subject_name( cert.get() );
			DistinguishedName = Internal::ToString( subject, sl );
			if( let i = ::X509_NAME_get_index_by_NID(subject, NID_commonName, -1); i>=0 )
				CommonName = asn1String( ::X509_NAME_ENTRY_get_data(::X509_NAME_get_entry(subject, i)) );
			const up<GENERAL_NAMES, decltype( &::GENERAL_NAMES_free )> sans{ (GENERAL_NAMES*)::X509_get_ext_d2i(cert.get(), NID_subject_alt_name, nullptr, nullptr), ::GENERAL_NAMES_free };//null = no SAN extension, not an error.
			vector<string> sanEntries;
			for( int i = 0; sans && i<sk_GENERAL_NAME_num(sans.get()); ++i ){
				let gn = sk_GENERAL_NAME_value( sans.get(), i );
				if( gn->type==GEN_EMAIL && Email.empty() )
					Email = asn1String( gn->d.rfc822Name );
				else if( gn->type==GEN_OTHERNAME && Upn.empty() ){ //value type checked - the cert is parsed pre-verification, treat as untrusted input.
					let other = gn->d.otherName;
					if( ::OBJ_obj2nid(other->type_id)==NID_ms_upn && other->value && other->value->type==V_ASN1_UTF8STRING )
						Upn = asn1String( other->value->value.utf8string );
				}
				if( auto entry = sanEntry(*gn); entry.size() )
					sanEntries.push_back( move(entry) );
			}
			SubjectAltName = Str::Join( sanEntries );
			tm t{};
			THROW_IFX( ::ASN1_TIME_to_tm(::X509_get0_notAfter(cert.get()), &t)!=1, OpenSslException("ASN1_TIME_to_tm failed", sl) );
			using namespace std::chrono;
			Expiration = sys_days{ year{t.tm_year+1900}/month{(unsigned)t.tm_mon+1}/day{(unsigned)t.tm_mday} } + hours{ t.tm_hour } + minutes{ t.tm_min } + seconds{ t.tm_sec };
			//the whole DER, not the key:  what openssl and every browser's certificate viewer print, so an operator can match it by eye.
			unsigned char md[EVP_MAX_MD_SIZE]; unsigned mdLength{};
			THROW_IFX( ::X509_digest(cert.get(), ::EVP_sha256(), md, &mdLength)!=1, OpenSslException("X509_digest failed", sl) );
			Fingerprint.reserve( mdLength*3 );
			for( unsigned i = 0; i<mdLength; ++i )
				Fingerprint += Ƒ( "{}{:02X}", i ? ":" : "", md[i] );
		}

		α Certificate::Log( string prefix, SL sl )Ι->void{
			LOGSL( ELogLevel::Information, sl, ELogTags::Crypto, "{} {}", prefix, ToString() );
		}
		α Certificate::ToString()Ι->string{
			return Ƒ(
				"issuer: {}, subject: {}, SubjectAltName: {}, CommonName: {}, UPN: {}, Email: {}, Expiration: {}, Fingerprint: {}",
				Issuer, DistinguishedName, SubjectAltName, CommonName, Upn, Email, ToIsoString<std::chrono::days>( Expiration ), Fingerprint
			);
		}
	}

	α Crypto::SanUri( sv subjectAltName )ι->string{
		let i = subjectAltName.starts_with( "URI:" ) ? 0 : subjectAltName.find( ",URI:" );
		if( i==sv::npos )
			return {};
		let start = i + (i ? 5 : 4);
		let end = subjectAltName.find( ',', start );
		return string{ subjectAltName.substr(start, end==sv::npos ? sv::npos : end-start) };
	}

	α Crypto::ReadCertificate( const fs::path& certificate, SL sl )ε->vector<byte>{
		X509Ptr cert{ ::PEM_read_bio_X509(ReadFile(certificate, sl).get(), nullptr, 0, nullptr), ::X509_free };
		if( !cert ){//not PEM - try DER, which is what an OPC UA server publishes its instance certificate as (install-issues #33).
			::ERR_clear_error();//the PEM attempt's "no start line" would otherwise ride along on a later, unrelated exception.
			cert = X509Ptr{ ::d2i_X509_bio(ReadFile(certificate, sl).get(), nullptr), ::X509_free };//a second open, not BIO_reset:  file BIOs invert its return.
		}
		//not OSSL_CHECK:  "null returned" names no file, and a process reads several certificates - the web cert, the ua server
		//cert, one per opc slug, every trust anchor - so the path is what tells the operator which one is bad.  A *missing* file
		//already throws IOException(path); this is the parse failure of both encodings.  The openssl reason rides along from the
		//ERR queue - DER's ("wrong tag"), since PEM's was cleared - and the exception takes the rest of the queue with it
		//(CurrentErrorCode):  the second parse doubled what a bad file left behind.
		THROW_IFX( !cert, OpenSslException(Ƒ("Could not parse certificate '{}' as PEM or DER", certificate.string()), sl) );
		return ToDer( cert.get(), sl );
	}

	α Crypto::WriteCertificate( const fs::path& path, std::span<const byte> certificate, SL sl )ε->void{
		let der = ToDer( ParseDer(certificate, sl).get(), sl );//the first certificate alone, re-encoded:  a UA peer may send its chain after it.
		IO::SaveBinary( path, std::span{der}, false, sl );//not WriteFile:  its "w" is text mode on Windows, which mangles DER.
	}
}