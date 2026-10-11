#pragma once
#include <jde/fwk/crypto/OpenSsl.h>

namespace Jde::Opc::Gateway{
	struct User{
		α operator==( const User& other )Ι{ return LoginName == other.LoginName && Password == other.Password; }
		α operator<( const User& other )Ι{ return LoginName == other.LoginName ? Password < other.Password : LoginName < other.LoginName; }
		string LoginName;
		string Password;
	};
	using Token = string;
	enum class ETokenType : uint8{
		None=0,
		Anonymous=1,
		Username=2,
		Certificate=4,
		IssuedToken=8
	};
	//bit-indexed for FromEnumFlag: "None" first, then one name per bit - never FromEnum, which would index it by the flag value.
	constexpr static const array<sv,5> TokenTypeNames = { "None", "Anonymous", "Username", "Certificate", "IssuedToken" };
	//not an overload of ToString: this namespace would then hide Opc::ToString( UA_String ) from every unqualified call.
	Ξ TokenTypeName( ETokenType x )ι->string{ return FromEnumFlag<ETokenType>( TokenTypeNames, x ); }
	α ToTokenType( UA_UserTokenType ua )ι->ETokenType;
	struct Credential{
		Credential()ι{}
		Credential( User user )ι:_value{move(user)}{}
		Credential( Token token )ι:_value{move(token)}{}
		Credential( Crypto::PublicKey key )ι:_value{move(key)}{}
		α operator==( const Credential& other )Ι->bool;

		α Token()Ι->const Gateway::Token&{ return get<Gateway::Token>( _value ); }
		α Type()Ι->ETokenType;
		α UserPK()Ι->Jde::UserPK{ return _userPK; }
		α SetUserPK( Jde::UserPK userPK )ι->void{ _userPK = userPK; }
		α IsUser()Ι{ return Type()==ETokenType::Username; }
		α LoginName()Ι->str;
		α Password()Ι->str;
		α ToString()Ι->string;
		α operator<( const Credential& other )Ι->bool;
		//The historian's collector logs in with its own credential per connection (spec *Collector contract*) and must get its own
		//client:  its subscriptions, items and their parameters are never a screen's, even when a browser presents the same token.
		//UAClient::GetClient keys by (connection, credential), so the key carries it - a collector credential equals no session's,
		//and a session's connect never finds the collector's client.  Nothing a web session builds sets it.
		Ω Collector( Credential cred )ι->Credential{ cred._collector = true; return cred; }
		α IsCollector()Ι->bool{ return _collector; }
	private:
		variant<nullptr_t, Gateway::Token, User, Crypto::PublicKey> _value;
		mutable string _display;
		Jde::UserPK _userPK{};
		bool _collector{};
	};
	α AddSession( SessionPK sessionId, ServerCnnctnNK opcNK, Credential credential )ι->void;
	α AuthCache( const Credential& credential, const ServerCnnctnNK& opcNK, SessionPK sessionId )ι->optional<bool>;
	α Logout( SessionPK sessionId )ι->void;
	α GetCredential( SessionPK sessionId, str opcId )ι->optional<Credential>;
	struct SessionCount{ ServerCnnctnNK Connection; ETokenType Type; Jde::UserPK UserPK; uint32 Count; };
	//one row per distinct (Connection,Type,UserPK); Count = web sessions holding that credential on that connection.
	α SessionCounts()ι->vector<SessionCount>;
}