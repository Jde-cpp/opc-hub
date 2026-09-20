#pragma once
#ifndef OPEN_SSL_H
#define OPEN_SSL_H
//#include <span>
//#include <boost/uuid/uuid.hpp>
#include "jde/fwk/log/logTags.h"
#include <jde/fwk/chrono.h>
#include "CryptoSettings.h"

#define Φ Γ auto

namespace Jde::Crypto{
	struct CryptoSettings;

	using Signature = vector<unsigned char>;
	using MD5 = boost::uuids::uuid;
	Φ CalcMd5( std::span<const byte> data )ε->MD5;
	Ŧ CalcMd5( const T& content )ε->MD5 requires requires{ content.data(); content.size(); }{ return CalcMd5( std::as_bytes(std::span{content.data(), content.size()}) ); }//by reference - the old by-value form copied every Modulus it hashed.
	Φ Random( unsigned char* p, uint size )ε->void;//throws on entropy failure.
	Ŧ Random()ε->T{ T y{}; Random( (unsigned char*)&y, sizeof(T) ); return y; }
	Φ CreateKey( const CryptoSettings& settings, SL sl )ε->void;
	//validity is a test seam:  production always issues for a year, and the expiry branch of ReissueReason is otherwise untestable (nothing else mints an expired certificate).
	Φ IssueCertificate( const CryptoSettings& settings, std::chrono::seconds validity = std::chrono::days{365}, SRCE )ε->void;
	//why the certificate on disk can no longer stand for `settings` - missing, expired or expiring within a day, or a SAN drifted from the configured one - empty if it can.  One predicate for every issuer (EnsureKeyCertificate, the gateway's per-target EnsureCertificate), so the two cannot drift apart again (web-certs3 #17).  An unreadable certificate throws (#3(b)).
	Φ ReissueReason( const CryptoSettings& settings, SRCE )ε->string;
	Φ CreateKeyCertificate( const CryptoSettings& settings, SRCE )ε->void;
	Φ EnsureKeyCertificate( const CryptoSettings& settings, SRCE )ε->void;
	Φ ExtractPublicKey( std::span<byte> certificate, SL sl )ε->PublicKey;
	Φ Fingerprint( const PublicKey& key, SRCE )ε->MD5;
	Φ ReadPublicKey( const fs::path& publicKey, SRCE )ε->PublicKey;
	Φ ToBytes( const PublicKey& key, SRCE )ε->vector<byte>;
	Φ ReadCertificate( const fs::path& certificate, SRCE )ε->vector<byte>;//PEM or DER - see IsCertificateFile.
	//What a file dropped in a certificate directory may be called - an operator's drop-dir is scanned, not a configured path,
	//so something has to keep the .crl/.key/README out of the parser.  `.der`/`.cer` are the encoding a third party publishes:
	//an OPC UA server writes DER (Kepware's kepserverex_ua_server.der) and a UA trust list is a directory of .der by
	//convention, and skipping those in silence was install-issues #33 - the operator copied in the file the server actually
	//produces and nothing changed.  One list for every such scan (ServerTrust, UATrust, the enrollment anchors) so they cannot
	//drift, and one sentence to quote back when a scan found nothing.
	constexpr sv CertificateExtensions{ ".pem, .crt, .der and .cer" };
	Ξ IsCertificateFile( const fs::path& p )ι->bool{ const auto ext = p.extension(); return ext==".pem" || ext==".crt" || ext==".der" || ext==".cer"; }
	Φ ReadPrivateKey( const PrivateKeySettings& settings )ε->vector<byte>;
	Φ RsaSign( str content, const fs::path& privateKeyFile, str passcode={}, SRCE )ε->Signature;
	Φ Verify( const PublicKey& certificate, str decrypted, const Signature& signature, SRCE )ε->void;
	Φ WriteCertificate( const fs::path& path, vector<byte>&& certificate, SL sl )ε->void;
	Φ WritePrivateKey( const fs::path& path, vector<byte>&& privateKey, str passcode, SL sl )ε->void;

	struct OpenSslException final : ExternalException{
		OpenSslException( string m, SRCE )ι:
			OpenSslException{ move(m), CurrentErrorCode(), sl }
		{}
		OpenSslException( string m, uint32 rc, SRCE )ι:
			OpenSslException{ CurrentError(rc), move(m), {ELogLevel::Warning, ELogTags::Crypto, rc}, sl }
		{}
		OpenSslException( string externalMessage, string description, ExceptionArgs args, SRCE )ι:
			ExternalException{ move(externalMessage), move(description), args, sl }
		{}
		static Φ CurrentError()ι->string;
		static Φ CurrentError( uint32 rc )ι->string;
		static Φ CurrentErrorCode()ι->uint32;
		α Move()ι->up<Exception> override{ return mu<OpenSslException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }
  };
}
#undef Φ
#endif