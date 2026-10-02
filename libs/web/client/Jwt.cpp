#include "jde/fwk/crypto/CryptoSettings.h"
#include "jde/fwk/exceptions/Exception.h"
#include <jde/web/Jwt.h>
#include <boost/uuid/uuid_io.hpp>
#include <jde/fwk/io/json.h>
#include <jde/fwk/str.h>

#define let const auto

namespace Jde::Web{
	Jwt::Jwt( Crypto::PublicKey key, Jde::UserPK userPK, str userName, str userSlug, SessionPK sessionId, str endpoint, TimePoint expires, str description, const struct Crypto::PrivateKeySettings& privateKey, vector<byte> certificate, SL sl )ε:
		PublicKey{ certificate.size() ? Crypto::ExtractPublicKey(certificate, sl) : move(key) }, Host{ endpoint }, Iat{ time(nullptr) }, UserPK{ userPK }, UserName{ userName }, UserSlug{ userSlug }, Description{ description }{
		Body = jobject{
			{ "iat", Iat },
			{ "host", Host },
			{ "sub", UserPK.Value },
			{ "name", userName },
			{ "slug", userSlug },
		};
		if( certificate.size() ){ //the cert is the single source of key material - n/e derive from it on parse.
			Body["x5c"] = Str::Encode64( certificate, true );
			Certificate = move( certificate );
		}
		else{
			Body["n"] = Str::Encode64( PublicKey.Modulus, true );
			Body["e"] = Str::Encode64( PublicKey.Exponent, true );
		}
		if( sessionId )
			Body["sid"] = hex( sessionId );
		if( description.size() )
			Body["description"] = Description;
		if( expires!=TimePoint::min() )
			Body["exp"] = Clock::to_time_t( expires );

		let head = jobject{ {"alg","RS256"}, {"typ","JWT"} };
		HeaderBodyEncoded = Str::Encode64( serialize(head), true )+ "." + Str::Encode64( serialize(Body), true );
		Signature = Crypto::RsaSign( HeaderBodyEncoded, privateKey.Path, privateKey.Passcode, sl );
	}
	Jwt::Jwt( sv encoded, SL sl )ε{
		let fpIndex = encoded.find_last_of( '.' );
		let bodyIndex = encoded.find_first_of( '.' );
		if( fpIndex==string::npos || fpIndex+1==encoded.size() || bodyIndex==string::npos || fpIndex==bodyIndex )
			THROW( "Invalid jwt.  Expected 3 parts." );
		HeaderBodyEncoded = encoded.substr( 0, fpIndex );
		let headerEncoded = HeaderBodyEncoded.substr( 0, bodyIndex );
		let header = Json::Parse( Str::Decode64(headerEncoded, true) );//{ "alg":"RS256","kid":"fed80fec56db99233d4b4f60fbafdbaeb9186c73","typ":"JWT" }
		if( auto alg = Json::AsSV(header, "alg"); alg!="RS256" )
			THROW( "Invalid jwt.  Expected alg=RS256, found '{}'.", alg );
		if( auto type = Json::AsSV(header, "typ"); type!="JWT" )
			THROW( "Invalid jwt.  Expected typ=JWT, found '{}'.", type );
		Kid = Json::FindSV( header, "kid" ).value_or( "" );
		Signature = Str::Decode64<Crypto::Signature>( encoded.substr(fpIndex+1), true );

		auto body = Str::Decode64( HeaderBodyEncoded.substr(bodyIndex+1), true );
		Body = Json::Parse( body );
		optional<Crypto::MD5> fpKey;
		if( auto x5c = Json::FindString(Body, "x5c"); x5c ){
			let der = Str::Decode64<string>( *x5c, true );
			Certificate.assign( (const byte*)der.data(), (const byte*)der.data()+der.size() );
			PublicKey = Crypto::ExtractPublicKey( Certificate, sl );
			fpKey = Crypto::Fingerprint( PublicKey );
		}
		else if( auto modulus = Json::FindString(Body, "n"); modulus ){ //certless jwts - session reissue, google jwks keys.
			SetModulus( move(*modulus) );
			SetExponent( Json::AsString(Body, "e") );
			fpKey = Crypto::Fingerprint( PublicKey );
		}
		Iat = Json::AsNumber<time_t>( Body, "iat" );
		//a jwt has to be time-bounded.  the ones minted for certificate login carry no exp (App::Client::getJwt) and were accepted
		//forever; bound those by iat instead - they are posted to /login the moment they are signed, so a stale one is a replay.
		//tokens that do carry an exp are left to it: a google id token or an AppServer-issued session token is legitimately hours
		//old by the time it is re-presented, which is why the same window cannot be applied to the login path as a whole.
		//two independent tests, not if/else: THROW_IF expands to a bare `if`, so an `else` here would bind to its inner one and
		//run the iat check on every unexpired token that does have an exp.
		let now = time( nullptr );
		let exp = Json::FindNumber<time_t>( Body, "exp" );
		THROW_IFX( exp && *exp<now, Exception(sl, {EHttpStatus::Unauthorized}, "Invalid jwt.  Expired at '{}'.", ToIsoString(Clock::from_time_t(*exp))) );
		THROW_IFX( !exp && std::abs(now-Iat)>MaxAgeWithoutExpiration, Exception(sl, {EHttpStatus::Unauthorized}, "Invalid jwt.  No 'exp' claim and 'iat' '{}' is not within {}s of '{}'.", Iat, MaxAgeWithoutExpiration, ToIsoString(Clock::from_time_t(now))) );
		UserPK = { Json::FindNumber<UserPK::Type>(Body, "sub").value_or(0) };
		UserName = Json::FindString( Body, "name" ).value_or( fpKey ? Str::ToHex(*fpKey) : "" );
		UserSlug = Json::FindString( Body, "slug" ).value_or( UserName );
		Host = Json::FindString( Body, "host" ).value_or( "" );
		SessionId = Json::FindDefaultSV( Body, "sid" );

		//no fallback to the key fingerprint:  enrollment writes this straight to users.description, and the modulus/exponent it
		//would have restated are stored on the same row.  A client that says nothing about itself enrolls with an empty
		//description, which an admin can fill in - App::Client::getJwt sends the program, instance and host.
		Description = Json::FindSV( Body, "description" ).value_or( "" );
	}
	α Jwt::Payload()Ι->string{
		auto signature = Str::Encode64( Signature, true );
		auto payload = Ƒ( "{}.{}", HeaderBodyEncoded, signature );
		return payload;
	}
	α Jwt::SetModulus( str encoded )ε->void{
		PublicKey.Modulus = Str::Decode64<Crypto::Modulus>( encoded, true );
	}
	α Jwt::SetExponent( str encoded )ε->void{
		PublicKey.Exponent = Str::Decode64<Crypto::Exponent>( encoded, true );
	}
}