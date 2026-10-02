#pragma once
#include "Group.h"

namespace Jde::Opc::Hist{
	//The host's `hist` block.  Both hosts read the same keys; only the path's default differs, hist/opc-gateway or
	//hist/opc-server under the host's logsDir, so the host passes it.
	struct Settings{
		Settings( fs::path path )ι;
		Settings( const jobject& hist, fs::path defaultPath, SRCE )ε;
		fs::path Path;
		Duration Delay{ 1min };
		uint MaxBuffer{ 64*1024*1024 };//all groups' buffers together.
		//UTC unless an IANA name pins another, which must not change once Path holds files:  the day directories decide
		//which file a read opens.
		const std::chrono::time_zone* TimeZone;
		uint ReadLimit{ 10'000 };
	};

	//The library's root, one per host:  OpcServer adds its one group, `server`, at start; the gateway adds one per
	//hist_groups row, named by its guid, whenever one is created.
	struct Historian final : noncopyable{
		Historian( Settings settings, sp<IClock> clock )ι;
		α AddGroup( GroupConfig config, SRCE )ε->sp<Group>;
		α FindGroup( sv name )Ι->sp<Group>;
		α Config()Ι->const Settings&{ return _settings; }
		α Time()Ι->IClock&{ return *_clock; }
	private:
		sp<IClock> _clock;
		mutable absl::Mutex _mutex;
		flat_map<string,sp<Group>,std::less<>> _groups ABSL_GUARDED_BY(_mutex);
		const Settings _settings;
	};
}