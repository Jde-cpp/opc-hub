#pragma once
#include "Certificate.h"
#include "PublicKey.h"
#define Φ Γ auto

namespace Jde::Crypto{
	//An `ssl` settings block:  where a key pair and its certificate live and what the certificate is issued as.  Nothing here
	//touches the files - issue.h does that from one of these.
	struct Γ PrivateKeySettings{
		PrivateKeySettings( fs::path path, string passcode )ι: Path{move(path)}, Passcode{move(passcode)}{}
		PrivateKeySettings( const jobject& settings, sv defaultFileName )ι;
		fs::path Path;
		string Passcode;
	};

	struct Γ CertificateSettings{
		CertificateSettings( const jobject& settings, sv certInstance={} )ι;
		α SanUri()Ι->string{ return Crypto::SanUri( SubjectAltName ); }//the configured applicationUri, for a UA endpoint.
		//users.name = UPN → email → CN
		string CommonName; //subject CN. users.slug
		//file stem for the cert and both keys - settings "fileName", defaulting to CommonName.  Separate because the CN
		//is the enrollment identity and may carry $(HostName), which must not put the key pair on a moving path.
		string FileStem;
		fs::path Path;
		//openssl config syntax - "URI:urn:x,DNS:host,IP:127.0.0.1,email:a@b" - what X509V3_EXT_conf_nid consumes, NOT a DN, and the
		//form Certificate::SubjectAltName reads back, so ReissueReason can compare the two.
		//An unconfigured value defaults to "DNS:localhost,IP:127.0.0.1"; an explicit "" opts out.
		string SubjectAltName;
		string Country;    //subject C, empty if absent.
		string Company;    //subject O, empty if absent.
		//settings "managed", default true:  this product issues the certificate, re-issues it on expiry or SAN drift on the same
		//key, and never deletes it.  false:  the operator's pair - one a CA the browsers trust issued - used as found, never
		//issued or replaced; EnsureKeyCertificate requires both files and derives the public key file when the pair came without one.
		bool Managed{ true };
	};

	struct Γ CryptoSettings final{
		CryptoSettings( str settingsPath )ι;
		CryptoSettings( const jobject& settings, sv certInstance={} )ι;
		α CreateDirectories()Ε->void;

		struct Γ PublicKeyPath{
			PublicKeyPath( const jobject& o, sv defaultFileName )ι;
			α Read( SRCE )Ε->Crypto::PublicKey;//a missing file throws IOException - a default-constructed key would compare equal to every other empty one (fwk-max #12).
			fs::path Path;
		};

		CertificateSettings Certificate;
		PrivateKeySettings PrivateKey;
		PublicKeyPath PublicKey;
		fs::path DhPath;
	};
	Φ ReadPrivateKey( const PrivateKeySettings& settings, SRCE )ε->vector<byte>;//the configured key's DER - PrivateKey{Path, Passcode}.Der().
}
#undef Φ