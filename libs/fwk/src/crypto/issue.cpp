#include <jde/fwk/crypto/issue.h>
#include <fstream>
#include <mutex>
#include <absl/strings/match.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <jde/fwk/crypto/Certificate.h>
#include <jde/fwk/crypto/PrivateKey.h>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/durable.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/str.h>
#include "ossl.h"

#define let const auto

namespace Jde{
	using namespace Crypto::Internal;
	namespace Crypto{
		constexpr ELogTags _tags{ ELogTags::Crypto };

		//SAN entry types a re-issue comparison can trust: sanEntry() renders these back byte-for-byte.  otherName is excluded
		//on both sides - an OID spelling comes back as its short name (1.3.6.1.4.1.311.20.2.3 → msUPN), which would re-issue
		//on every start.  Entries are normalized ("dns : x" → "DNS:x") so conf-syntax slack doesn't read as drift.
		Ω comparableSanEntries( sv san )ι->vector<string>{
			constexpr array<sv,4> types{ "DNS", "IP", "URI", "email" };
			vector<string> y;
			for( let& entry : Str::Split(san) ){
				let colon = entry.find( ':' );
				if( colon==sv::npos )
					continue;
				let type = Str::Trim( entry.substr(0, colon) );
				let p = find_if( types, [type](sv t){ return absl::EqualsIgnoreCase(type, t); } );
				if( p!=types.end() )
					y.push_back( Ƒ("{}:{}", *p, Str::Trim(entry.substr(colon+1))) );
			}
			return y;
		}

		//X509_check_private_key:  the certificate carries the key's public half.
		Ω issuedOnKey( std::span<const byte> der, const CryptoSettings& settings, SL sl )ε->bool{
			let cert = ParseDer( der, sl );
			const PrivateKey key{ settings.PrivateKey.Path, settings.PrivateKey.Passcode, sl };
			let y = ::X509_check_private_key( cert.get(), key.Native() )==1;
			::ERR_clear_error();//a mismatch queues "key values mismatch" - it would ride along on a later, unrelated exception.
			return y;
		}
	}

	//https://stackoverflow.com/questions/5927164/how-to-generate-rsa-private-key-using-openssl
	α Crypto::CreateKey( const CryptoSettings& settings, SL sl )ε->void{
		auto key = PrivateKey::Generate( sl );
		settings.CreateDirectories();
		key.WritePublic( settings.PublicKey.Path, sl );
		INFO( "Created public key at {}", settings.PublicKey.Path.string() );
		key.Write( settings.PrivateKey.Path, settings.PrivateKey.Passcode, sl );
		INFO( "Created private key at {} ({}).", settings.PrivateKey.Path.string(), settings.PrivateKey.Passcode.empty() ? "unencrypted - privateKey.passcode is empty" : "encrypted at rest" );
	}

	α Crypto::IssueCertificate( const CryptoSettings& settings, std::chrono::seconds validity, SL sl )ε->void{
		X509Ptr cert{ ::X509_new(), ::X509_free }; OSSL_CHECK( cert, sl );
		auto pCert = cert.get();

		//random serial, never a constant: re-issues keep the same subject DN, and same-DN+same-serial certs are
		//indistinguishable to anything that resolves anchors by subject (X509_STORE) - RFC 5280 caps serials at 20 octets.
		array<byte,16> serial;
		Random( serial );
		serial[0] &= byte{ 0x7f };//DER serials must be positive.
		OSSL_CHECK( ::BN_to_ASN1_INTEGER(ToBigNum(serial, sl).get(), ::X509_get_serialNumber(pCert)), sl );
		::X509_set_version( pCert, 2 );//X509v3
		::X509_gmtime_adj( ::X509_getm_notBefore(pCert), 0 );
		::X509_gmtime_adj( ::X509_getm_notAfter(pCert), (long)validity.count() );
		const PrivateKey key{ settings.PrivateKey.Path, settings.PrivateKey.Passcode, sl };
		::X509_set_pubkey( pCert, key.Native() );

		auto add_x509V3ext = [&]( int nid, const char* value )ε{
			X509V3_CTX ctx;
			X509V3_set_ctx_nodb( &ctx );
			X509V3_set_ctx( &ctx, pCert, pCert, nullptr, nullptr, 0 );
			const up<X509_EXTENSION, decltype( &::X509_EXTENSION_free )> ex{ X509V3_EXT_conf_nid(nullptr, &ctx, nid, value), ::X509_EXTENSION_free }; OSSL_CHECK( ex, sl );
			X509_add_ext( pCert, ex.get(), -1 );
		};
		let& certSettings = settings.Certificate;
		if( certSettings.SubjectAltName.size() )
			add_x509V3ext( NID_subject_alt_name, certSettings.SubjectAltName.c_str() );

		auto name{ ::X509_get_subject_name(pCert) };
		if( !certSettings.Country.empty() )
			::X509_NAME_add_entry_by_txt( name, "C", MBSTRING_ASC, (const unsigned char*)certSettings.Country.c_str(), -1, -1, 0 ); //country code
		if( !certSettings.Company.empty() )
			::X509_NAME_add_entry_by_txt( name, "O", MBSTRING_ASC, (const unsigned char*)certSettings.Company.c_str(), -1, -1, 0 ); //organization name
		if( !certSettings.CommonName.empty() )
			::X509_NAME_add_entry_by_txt( name, "CN", MBSTRING_ASC, (const unsigned char*)certSettings.CommonName.c_str(), -1, -1, 0 ); //common name
		::X509_set_issuer_name( pCert, name );
		::X509_sign( pCert, key.Native(), ::EVP_sha256() );

		let& path = certSettings.Path;
		if( !fs::exists(path.parent_path()) )
			IO::CreateDirectories( path.parent_path() );
		OSSL_CALL( ::PEM_write_bio_X509(WriteFile(path, sl).get(), pCert), sl );//the BIO closes with the statement, so the read below sees the file.
		Certificate{ ReadCertificate(path, sl), sl }.Log( Ƒ("Issued Certificate at {}", path.string()), sl );
	}

	//why the on-disk certificate can no longer stand for `settings`, empty if it can.  It used to stand forever: a cert
	//issued before its config gained a subjectAltName kept failing host_name_verification until an operator hand-deleted
	//the pems.  A re-issue keeps the key pair, so modulus-keyed enrollment and public-key trust survive; only anchors
	//holding the exact bytes have to re-read the file.  Configs sharing one path with differing SANs re-issue on every
	//start - the caller's log line names both sides, making that visible.
	α Crypto::ReissueReason( const CryptoSettings& settings, SL sl )ε->string{
		if( !fs::exists(settings.Certificate.Path) )
			return "no certificate";
		let der = ReadCertificate( settings.Certificate.Path, sl );//unreadable throws - a damaged install, not something to paper over by minting a replacement.
		//Issued on another key - another checkout's sharing the path, a key replaced and the certificate kept - it is presented, and
		//nothing encrypted to it can be decrypted:  every UA token encrypted to the test OpcServer's failed (2026-10).
		if( !issuedOnKey(der, settings, sl) )
			return Ƒ( "not issued on the private key '{}'", settings.PrivateKey.Path.string() );
		let onDisk = Certificate{ der, sl };
		//a day's margin:  a certificate that outlives the check by an hour is presented, trusted, and then rejected mid-session
		//by the peer's own clock - re-issuing at the last start that sees it coming costs nothing, the key pair survives.
		if( let now = Clock::now(); onDisk.Expiration<=now+24h )
			return Ƒ( "{} {}", onDisk.Expiration<=now ? "expired" : "expires", ToIsoString<std::chrono::days>(onDisk.Expiration) );
		if( comparableSanEntries(settings.Certificate.SubjectAltName)!=comparableSanEntries(onDisk.SubjectAltName) )
			return Ƒ( "subjectAltName '{}' -> '{}'", onDisk.SubjectAltName, settings.Certificate.SubjectAltName );
		return {};
	}

	α Crypto::EncryptPrivateKey( const CryptoSettings& settings, SL sl )ε->void{
		let& path = settings.PrivateKey.Path;
		if( settings.PrivateKey.Passcode.empty() || !fs::exists(path) )
			return;
		let clear = PrivateKey::ReadUnencrypted( path, sl );
		if( !clear )
			return;
		//written beside it and renamed over it:  a crash mid-write must not cost the only copy of the key the certificates were issued on.
		let temp = fs::path{ path.string()+".encrypting" };
		clear->Write( temp, settings.PrivateKey.Passcode, sl );
		IO::Replace( temp, path, sl );
		INFO( "Encrypted the private key at {} with privateKey.passcode - it was written in the clear, before the passcode was set.", path.string() );
	}

	α Crypto::CreateKeyCertificate( const CryptoSettings& settings, SL sl )ε->void{
		CreateKey( settings, sl );
		IssueCertificate( settings, std::chrono::days{365}, sl );
	}

	//One lock for every path:  only an issue is slow, and the gateway's per-target certificates - a test connection and a connect,
	//or two tests, for a slug with no certificate - used to issue it twice, one reading the other's half-written PEM (#250 review #13).
	static std::mutex _ensureMutex;
	α Crypto::EnsureKeyCertificate( const CryptoSettings& settings, SL sl )ε->void{
		EnsureKeyCertificate( settings, ELogLevel::Information, sl );
	}
	α Crypto::EnsureKeyCertificate( const CryptoSettings& settings, ELogLevel found, SL sl )ε->void{
		const std::lock_guard _{ _ensureMutex };
		if( !settings.Certificate.Managed ){//the operator's pair, used as found:  the expiry and SAN checks below re-issue only what this product issued, and a certificate a CA vouched for must never be replaced by a self-signed one (web-certs3 (b)).
			THROW_IF( !fs::exists(settings.PrivateKey.Path), "certificate.managed is false and the private key '{}' does not exist - supply the pair, or set managed:true to have one issued.", settings.PrivateKey.Path.string() );
			THROW_IF( !fs::exists(settings.Certificate.Path), "certificate.managed is false and the certificate '{}' does not exist - supply the pair, or set managed:true to have one issued.", settings.Certificate.Path.string() );
			if( !fs::exists(settings.PublicKey.Path) ){//CreateKey writes it beside a key it generates; an operator's key comes without one, and the app server's identity (SetPublicKey) reads it.
				settings.CreateDirectories();
				PrivateKey{ settings.PrivateKey.Path, settings.PrivateKey.Passcode, sl }.WritePublic( settings.PublicKey.Path, sl );
				INFO( "Wrote the public key of '{}' to '{}'.", settings.PrivateKey.Path.string(), settings.PublicKey.Path.string() );
			}
			LOGSL( found, sl, _tags, "Read unmanaged certificate at {} {}", settings.Certificate.Path.string(), Certificate{ReadCertificate(settings.Certificate.Path, sl), sl}.ToString() );
			return;
		}
		bool unwritable{};
		try{
			if( !fs::exists(settings.PrivateKey.Path) )
				CreateKeyCertificate( settings, sl );
			else{
				EncryptPrivateKey( settings, sl );//before a re-issue, which signs with the key as it will be read from now on
				if( let reason = ReissueReason(settings, sl); reason.size() ){
					let& path = settings.Certificate.Path;
					//checked up front:  BIO_new_file's failure on Windows reports ERROR_ACCESS_DENIED (5) through strerror - "Input/output error".
					//Opened for append so nothing is truncated; only an existing file, so a failed check leaves no empty pem behind.
					unwritable = fs::exists( path ) && !std::ofstream{ path, std::ios::app }.is_open();
					THROW_IFSL( unwritable, "'{}' has to be re-issued ({}), but this process cannot write it.  Run once as an account that can (e.g. elevated, if an elevated install created it), or grant this account write access to it - the key is untouched either way.", path.string(), reason );
					INFO( "Re-issuing '{}': {}.", path.string(), reason );
					IssueCertificate( settings, std::chrono::days{365}, sl );
				}
			}
			LOGSL( found, sl, _tags, "Read Certificate at {} {}", settings.Certificate.Path.string(), Certificate{ReadCertificate(settings.Certificate.Path, sl), sl}.ToString() );
		}
		catch( IO::IOException& e ){
			e.PrependWhat( "Delete the file and restart to re-issue it on the existing key." );
			throw;
		}
		catch( Exception& e ){
			if( !unwritable )
				e.PrependWhat( Ƒ("Delete {} and restart to re-issue it on the existing key - the key is untouched, so the modulus and the enrolled identity survive.", settings.Certificate.Path.string()) );
			throw;
		}
	}
}