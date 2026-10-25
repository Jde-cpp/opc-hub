#include <jde/opc/server/UASettings.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/io/json.h>

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };

	//The username token's login list ([{name,password}]).  An opt-in - nothing shipped configures it (reviews/install-issues.md #1:
	//a fresh install logs in with Google, by ruling no username is seeded);  the test configs do.  Plain text, on purpose:  a hashed
	//per-user store is the hub's job.  A malformed entry is logged and skipped.
	Ω readUsers( str path )ι->vector<std::pair<string,string>>{
		vector<std::pair<string,string>> y;
		let users = Settings::FindArray( path );
		if( !users )
			return y;
		for( let& v : *users ){
			let name = v.is_object() ? Json::FindDefaultSV( v.get_object(), "name" ) : sv{};
			if( !name.empty() )
				y.emplace_back( string{name}, string{Json::FindDefaultSV(v.get_object(), "password")} );
			else
				WARN( "{}:  a user without a name is skipped.", path );
		}
		return y;
	}
	α UASettings::ReadTokens( sv root )ε->void{
		let key = [&]( sv name ){ return Ƒ( "{}/{}", root, name ); };
		Tokens.Anonymous = Settings::FindBool( key("tokenTypes/anonymous") ).value_or( Tokens.Anonymous );
		Tokens.Certificate = Settings::FindBool( key("tokenTypes/certificate") ).value_or( Tokens.Certificate );
		Tokens.Issued = Settings::FindBool( key("tokenTypes/issued") ).value_or( Tokens.Issued );
		if( Settings::FindBool(key("tokenTypes/username")).value_or(true) )
			Users = readUsers( key("users") );
		if( let uri = Settings::FindString(key("userTokenPolicyUri")); uri )
			UserTokenPolicyUri = *uri;
	}
	α UASettings::ReadEditTimeout( sv path )ε->void{
		if( let timeout = Settings::FindDuration(path); timeout ){
			THROW_IF( *timeout<=Duration::zero(), "{} must be positive, not {} ms.", path, duration_cast<milliseconds>(*timeout).count() );
			EditTimeout = *timeout;
		}
	}
	α UASettings::ReadWriteTimeout( sv path )ε->void{
		if( let timeout = Settings::FindDuration(path); timeout ){
			THROW_IF( *timeout<=Duration::zero(), "{} must be positive, not {} ms.", path, duration_cast<milliseconds>(*timeout).count() );
			WriteTimeout = *timeout;
		}
	}
}