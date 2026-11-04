#pragma once

namespace Jde::Google{

	//result from https://oauth2.googleapis.com/tokeninfo?id_token=
	struct TokenInfo{
		TokenInfo()=default;
		TokenInfo( const jobject& j )ι;
		string Iss;
		string Azp;
		string Aud;
		string Email;
		bool EmailVerified{};
		string Name;
		string PictureUrl;
		string GivenName;
		string FamilyName;
		string Locale;
		time_t Iat{};
		time_t Expiration{};
	};
	inline TokenInfo::TokenInfo( const jobject& j )ι:
		Iss{ Json::FindDefaultSV(j, "iss") },
		Azp{ Json::FindDefaultSV(j, "azp") },
		Aud{ Json::FindDefaultSV(j, "aud") },
		Email{ Json::FindDefaultSV(j, "email") },
		EmailVerified{ Json::FindBool(j, "email_verified").value_or(false) },
		Name{ Json::FindDefaultSV(j, "name") },
		PictureUrl{ Json::FindDefaultSV(j, "picture") },
		GivenName{ Json::FindDefaultSV(j, "given_name") },
		FamilyName{ Json::FindDefaultSV(j, "family_name") },
		Locale{ Json::FindDefaultSV(j, "locale") },
		Iat{ Json::FindNumber<time_t>(j, "iat").value_or(0) },
		Expiration{ Json::FindNumber<time_t>(j, "exp").value_or(0) }
	{}
}