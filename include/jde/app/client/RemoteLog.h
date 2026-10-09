#pragma once
#include <jde/app/log/BufferedLogger.h>

namespace Jde::App::Client{
	struct IAppClient;
	struct RemoteLog final : BufferedLogger{
		RemoteLog( const jobject& settings, sp<IAppClient> client )ι;
		~RemoteLog();
		Ω Init( sp<IAppClient> client )ι->void;
		α Shutdown( bool terminate=false, SRCE )ι->void override;
		α Write( const Logging::Entry& m )ι->void override;
		α Write( const Logging::Entry& m, uint32 /*appPK*/, uint32 /*instancePK*/ )ι->void override{ Write(m); }
		α Name()Ι->sv override{ return "RemoteLog"; }
	private:
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Drop()ι->uint override;
		ABSL_UNLOCK_FUNCTION(_mutex) α Flush()ι->void override{ Send(); }
		// Shutdown has to: the executor is being torn down, so a posted lambda can simply never run
		ABSL_UNLOCK_FUNCTION(_mutex) α Send( bool post=true )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Size()Ι->uint override{ return _entries.size(); }
		sp<IAppClient> _client ABSL_GUARDED_BY(_mutex);
		uint32 _maxBatch;
		//Entries go in whenever the process logs and only come out when the app server is reachable
		vector<Logging::Entry> _entries ABSL_GUARDED_BY(_mutex);
	};
}