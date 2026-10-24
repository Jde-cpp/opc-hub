#pragma once
#include <span>
#include <jde/fwk/chrono.h>
#define Φ Γ auto

namespace Jde::Crypto{
	//the URI entry of a SAN in openssl config syntax ("URI:urn:x,DNS:host"), its "URI:" prefix stripped - empty if it has none.  The
	//one entry:  replacing "URI:" across the whole string would hand the caller "urn:x,DNS:host".  A parsed certificate's and a configured block's.
	Φ SanUri( sv subjectAltName )ι->string;

	//What an X.509 certificate says, parsed from its DER.  The certificate's own identity for enrollment (users.name = UPN →
	//email → CN, users.slug = CN, identities.subject, users.fingerprint) and what ReissueReason holds against the configured block.
	struct Γ Certificate final{
		Certificate( std::span<const byte> der, SRCE )ε;
		α Log( string prefix, SRCE )Ι->void;
		α ToString()Ι->string;
		α SanUri()Ι->string{ return Crypto::SanUri( SubjectAltName ); }
		string CommonName;        //subject CN, empty if absent.
		string Issuer;            //issuer RFC2253 one-line DN.
		string DistinguishedName; //subject RFC2253 one-line DN.
		//openssl config syntax - "URI:urn:x,DNS:host,IP:127.0.0.1,email:a@b" - i.e. what X509V3_EXT_conf_nid consumes, NOT a DN,
		//and the form CertificateSettings::SubjectAltName takes, so a parsed certificate can be re-issued without mangling the extension.
		string SubjectAltName;
		string Upn;               //SAN otherName 1.3.6.1.4.1.311.20.2.3 (ms UPN), empty if absent.
		string Email;             //SAN rfc822Name, empty if absent.
		TimePoint Expiration;     //notAfter.
		string Fingerprint;       //sha-256 of the DER, colon-separated upper hex as `openssl x509 -fingerprint -sha256` prints it.
	};
	Φ ReadCertificate( const fs::path& certificate, SRCE )ε->vector<byte>;//PEM or DER on disk - see TrustDirs' IsCertificateFile - DER back.
	Φ WriteCertificate( const fs::path& path, std::span<const byte> certificate, SRCE )ε->void;//DER, the first certificate of `certificate` - a UA trust list's encoding.
}
#undef Φ