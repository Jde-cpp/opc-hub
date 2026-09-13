#pragma once
//#include "OpenSsl.h"
#include <jde/fwk/chrono.h>
#define Φ Γ auto

namespace Jde::Crypto{
	using Modulus = vector<unsigned char>;
	using Exponent = vector<unsigned char>;

	struct PrivateKeySettings{
		PrivateKeySettings( fs::path path, string passcode )ι: Path{move(path)}, Passcode{move(passcode)}{}
		PrivateKeySettings( const jobject& settings, sv defaultFileName )ι;
		fs::path Path;
		string Passcode;
	};

	struct PublicKey{
		PublicKey() = default;
		PublicKey( fs::path path, SRCE )ε;
		PublicKey( Crypto::Modulus modulus, Crypto::Exponent exponent )ε: Modulus{move(modulus)}, Exponent{move(exponent)} {}
		α operator==( const PublicKey& other )Ι->bool{ return Modulus == other.Modulus && Exponent == other.Exponent; }
		α operator<( const PublicKey& other )Ι->bool{ return Exponent == other.Exponent ? Modulus < other.Modulus : Exponent < other.Exponent; }
		Φ Hash32()Ι->uint32_t;
		Φ ExponentInt()Ι->uint32_t;
		Φ ModulusHex()Ε->string;
		α ToBytes()ε->vector<byte>;
		Crypto::Modulus Modulus;
		Crypto::Exponent Exponent;
	};

	struct Γ Certificate{
		Certificate( const jobject& settings, sv certInstance={} )ι;
		Certificate( std::span<const byte> certificate, SRCE )ε;
		α Log( string prefix, SRCE )Ι->void;
		α ToString()Ι->string;
		α SanUri()Ι->string;//the SAN's URI entry with the "URI:" prefix stripped, empty if it has none.
		//users.name = UPN → email → CN
		string CommonName; //subject CN, empty if absent. users.slug
		//file stem for the cert and both keys - settings "fileName", defaulting to CommonName.  Separate because the CN
		//is the enrollment identity and may carry $(HostName), which must not put the key pair on a moving path.
		string FileStem;
		fs::path Path;
		string Issuer;     //issuer RFC2253 one-line DN, der ctor only.
		string DistinguishedName; //subject RFC2253 one-line DN, der ctor only.  identities.subject
		//openssl config syntax - "URI:urn:x,DNS:host,IP:127.0.0.1,email:a@b" - i.e. what X509V3_EXT_conf_nid consumes,
		//NOT a DN.  Both ctors produce this form, so a cert can be parsed and re-issued without mangling the extension.
		//The settings ctor defaults an unconfigured value to "DNS:localhost,IP:127.0.0.1"; an explicit "" opts out.
		string SubjectAltName;
		string Country;			//subject C, empty if absent.
		string Company;		//subject O, empty if absent.
		string Upn;        //SAN otherName 1.3.6.1.4.1.311.20.2.3 (ms UPN), empty if absent.
		string Email;      //SAN rfc822Name, empty if absent.
		TimePoint Expiration; //notAfter.
		string Fingerprint; //sha-256 of the DER, colon-separated upper hex as `openssl x509 -fingerprint -sha256` prints it, der ctor only.  users.fingerprint
		//settings "managed", default true:  this product issues the certificate, re-issues it on expiry or SAN drift on the same
		//key, and never deletes it.  false:  the operator's pair - one a CA the browsers trust issued - used as found, never
		//issued or replaced; EnsureKeyCertificate requires both files and derives the public key file when the pair came without one.
		bool Managed{ true };
	};

	struct Γ CryptoSettings final{
		CryptoSettings( str settingsPath )ι;
		CryptoSettings( const jobject& settings, sv certInstance={} )ι;
		α CreateDirectories()Ε->void;

		struct PublicKeyPath{
			PublicKeyPath( const jobject& o, sv defaultFileName )ι;
			α Γ Value(SL sl)Ε->const struct Crypto::PublicKey&;
			fs::path Path;
		private:
			mutable optional<Crypto::PublicKey> _value;
		};

		Certificate Certificate;
		PrivateKeySettings PrivateKey;
		PublicKeyPath PublicKey;
		fs::path DhPath;
	};
}
#undef Φ