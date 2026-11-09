#pragma once
#include <jde/app/client/IAppClient.h>

namespace Jde::Opc::Gateway{
	α AppClient()ι->sp<App::Client::IAppClient>;
	//Replace the default socket client before Startup captures it - OpcHub installs its in-process subclass this way.
	α SetAppClient( sp<App::Client::IAppClient> client )ι->void;

	struct GatewayAppClient : App::Client::IAppClient{
		α ClientQuery( QL::RequestQL&& q, Jde::UserPK executer, SL sl )ε->up<TAwait<jvalue>> override;
		α OnReconnected()ι->void override;//the calendars, read again on the new session.
	};
}