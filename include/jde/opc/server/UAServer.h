#pragma once
#include <open62541/server.h>
#include "UAConfig.h"
#include "UAHistory.h"
#include "jde/fwk/process/process.h"
#include <thread>

namespace Jde::App::Client{ struct IAppClient; }
namespace Jde::Opc::Server {
	struct INodeAccess;
	//A UA server and its thread:  the endpoint and its policies, the trust list, the session login and node rights through
	//the host's appClient and nodeAccess, and the history backend.  The address space is the host's:  the OpcServer's comes
	//from its NodeSet2 files (LoadNodeset), added before Run.
	struct UAServer final{
		UAServer( const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess )ε;
		~UAServer();
		operator UA_Server&()ι{ ASSERT(_ua); return *_ua; }
		α Run()ε->void;

		α Namespaces()ι->flat_map<uint,string>;
		α Ptr()ι->UA_Server*{ return _ua; }
		α History()ι->UAHistory&{ return _history; }
		α Trust()ι->UATrust&{ return _config.Trust(); }
		α CustomTypes()ι->vector<UA_DataTypeArray>&{ return _customTypes; }//the host's mirrors of the nodeset types, which config.customDataTypes points into.
	private:
		UAHistory _history;//before _config, which installs its backend, and so destroyed after the server it serves.
		UAConfig _config;
		UA_Server* _ua{};
		optional<std::jthread> _thread;
		vector<UA_DataTypeArray> _customTypes;
	};
}