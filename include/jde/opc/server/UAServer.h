#pragma once
#include <open62541/server.h>
#include "Condition.h"
#include "UAConfig.h"
#include "UAHistory.h"
#include "jde/fwk/process/process.h"
#include <thread>

namespace Jde::App::Client{ struct IAppClient; }
namespace Jde::Opc::Server {
	struct INodeAccess; struct IHistoryHost;
	namespace UAAccess{ struct SessionList; }
	//A UA server and its thread:  the endpoint and its policies, the trust list, the session login and node rights through
	//the host's appClient and nodeAccess, and the history backend over its history, which may be null for a server that
	//keeps none.  The address space is the host's:  the OpcServer's comes from its NodeSet2 files (LoadNodeset), added
	//before Run.
	struct UAServer final{
		UAServer( const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess, sp<IHistoryHost> history )ε;
		~UAServer();
		operator UA_Server&()ι{ ASSERT(_ua); return *_ua; }
		α Run()ε->void;

		α Namespaces()ι->flat_map<uint,string>;
		α Sessions()ι->sp<UAAccess::SessionList>;//the sessions held, kept by the plugin:  a host reads it off this server's thread, and after the server.
		//The endpoints offered, each its security policy URI and mode, as the config holds them from construction:  no UA call, read any time.
		α Policies()Ι->vector<std::pair<string,UA_MessageSecurityMode>>;
		α Ptr()ι->UA_Server*{ return _ua; }
		α History()ι->UAHistory&{ return _history; }
		α Conditions()ι->Server::Conditions&{ return _conditions; }//the alarm conditions the host declared and added (Condition.h):  Add before Run.
		α Trust()ι->UATrust&{ return _config.Trust(); }
		α OnAsyncCancel( UAConfig::AsyncCancel f )ι->void{ _config.OnAsyncCancel( move(f) ); }//a cancelled async operation's key (UAConfig::OnAsyncCancel);  before Run.
		α CustomTypes()ι->vector<UA_DataTypeArray>&{ return _customTypes; }//the host's mirrors of the nodeset types, which config.customDataTypes points into.
		//The stop of a loop run as this one's is, UA_Server_run_iterate( server, true ) until a stop flag:  Wake from another thread
		//once the flag is set, then RunShutdown.  An idle loop's poll otherwise sleeps out UA_MAXTIMEOUT (500 ms) in each - a second a stop.
		Ω Wake( UA_Server& server )ι->void;
		Ω RunShutdown( UA_Server& server )ι->void;
	private:
		UAHistory _history;//before _config, which installs its backend, and so destroyed after the server it serves.
		UAConfig _config;
		UA_Server* _ua{};
		optional<std::jthread> _thread;
		vector<UA_DataTypeArray> _customTypes;
		Server::Conditions _conditions;//node ids alone, so its order against _ua is free.
	};
}