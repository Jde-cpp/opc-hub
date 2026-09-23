#include <jde/fwk/crypto/OpenSsl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include <openssl/x509v3.h>
#include "OpenSslInternal.h"
#include "jde/fwk/io/file.h"
#include <jde/fwk/str.h>
#include <jde/fwk/crypto/CryptoSettings.h>

#define let const auto

namespace Jde{
	using namespace Jde::Crypto::Internal;
	namespace Crypto{
		constexpr ELogTags _tags{ ELogTags::Crypto };
		α OpenSslException::CurrentError()ι->string{ return CurrentError(CurrentErrorCode()); }
		α OpenSslException::CurrentError( uint32 rc )ι->string{ if(!rc) return "no queued openssl error"; char b[256]; ERR_error_string_n(rc, b, sizeof(b)); return {b}; }//0 would format as 'error:00000000...' - noise masquerading as detail.
		//The earliest queued error, and the queue emptied behind it.  One failed call queues several - a DER parse leaves "wrong
		//tag" and then "nested asn1 error", the same failure seen from further out - and this popped the first alone, so the rest
		//stayed on the thread's queue to be the *first* thing the next OpenSslException on that thread found:  a trust scan that
		//skipped an unparseable .cer made a later, unrelated key failure read "asn1 encoding routines::wrong tag"
		//(reviews/m2-closing.md #10).  The earliest is the one kept - it is the cause; what follows it is its echo.
		α OpenSslException::CurrentErrorCode()ι->uint32{ let rc = (uint32)ERR_get_error(); ERR_clear_error(); return rc; }

		//https://stackoverflow.com/questions/1986888/how-to-compute-a-32-bit-fingerprint-of-a-certificate
		α PublicKey::Hash32()Ι->uint32_t{
			auto md5 = CalcMd5( Modulus );
			uint32_t hash = 0;
			for( auto b : md5 )
				hash = ( hash << 8 ) | ( uint32_t )b;
			return hash;
		}
		α PublicKey::ToBytes()ε->vector<byte>{
			return Crypto::ToBytes( *this );
		}

		α PublicKey::ExponentInt()Ι->uint32_t{
			uint32_t exponent{};
			for( let i : Exponent )
				exponent = ( exponent<<8 ) | i;
			return exponent;
		}
		α PublicKey::ModulusHex()Ε->string{
			auto modHex = Str::ToHex( Modulus );
			THROW_IF( modHex.size() > 1024, "modulus {} is too long. max length: {}", modHex.size(), 1024 );
			return modHex;
		}

	}

	α Crypto::Random( unsigned char* p, uint size )ε->void{
		THROW_IFX( ::RAND_bytes(p, (int)size)!=1, OpenSslException(Ƒ("RAND_bytes({}) failed", size)) );
	}
	α Crypto::CalcMd5( std::span<const byte> data )ε->MD5{
		if( data.empty() )
			return EmptyStringMd5;
		auto ctx = NewMDCtx();
		CALL( EVP_DigestInit_ex(ctx.get(), EVP_md5(), nullptr) );
		CALL( 	EVP_DigestUpdate(ctx.get(), data.data(), data.size()) );
		MD5 md5;
		unsigned int outputSize;
		CALL( 	EVP_DigestFinal_ex(ctx.get(), md5.data(), &outputSize) );
		return md5;
	}

	α Crypto::CreateKeyCertificate( const CryptoSettings& settings, SL sl )ε->void{
		CreateKey( settings, sl );
		IssueCertificate( settings, std::chrono::days{365}, sl );
	}

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
			let p = find_if( types, [type](sv t){ return t.size()==type.size() && Str::StartsWithInsensitive(type, t); } );
			if( p!=types.end() )
				y.push_back( Ƒ("{}:{}", *p, Str::Trim(entry.substr(colon+1))) );
		}
		return y;
	}

	//why the on-disk certificate can no longer stand for `settings`, empty if it can.  It used to stand forever: a cert
	//issued before its config gained a subjectAltName kept failing host_name_verification until an operator hand-deleted
	//the pems.  A re-issue keeps the key pair, so modulus-keyed enrollment and public-key trust survive; only anchors
	//holding the exact bytes have to re-read the file.  Configs sharing one path with differing SANs re-issue on every
	//start - the caller's log line names both sides, making that visible.
	α Crypto::ReissueReason( const Crypto::CryptoSettings& settings, SL sl )ε->string{
		if( !fs::exists(settings.Certificate.Path) )
			return "no certificate";
		let onDisk = Crypto::Certificate{ Crypto::ReadCertificate(settings.Certificate.Path, sl), sl };//unreadable throws - a damaged install, not something to paper over by minting a replacement.
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
		//read with a callback that supplies no passphrase:  an encrypted key fails on it - the case on every start once this has
		//run, so it neither throws nor logs - and a clear one never calls it.
		EVP_PKEY* clear = ::PEM_read_bio_PrivateKey( Internal::ReadFile(path, sl).get(), nullptr, [](char*, int, int, void*)->int{ return -1; }, nullptr );
		if( !clear ){
			::ERR_clear_error();
			return;
		}
		//written beside it and renamed over it:  a crash mid-write must not cost the only copy of the key the certificates were issued on.
		let temp = fs::path{ path.string()+".encrypting" };
		Internal::WritePrivateKey( temp, Internal::KeyPtr{clear, ::EVP_PKEY_free}, settings.PrivateKey.Passcode, sl );
		fs::rename( temp, path );
		INFO( "Encrypted the private key at {} with privateKey.passcode - it was written in the clear, before the passcode was set.", path.string() );
	}

	α Crypto::EnsureKeyCertificate( const CryptoSettings& settings, SL sl )ε->void{
		if( !settings.Certificate.Managed ){//the operator's pair, used as found:  the expiry and SAN checks below re-issue only what this product issued, and a certificate a CA vouched for must never be replaced by a self-signed one (web-certs3 (b)).
			THROW_IF( !fs::exists(settings.PrivateKey.Path), "certificate.managed is false and the private key '{}' does not exist - supply the pair, or set managed:true to have one issued.", settings.PrivateKey.Path.string() );
			THROW_IF( !fs::exists(settings.Certificate.Path), "certificate.managed is false and the certificate '{}' does not exist - supply the pair, or set managed:true to have one issued.", settings.Certificate.Path.string() );
			if( !fs::exists(settings.PublicKey.Path) ){//CreateKey writes it beside a key it generates; an operator's key comes without one, and the app server's identity (SetPublicKey) reads it.
				settings.CreateDirectories();
				let pKey = Internal::ReadPrivateKey( settings.PrivateKey.Path, settings.PrivateKey.Passcode, sl );
				BioPtr publicBio{ BIO_new_file(settings.PublicKey.Path.string().c_str(), "w"), ::BIO_free }; CHECK_NULL( publicBio );
				CALL( PEM_write_bio_PUBKEY(publicBio.get(), pKey.get()) );
				INFO( "Wrote the public key of '{}' to '{}'.", settings.PrivateKey.Path.string(), settings.PublicKey.Path.string() );
			}
			Certificate{ ReadCertificate(settings.Certificate.Path), sl }.Log( Ƒ("Read unmanaged certificate at {}", settings.Certificate.Path.string()), sl );
			return;
		}
		try{
			if( !fs::exists(settings.PrivateKey.Path) )
				CreateKeyCertificate( settings, sl );
			else{
				EncryptPrivateKey( settings, sl );//before a re-issue, which signs with the key as it will be read from now on
				if( let reason = ReissueReason(settings, sl); reason.size() ){
					INFO( "Re-issuing '{}': {}.", settings.Certificate.Path.string(), reason );
					IssueCertificate( settings, std::chrono::days{365}, sl );
				}
			}

			Certificate{ ReadCertificate(settings.Certificate.Path), sl }.Log( Ƒ("Read Certificate at {}", settings.Certificate.Path.string()), sl );
		}
		catch( IO::IOException& e ){
			e.PrependWhat( "Delete the file and restart to re-issue it on the existing key." );
			throw;
		}
		catch( Exception& e ){
			e.PrependWhat( Ƒ("Delete {} and restart to re-issue it on the existing key - the key is untouched, so the modulus and the enrolled identity survive.", settings.Certificate.Path.string()) );
			throw;
		}
	}

	//https://stackoverflow.com/questions/5927164/how-to-generate-rsa-private-key-using-openssl
	α Crypto::CreateKey( const CryptoSettings& settings, SL sl )ε->void{
		auto pctx = NewRsaCtx();
		uint32_t bits = 2048;
		uint32_t publicExponent = 65537;
		OSSL_PARAM params[3]{ OSSL_PARAM_construct_uint("bits", &bits), OSSL_PARAM_construct_uint("e", &publicExponent),  OSSL_PARAM_construct_end() };
		EVP_PKEY_CTX_set_params( pctx.get(), params );
		EVP_PKEY* key{};
		EVP_PKEY_generate( pctx.get(), &key ); CHECK_NULL( key );
		KeyPtr pKey( key, ::EVP_PKEY_free );
		settings.CreateDirectories();

		BioPtr publicBio{ BIO_new_file(settings.PublicKey.Path.string().c_str(), "w"), ::BIO_free }; CHECK_NULL( publicBio );
		INFO( "Created public key at {}", settings.PublicKey.Path.string() );
		CALL( PEM_write_bio_PUBKEY(publicBio.get(), pKey.get()) );
		Internal::WritePrivateKey( settings.PrivateKey.Path, move(pKey), settings.PrivateKey.Passcode );
		INFO( "Created private key at {} ({}).", settings.PrivateKey.Path.string(), settings.PrivateKey.Passcode.empty() ? "unencrypted - privateKey.passcode is empty" : "encrypted at rest" );
	}

	α Crypto::IssueCertificate( const CryptoSettings& settings, std::chrono::seconds validity, SL sl )ε->void{
		X509Ptr cert{ ::X509_new(), ::X509_free };
		auto pCert = cert.get();

		//random serial, never a constant: re-issues keep the same subject DN, and same-DN+same-serial certs are
		//indistinguishable to anything that resolves anchors by subject (X509_STORE) - RFC 5280 caps serials at 20 octets.
		unsigned char serial[16];
		Random( serial, sizeof(serial) );
		serial[0] &= 0x7f;//DER serials must be positive.
		BNPtr serialBN{ ::BN_bin2bn(serial, sizeof(serial), nullptr), ::BN_free }; CHECK_NULL( serialBN );
		CHECK_NULL( ::BN_to_ASN1_INTEGER(serialBN.get(), ::X509_get_serialNumber(pCert)) );
		::X509_set_version( pCert, 2 );//X509v3
		::X509_gmtime_adj( ::X509_get_notBefore(pCert), 0 );
		::X509_gmtime_adj( ::X509_get_notAfter(pCert), (long)validity.count() );
		let privateKey{ Internal::ReadPrivateKey(settings.PrivateKey.Path, settings.PrivateKey.Passcode, sl) };
		::X509_set_pubkey( pCert, privateKey.get() );

		auto add_x509V3ext = [&]( int nid, const char* value )ε{
			X509V3_CTX ctx;
			X509V3_set_ctx_nodb( &ctx );
			X509V3_set_ctx( &ctx, pCert, pCert, nullptr, nullptr, 0 );
			using ExtPtr = up<X509_EXTENSION, decltype( &::X509_EXTENSION_free )>;
			ExtPtr ex{ X509V3_EXT_conf_nid(nullptr, &ctx, nid, value), ::X509_EXTENSION_free }; CHECK_NULL( ex );
			X509_add_ext( pCert, ex.get(), -1 );
		};
		let& certSettings = settings.Certificate;
		if( certSettings.SubjectAltName.size() )
			add_x509V3ext( NID_subject_alt_name, string{certSettings.SubjectAltName}.c_str() );

		auto name{ ::X509_get_subject_name(pCert) };
		if( !certSettings.Country.empty() )
			::X509_NAME_add_entry_by_txt( name, "C", MBSTRING_ASC, (unsigned char*)string{certSettings.Country}.c_str(), -1, -1, 0 ); //country code
		if( !certSettings.Company.empty() )
			::X509_NAME_add_entry_by_txt( name, "O", MBSTRING_ASC, (unsigned char*)string{certSettings.Company}.c_str(), -1, -1, 0 ); //organization name
		if( !certSettings.CommonName.empty() )
			::X509_NAME_add_entry_by_txt( name, "CN", MBSTRING_ASC, (unsigned char*)string{certSettings.CommonName}.c_str(), -1, -1, 0 ); //common name
		::X509_set_issuer_name( pCert, name );
		::X509_sign( pCert, privateKey.get(), ::EVP_sha256() );

		let path = certSettings.Path;
		if( !fs::exists(path.parent_path()) )
			IO::CreateDirectories( path.parent_path() );
		BioPtr file{ BIO_new_file(path.string().c_str(), "w"), ::BIO_free }; CHECK_NULL( file );
		CALL( PEM_write_bio_X509(file.get(), pCert) );
		file = nullptr; //write
		Certificate{ ReadCertificate(path), sl }.Log( Ƒ("Issued Certificate at {}", path.string()) );
	}

	Ω toPublicKey( KeyPtr&& key, SL sl )ε->Crypto::PublicKey{
		BIGNUM* n{}, *e{};
		CALLSL( EVP_PKEY_get_bn_param(key.get(), "n", &n) );
		CALLSL( EVP_PKEY_get_bn_param(key.get(), "e", &e) );
		BNPtr pN( n, ::BN_free );
		BNPtr pE( e, ::BN_free );
		Crypto::Modulus modulus( BN_num_bytes(n) );
		BN_bn2bin( pN.get(), modulus.data() );
		Crypto::Exponent exponent( BN_num_bytes(e) );
		BN_bn2bin( pE.get(), exponent.data() );
		return { move(modulus), move(exponent) };
	}
	α Crypto::ExtractPublicKey( std::span<byte> certificate, SL sl )ε->PublicKey{
		X509Ptr cert{ ::d2i_X509_bio(ToBio(certificate, sl).get(), nullptr), ::X509_free }; CHECK_NULL( cert.get() );
		KeyPtr key{ X509_get_pubkey(cert.get()), ::EVP_PKEY_free }; CHECK_NULL( key.get() );
		return toPublicKey( move(key), sl );
	}

	Ω rsaPemFromModExp( Crypto::Modulus modulus, const Crypto::Exponent& exponent, SRCE )ε->KeyPtr;

	α Crypto::Fingerprint( const PublicKey& pubKey, SL sl )ε->MD5{
		let bytes = ToBytes( pubKey, sl );
		return CalcMd5( bytes );
	}
	α Crypto::ReadPublicKey( const fs::path& publicKey, SL sl )ε->PublicKey{
		return toPublicKey( Internal::ReadPublicKey(publicKey, sl), sl );
	}
	α Crypto::ToBytes( const PublicKey& modExp, SL sl )ε->vector<byte>{
		let key = rsaPemFromModExp( modExp.Modulus, modExp.Exponent, sl );
		auto len = i2d_PUBKEY( key.get(), nullptr );
		vector<byte> y( len );
		unsigned char* p = ( unsigned char* )y.data();
		len = i2d_PUBKEY( key.get(), &p ); THROW_IFX( len<=0, OpenSslException("i2d_PUBKEY failed") );
		return y;
	}

	α Crypto::RsaSign( str content, const fs::path& privateKeyFile, str passcode, SL sl )ε->Signature{
		array<unsigned char, SHA256_DIGEST_LENGTH> md;
		auto pDigest = SHA256( (const unsigned char*)content.data(), content.size(), md.data() ); CHECK_NULL( pDigest );
		KeyPtr pKey{ Internal::ReadPrivateKey(privateKeyFile, passcode) };
		CtxPtr ctx{ NewCtx(pKey) };
		CALL( EVP_PKEY_sign_init(ctx.get()) );
		CALL( EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PADDING) );
		CALL( EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256()) );
		uint siglen;
		CALL( EVP_PKEY_sign(ctx.get(), nullptr, &siglen, pDigest, md.size()) );
		Signature signature( siglen );
		CALL( EVP_PKEY_sign(ctx.get(), (unsigned char*)signature.data(), &siglen, pDigest, md.size()) );
		return signature;
	}

	//https://stackoverflow.com/questions/28770426/rsa-public-key-conversion-with-just-modulus
	α rsaPemFromModExp( Crypto::Modulus modulus, const Crypto::Exponent& exponent, SL sl )ε->KeyPtr{ //this changes the modulus.
		OSSL_PARAM params[]{
			OSSL_PARAM_construct_BN( "n", (unsigned char*)modulus.data(), modulus.size() ),
			OSSL_PARAM_construct_BN( "e", (unsigned char*)exponent.data(), exponent.size() ),
			OSSL_PARAM_construct_end() };

		auto pMod = ToBigNum( modulus, sl );
		auto pExp = ToBigNum( exponent, sl );//above seems to allocate space, this sets.
		//sets return size.
		OSSL_PARAM_set_BN( &params[0], pMod.get() );
		OSSL_PARAM_set_BN( &params[1], pExp.get() );

    EVP_PKEY* key{};
		auto pctx = NewRsaCtx();
		CALLSL( EVP_PKEY_fromdata_init(pctx.get()) );
		CALLSL( EVP_PKEY_fromdata(pctx.get(), &key, EVP_PKEY_PUBLIC_KEY, params) );
		return { key, ::EVP_PKEY_free };
	}

	α Crypto::Verify( const PublicKey& certificate, str decrypted, const Signature& signature, SL sl )ε->void{
		THROW_IF( certificate.Modulus.size() == 0, "certificate.Modulus.size() == 0" );
		THROW_IF( certificate.Exponent.size() == 0, "certificate.Exponent.size() == 0" );
		using ContextPtr = std::unique_ptr<EVP_MD_CTX, decltype( &::EVP_MD_CTX_free )>;
		ContextPtr ctx{ EVP_MD_CTX_create(), ::EVP_MD_CTX_free };

		let md = EVP_get_digestbyname( "SHA256" ); CHECK_NULL( md ); // do not need to be freed with EVP_MD_free
		CALL( EVP_VerifyInit_ex(ctx.get(), md, nullptr) );
		CALL( EVP_VerifyUpdate(ctx.get(), decrypted.c_str(), decrypted.size()) );
		let key = rsaPemFromModExp( certificate.Modulus, certificate.Exponent );
		let rc = EVP_VerifyFinal( ctx.get(), (const unsigned char*)signature.data(), (int)signature.size(), key.get() );
		if( rc!=1 )//0 is a semantic outcome (signature mismatch - an expected auth event), negative a malfunction.
			throw OpenSslException{ rc==0 ? string{"Signature verification failed."} : Ƒ("EVP_VerifyFinal -> {}", rc), sl };
	}

	α Crypto::FirstSkip( const fs::path& p )ι->bool{
		static std::mutex mutex;
		static flat_set<fs::path> skipped;//only ever grows, by the files an operator has put in a certificate directory - a handful.
		std::lock_guard _{ mutex };
		return skipped.emplace( p ).second;
	}

	α Crypto::ReadCertificate( const fs::path& certificate, SL sl )ε->vector<byte>{
		X509Ptr cert{ PEM_read_bio_X509(Internal::ReadFile(certificate, sl).get(), nullptr, 0, nullptr), ::X509_free };
		if( !cert ){//not PEM - try DER, which is what an OPC UA server publishes its instance certificate as (install-issues #33).
			ERR_clear_error();//the PEM attempt's "no start line" would otherwise ride along on a later, unrelated exception.
			cert = X509Ptr{ ::d2i_X509_bio(Internal::ReadFile(certificate, sl).get(), nullptr), ::X509_free };//a second open, not BIO_reset:  file BIOs invert its return.
		}
		//not CHECK_NULL:  "null returned" names no file, and a process reads several certificates - the web cert, the ua server
		//cert, one per opc slug, every trust anchor - so the path is what tells the operator which one is bad.  A *missing* file
		//already throws IOException(path); this is the parse failure of both encodings.  The openssl reason rides along from the
		//ERR queue - DER's ("wrong tag"), since PEM's was cleared - and the exception takes the rest of the queue with it
		//(CurrentErrorCode):  the second parse doubled what a bad file left behind.
		THROW_IFX( !cert, Crypto::OpenSslException(Ƒ("Could not parse certificate '{}' as PEM or DER", certificate.string()), sl) );

		auto len = i2d_X509( cert.get(), nullptr ); THROW_IFX( len<=0, OpenSslException("i2d_X509 failed") );
		vector<byte> y( len );
		unsigned char* p = ( unsigned char* )y.data();
		len = i2d_X509( cert.get(), &p ); THROW_IFX( len<=0, OpenSslException("i2d_X509 failed") );
		return y;
	}
	α Crypto::ReadPrivateKey( const Crypto::PrivateKeySettings& settings )ε->vector<byte>{
		auto pkey = Internal::ReadPrivateKey( settings.Path, settings.Passcode );
		auto len = i2d_PrivateKey( pkey.get(), nullptr ); THROW_IFX( len<=0, OpenSslException("i2d_PrivateKey failed") );
		vector<byte> y( len );
		unsigned char* pTemp = ( unsigned char* )y.data();
		len = i2d_PrivateKey( pkey.get(), &pTemp ); THROW_IFX( len<=0, OpenSslException("i2d_PrivateKey failed") );
		return y;
	}

	α Crypto::WriteCertificate( const fs::path& path, vector<byte>&& certificate, SL sl )ε->void{
		BioPtr mem{ Internal::ToBio(certificate, sl) };
		X509Ptr cert{ ::d2i_X509_bio(mem.get(), nullptr), ::X509_free };  CHECK_NULL( cert );
		BioPtr file{ Internal::File(path, true, sl) };
		CALL( ::PEM_write_bio_X509(file.get(), cert.get()) );
	}

	α Crypto::WritePrivateKey( const fs::path& path, vector<byte>&& privateKey, str passcode, SL sl )ε->void{
		auto bio = Internal::ToBio( privateKey, sl );
		KeyPtr pkey{ ::d2i_PrivateKey_bio(bio.get(), nullptr), ::EVP_PKEY_free }; CHECK_NULL( pkey );
		Internal::WritePrivateKey( path, move(pkey), passcode, sl );
	}
}