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
		//Told of each async operation open62541 cancels - a timeout, a shutdown - with the pointer that keyed it, the value a callback
		//source's write was handed:  its result is not to be set, since the memory is freed and the address may key another operation
		//next.  Called under the service lock, as the write was.  Set before Run, by the host that answers writes asynchronously.
		using AsyncCancel = function<void(const void*)>;
		α OnAsyncCancel( AsyncCancel f )ι->void{ _asyncCancel = move(f); }
	private:
		Ω AsyncCancelled( UA_Server* server, const void* out )ι->void;//open62541's asyncOperationCancelCallback, to the handler.
		α SetConfig( const UASettings& settings, ByteStringPtr&& certificate, const ByteStringPtr&& privateKey, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess )ε->void;
		α SetupSecurityPolicies( const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess, SRCE )ε->void;
		α AddSecurityPolicies( ByteStringPtr&& certificate, const ByteStringPtr&& privateKey )ε->void;
		Logger _logger;
		UATrust _trust;//outlives the server, which UA_Server_newWithConfig copies this base into:  its verify wrapper finds it.
		AsyncCancel _asyncCancel;//found through the copied base's `context`, which points here.
	};
}