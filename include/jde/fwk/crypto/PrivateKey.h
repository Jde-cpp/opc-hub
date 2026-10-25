#pragma once
#include <span>
#include "PublicKey.h"

typedef struct evp_pkey_st EVP_PKEY;

namespace Jde::Crypto{
	//An RSA private key held in memory - read from a PEM, encrypted with a passcode or not, or from a DER - that signs, writes
	//itself out, or hands over its encodings.  Loading is the expensive step (a PEM decrypt), so a caller that signs repeatedly keeps one.
	struct Γ PrivateKey final{
		//the throw names the file and says which of the two passcode faults it is - none configured for an encrypted key, or the wrong one.
		PrivateKey( const fs::path& pem, str passcode={}, SRCE )ε;
		PrivateKey( std::span<const byte> der, SRCE )ε;
		static α Generate( SRCE )ε->PrivateKey;//RSA 2048, e=65537.
		//the PEM only if it is unencrypted - nullopt for an encrypted one, with nothing left on the error queue:  EncryptPrivateKey's probe.
		static α ReadUnencrypted( const fs::path& pem, SRCE )ε->optional<PrivateKey>;
		α Sign( sv content, SRCE )Ε->Signature;//sha-256, PKCS#1 v1.5 - what Verify checks.
		α Der( SRCE )Ε->vector<byte>;//PKCS#8 - the form a UA client config takes.
		α Write( const fs::path& pem, str passcode, SRCE )Ε->void;//aes-256-cbc under `passcode`, cleartext without one.
		α WritePublic( const fs::path& pem, SRCE )Ε->void;//the public half, PEM PUBKEY.
		α Native()Ι->EVP_PKEY*{ return _key.get(); }
	private:
		PrivateKey( EVP_PKEY* key )ι;
		up<EVP_PKEY, void(*)(EVP_PKEY*)> _key;
	};
}