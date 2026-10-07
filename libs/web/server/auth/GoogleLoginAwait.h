#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/web/Jwt.h>

namespace Jde::Web::Server{
	struct GoogleLoginAwait : TAwaitEx<UserPK,TAwait<UserPK>::Task>{
		using base = TAwaitEx<UserPK,TAwait<UserPK>::Task>;
		GoogleLoginAwait( Web::Jwt jwt, SRCE ):base{sl},_jwt{ move(jwt) }{}
	private:
		α Execute()ι->TAwait<UserPK>::Task;

		Web::Jwt _jwt;
	};
}