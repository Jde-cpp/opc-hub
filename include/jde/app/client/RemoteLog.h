#pragma once
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/log/ILogger.h>
#include "../log/ProtoLog.h"

namespace Jde::App::Client{
	struct IAppClient;
	struct RemoteLog final : Logging::ILogger, noncopyable{
		RemoteLog( const jobject& settings, sp<IAppClient> client )ι;
		~RemoteLog();
		Ω Init( sp<IAppClient> client )ι->void;
		α Shutdown( bool terminate=false, SRCE )ι->void override;
		α Write( const Logging::Entry& m )ι->void override;
		α Write( const Logging::Entry& m, uint32 /*appPK*/, uint32 /*instancePK*/ )ι->void override{ Write(m); }
		α Name()Ι->sv override{ return "RemoteLog"; }
	private:
		// Shutdown has to: the executor is being torn down, so a posted lambda can simply never run
		α Send( bool post=true )ι->void;
		α Start( sp<IAppClient> client )ι->void;
		ABSL_UNLOCK_FUNCTION(_mutex) α StartTimer()ι->TimerAwait::Task;
		sp<IAppClient> _client;
		Duration _delay;
		//Entries go in whenever the process logs and only come out when the app server is reachable
		uint32 _maxEntries;
		uint32 _maxBatch;
		uint _dropped{};
		absl::Mutex _mutex;
		bool _running{};
		static constexpr ELogTags _tags{ ELogTags::ExternalLogger };
		up<DurationTimer> _timer;
		vector<Logging::Entry> _entries;
	};
}