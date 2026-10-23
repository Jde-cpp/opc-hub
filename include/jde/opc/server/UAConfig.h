#pragma once
#include <open62541/server.h>
#include "jde/fwk/usings.h"
#include <jde/opc/uatypes/Logger.h>
#include "UASettings.h"
#include "UATrust.h"

namespace Jde::App::Client{ struct IAppClient; }
namespace Jde::Opc::Server {
	struct INodeAccess; struct UAHistory;
	struct UAConfig final : UA_ServerConfig {
		//history:  the server's historian, whose backend is installed when it has one.
		UAConfig( const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess, UAHistory* history=nullptr )ε;
		α Trust()ι->UATrust&{ return _trust; }
	private:
		α SetConfig( const UASettings& settings, ByteStringPtr&& certificate, const ByteStringPtr&& privateKey, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess )ε->void;
		α SetupSecurityPolicies( const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess, SRCE )ε->void;
		α AddSecurityPolicies( ByteStringPtr&& certificate, const ByteStringPtr&& privateKey )ε->void;
		Logger _logger;
		UATrust _trust;//outlives the server, which UA_Server_newWithConfig copies this base into:  its verify wrapper finds it.
	};
}