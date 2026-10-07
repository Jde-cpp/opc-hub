#pragma once
#include <jde/ql/QLHook.h>
#include "exports.h"

namespace Jde::App{ struct IApp; }
namespace Jde::QL{ struct TableQL; }
namespace Jde::Web::Server{
	//A server setting a client asks for by name: restSessionTimeout, serverConnection (this connection's pk), else /http/clientSettings/<target> - null if unset.
	//The one answer behind QL's setting(target:…) and the REST /settings (where the pk is `connectionId`).
	α ServerSetting( sv target, const App::IApp& appClient )ε->jvalue;
	//setting(target:…){ target value }.  Answers without suspending.
	α SettingQL( QL::TableQL query, sp<App::IApp> appClient, SL sl )ι->up<TAwait<jvalue>>;
}