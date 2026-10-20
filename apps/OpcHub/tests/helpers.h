#pragma once
#include <jde/fwk/settings.h>
#include <jde/web/server/Sessions.h>

namespace Jde::Opc::Hub::Tests{
	constexpr sv Host{ "127.0.0.1" };//sessions are endpoint-bound; connect by ip so the socket's remote address matches the endpoint sessions are minted with.
	Ξ AppPort()ι->PortType{ return Settings::FindNumber<PortType>("/http/app/port").value_or(1973); }//the one listener - /http/gateway/port names it too.

	//A web session for `user` at `endpoint`, removed when this goes:  a test that stops part way leaves none in the one map,
	//where the next tests list sessions by endpoint.
	struct WebSession final{
		WebSession( Jde::UserPK user={1}, string endpoint=string{Host} )ι:
			Id{ Web::Server::Sessions::Add(user, move(endpoint), false)->SessionId },
			Authorization{ Ƒ("{:x}", Id) }
		{}
		~WebSession(){ Web::Server::Sessions::Remove( Id ); }
		WebSession( const WebSession& )=delete;
		α operator=( const WebSession& )->WebSession& =delete;
		const SessionPK Id;
		const string Authorization;//the header that names it.
	};
}