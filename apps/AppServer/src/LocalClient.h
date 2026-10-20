#pragma once
#include <jde/ql/QLAwait.h>
#include <jde/app/IApp.h>
#include "ql/AppServerQL.h"

namespace Jde::App::Server{
	struct LocalClient final : IApp{
		α InitLogging()ι->void;
		α IsLocal()Ι->bool override{ return true; }
		α QueryArray( string&& q, jobject variables, bool raw, SRCE )ι->up<TAwait<jarray>> override{ return query<jarray>( move(q), move(variables), raw, sl ); }
		α QueryObject( string&& q, jobject variables, bool returnRaw, SRCE )ι->up<TAwait<jobject>> override{ return query<jobject>( move(q), move(variables), returnRaw, sl ); }
		α QueryValue( string&& q, jobject variables, bool returnRaw, SRCE )ι->up<TAwait<jvalue>> override{ return query<jvalue>( move(q), move(variables), returnRaw, sl ); }
		α PublicKey()Ι->const Crypto::PublicKey& override{ return _publicKey; }
		α SetPublicKey( Crypto::PublicKey publicKey )ι->void{ _publicKey = move(publicKey); }
		//Unreachable:  JwtLoginAwait logs a jwt in itself when IsLocal.  An answer rather than a throw, as the remote one fails.
		α Login( Web::Jwt&&, SL sl )ε->up<TAwait<Web::FromServer::SessionInfo>> override{ return mu<ExceptionAwait<Web::FromServer::SessionInfo>>( mu<Exception>(Exception{sl, ELogLevel::Critical, "The AppServer does not log in to itself."}), sl ); }
	private:
		Ŧ query( string&& q, jobject&& variables, bool raw, SL sl )ι->up<TAwait<T>>{ return mu<QL::QLAwait<T>>( move(q), move(variables), UserPK{UserPK::System}, QLPtr(), raw, sl ); }//this process asking itself:  as System.
		Crypto::PublicKey _publicKey;
	};
	α AppClient()ι->sp<LocalClient>;
	α Authorizer()ι->sp<Access::Authorize>;
}