#pragma once
#include <atomic>
#include "UAConfig.h"
#include "hist/UAHistory.h"
#include "jde/fwk/process/process.h"
#include <thread>

namespace Jde::Opc::Server {
	//The address space comes from the NodeSet2 files at /opcServer/configFiles (Load), not from the database - the
	//PK-keyed node model this used to hold (objects/objectTypes/variables/refs/browseNames/constructors, and the
	//Add*/Get* api over them) went with the opc tables.
	struct UAServer final{
		UAServer()ε;
		~UAServer();
		operator UA_Server&()ι{ ASSERT(_ua); return *_ua; }
		α Run()ε->void;

		α Load( fs::path configFile, SRCE )ε->void;
		α Namespaces()ι->flat_map<uint,string>;
		α Ptr()ι->UA_Server*{ return _ua; }
		α History()ι->UAHistory&{ return _history; }
		string ServerName;
	private:
		α PublishDataTypes()ι->void;
		UAHistory _history;//before _config, which installs its backend, and so destroyed after the server it serves.
		UAConfig _config{ &_history };
		UA_Server* _ua{};
		optional<std::jthread> _thread;
		std::atomic<UA_Boolean> _running{};
		vector<UA_DataTypeArray> _customTypes;
	};
}
