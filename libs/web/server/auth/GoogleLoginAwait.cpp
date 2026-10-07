#include "GoogleLoginAwait.h"
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/io/json.h>
#include <jde/fwk/str.h>
#include <jde/access/server/accessServer.h>
#include <jde/access/types/GoogleTokenInfo.h>
#include <jde/web/client/http/ClientHttpAwait.h>

#define let const auto
namespace Jde::Web::Server{
	using Web::Client::ClientHttpAwait;
	sp<const jobject> _jwks; mutex _jwksMutex;//Google rotates signing keys - refetch when a kid is missing.
	Ω cachedJwks()ι->sp<const jobject>{ lg _{_jwksMutex}; return _jwks; }
	Ω setJwks( sp<const jobject> jwks )ι->void{ lg _{_jwksMutex}; _jwks = move(jwks); }
	Ω findKey( const jobject& jwks, str kid )ε->jobject{
		jobject y;
		for( let& key : Json::AsArray(jwks, "keys") ){
			if( Json::AsString(Json::AsObject(key), "kid")==kid )
				y = Json::AsObject( key );
		}
		return y;
	}

	α GoogleLoginAwait::Execute()ι->TAwait<UserPK>::Task{
		try{
			THROW_IF( _jwt.Aud() != Settings::FindSV("/http/clientSettings/googleAuthClientId").value_or(""), "Invalid client id: '{}'", _jwt.Aud() );

			let& kid = _jwt.Kid;
			THROW_IF( kid.empty(), "Could not find kid in header {}", Str::Decode64<string>(_jwt.HeaderBodyEncoded) );
			jobject foundKey;
			if( let jwks = cachedJwks(); jwks )
				foundKey = findKey( *jwks, kid );
			if( foundKey.empty() ){
				auto jwks = ms<const jobject>( (co_await Any(ClientHttpAwait{
					"www.googleapis.com",
					string{"/oauth2/v3/certs"},
					443,
					{.ContentType="", .Verb=http::verb::get}})).Json() );
				setJwks( jwks );
				foundKey = findKey( *jwks, kid );
				THROW_IF( foundKey.empty(), "Could not find key... '{}' in: '{}'", kid, serialize(*jwks) );
			}
			_jwt.SetModulus( Json::AsString( foundKey, "n") );
			_jwt.SetExponent( Json::AsString( foundKey, "e") );
			Crypto::Verify( _jwt.PublicKey, _jwt.HeaderBodyEncoded, _jwt.Signature );
			Google::TokenInfo token{ _jwt.Body };
			let expiration = Clock::from_time_t( token.Expiration );
			THROW_IF( !_debug && expiration<Clock::now(), "Token expired: '{}'.", ToIsoString(expiration) );
			ResumeScaler( co_await Access::Server::Authenticate(token.Email, underlying(Access::EProviderType::Google)) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}