#include "PubSubReader.h"
#include <jde/opc/server/UAServer.h>

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::PubSub };
	Ω resolved( PubSub::Config&& config, UAServer& ua, SL sl )ε->PubSub::Config{
		config.Resolve( ua, sl );
		for( let& f : config.Fields )
			INFO( "PubSub '{}' field '{}' ({}) -> {}", config.DataSetName, f.Name, f.Type->typeName, f.Node.ToString() );
		return move( config );
	}
	PubSubReader::PubSubReader( UAServer& ua, const jobject& settings, SL sl )ε:
		_config{ resolved(PubSub::Config{settings, sl}, ua, sl) },
		_reader{ ua, _config, sl }
	{}
}