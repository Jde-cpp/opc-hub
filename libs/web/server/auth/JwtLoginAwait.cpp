#include <jde/web/server/auth/JwtLoginAwait.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/access/server/awaits/LoginAwait.h>
#include "GoogleLoginAwait.h"

#define let const auto
namespace Jde::Web::Server{
	α JwtLoginAwait::Execute()ι->TAwait<UserPK>::Task{
		try{
			UserPK userPK{};
			if( !_appClient->IsLocal() )
				userPK = { (co_await Any(_appClient->Login(move(_jwt), _sl))).user_pk() };
			else if( _jwt.Iss()=="https://accounts.google.com" )
				userPK = co_await GoogleLoginAwait( move(_jwt) );
			else{
				Crypto::Verify( _jwt.PublicKey, _jwt.HeaderBodyEncoded, _jwt.Signature );
				userPK = co_await Access::Server::LoginAwait( move(_jwt.PublicKey), move(_jwt.Certificate), move(_jwt.Description), {} );//name/slug derive from the certificate at enrollment.
			}
			ResumeScaler( userPK );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}