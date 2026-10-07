#pragma once
#include <jde/fwk/crypto/OpenSsl.h>
#include "client/exports.h"
#include "jde/fwk/crypto/CryptoSettings.h"

namespace Jde::Web{
	struct ΓWC Jwt{
		//how stale an exp-less token may be, measured on its iat.  10 minutes is the window the mock login has always enforced,
		//and it doubles as the clock-skew tolerance between the signer and us - there is nothing else to bound such a token by.
		static constexpr time_t MaxAgeWithoutExpiration{ 60*10 };
		Jwt( sv encoded, SRCE )ε;
		Jwt( Crypto::PublicKey publicKey, Jde::UserPK userPK, str userName, str userSlug, SessionPK sessionId, str endpoint, TimePoint expires, str description, const struct Crypto::PrivateKeySettings& privateKey, vector<byte> certificate={}, SRCE )ε;
		Jwt( Crypto::PublicKey publicKey, Jde::UserPK userPK, str userName, str userSlug )ι: PublicKey{move(publicKey)}, UserPK{userPK}, UserName{userName}, UserSlug{userSlug}{}
		α Payload()Ι->string;
		α Aud()Ε->string{ return Json::AsString( Body, "aud" ); }
		α Iss()Ι->sv{ return Json::FindDefaultSV( Body, "iss" ); }
		α Expires()Ι->TimePoint{ auto exp = Json::FindNumber<time_t>( Body, "exp" ); return Clock::from_time_t( exp ? *exp : Iat+MaxAgeWithoutExpiration ); }
		string Kid;
		jobject Body;
		string HeaderBodyEncoded;
		Crypto::Signature Signature;
		Crypto::PublicKey PublicKey;
		vector<byte> Certificate;//der - travels in the signed body ("x5c") for enrollment chain-verify. When present it is the single source of key material: PublicKey derives from it and n/e are omitted from the wire.
		string Host;
		time_t Iat{};
		string SessionId;
		Jde::UserPK UserPK;
		string UserName;
		string UserSlug;
		string Description;
		α SetModulus( str encoded )ε->void;
		α SetExponent( str encoded )ε->void;
	};
}