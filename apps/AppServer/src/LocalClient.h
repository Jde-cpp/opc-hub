#pragma once
#include <jde/ql/QLAwait.h>
#include <jde/app/IApp.h>
#include "ql/AppServerQL.h"

namespace Jde::App::Server{
	struct LocalClient final : IApp{
		α InitLogging()ι->void;
		α IsLocal()Ι->bool override{ return true; }
		α QueryArray( string&& q, jobject variables, bool raw, SRCE )ι->up<TAwait<jarray>> override{ return mu<QL::QLAwait<jarray>>( move(q), variables, UserPK{UserPK::System}, QLPtr(), raw, sl ); }
		α QueryObject( string&& q, jobject variables, bool returnRaw, SRCE )ι->up<TAwait<jobject>> override{ return mu<QL::QLAwait<jobject>>( move(q), variables, UserPK{UserPK::System}, QLPtr(), returnRaw, sl ); }
		α QueryValue( string&& q, jobject variables, bool returnRaw, SRCE )ι->up<TAwait<jvalue>> override{ return mu<QL::QLAwait<jvalue>>( move(q), variables, UserPK{UserPK::System}, QLPtr(), returnRaw, sl ); }
		α SessionInfoAwait( SessionPK, SL )ι->up<TAwait<Web::FromServer::SessionInfo>> override{ return {}; }
		α PublicKey()Ι->const Crypto::PublicKey& override{ return _publicKey; }
		α SetPublicKey( Crypto::PublicKey publicKey )ι->void{ _publicKey = move(publicKey); }
		α ClientQuery( QL::RequestQL&& q, UserPK executer, SRCE )ε->up<TAwait<jvalue>> override;
		//Unreachable:  JwtLoginAwait logs a jwt in itself when IsLocal.  An answer rather than a throw, as the remote one fails.
		α Login( Web::Jwt&&, SL sl )ε->up<TAwait<Web::FromServer::SessionInfo>> override{ return mu<ExceptionAwait<Web::FromServer::SessionInfo>>( mu<Exception>(Exception{sl, ELogLevel::Critical, "The AppServer does not log in to itself."}), sl ); }
	private:
		Crypto::PublicKey _publicKey;
	};
	α AppClient()ι->sp<LocalClient>;
	α Authorizer()ι->sp<Access::Authorize>;
}