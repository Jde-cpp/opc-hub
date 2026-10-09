#pragma once
#include <chrono>
#include <absl/synchronization/mutex.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/co/LockKey.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/log/ILogger.h>
#include <jde/app/proto/Log.pb.h>
#include <jde/app/usings.h>

namespace Jde::App{
	//L2: was two deques scanned linearly - up to 1000 uuid comparisons, three-plus times per line, on the path of every Debug+ line
	//of every thread and under ProtoLog's one mutex.  Worse, a hit did not reorder (the standing `//TODO update position`), so the
	//hottest strings drifted to the back and Trim evicted precisely the ones about to be used again, which then had to be re-emitted.
	//Now a map keyed by id with the last-use sequence as its value: the lookup is a binary search, a hit is a store, and Trim keeps
	//the most recently used.
	struct ProtoLogCache{
		α Clear()ι->void;
		α Trim()ι->void;
		α Touch( flat_map<uuid,uint>& cache, uuid id )ι->bool;//true when the id was not cached - the caller emits the string record only then.
		flat_map<uuid,uint> Args;
		flat_map<uuid,uint> Strings;
		uint Sequence{};
	};
	struct ProtoLog final : Logging::ILogger, noncopyable{
		ProtoLog( const jobject& settings )ε;
		~ProtoLog();
		Ω Init()ι->void;

		α Archive()ι->VoidAwait::Task;
		α Shutdown( bool terminate, SL sl )ι->void override;
		α DailyFile()ι->fs::path{ return _root/"log.binpb"; }
		α DailyFileStart()Ι->TimePoint{ ul _{_mutex}; return _dailyFileStart; }
		α Entries()Ε->vector<App::Log::Proto::FileEntry>;//the buffer *and* whatever is in flight - see the definition (L1).
		α BufferSize()Ι->uint{ ul _{_mutex}; return _toSave.size(); }//what M5's cap bounds - the unflushed buffer alone, not _inFlight.
		α Name()Ι->sv override{ return "ProtoLog"; }
		α Root()Ι->const fs::path&{ return _root; }
		α SetMinLevel( ELogLevel /*level*/ )ι->void override{}
		α SetAppPKs( App::ProgramPK appPK, App::ProgInstPK instancePK )ι->void{ _appPK = appPK; _instancePK = instancePK; }
		α TimeZone()Ι->const std::chrono::time_zone&{ return _tz; }
		α Today()Ι->std::chrono::year_month_day{ ul _{_mutex}; return _today; }
		α Write( const Logging::Entry& m )ι->void override;
		α Write( const Logging::Entry& m, App::ProgramPK appPK, App::ProgInstPK instancePK )ι->void override;
	private:
		App::ProgramPK _appPK{};
		App::ProgInstPK _instancePK{};
		α Write( const Logging::Entry& m, App::Log::Proto::LogEntryFile&& entry )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α AddString( uuid id, sv str )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α AddString( uuid id, sv str, flat_map<uuid,uint>& cache )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α AddArguments( const vector<string>& args, const ::google::protobuf::RepeatedPtrField<std::string>& ids )ι->void;//L2: by reference - it was copying the whole field per entry.
		ABSL_UNLOCK_FUNCTION(_mutex) α Save()ι->TAwait<CoLockGuard>::Task;//releases _mutex before its first suspend.
		α Save( vector<byte> toSave, uint flushId, CoLockGuard l )ι->VoidAwait::Task;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α DropBufferUnlocked()ι->uint;//Returns the bytes dropped, 0 while under the cap.

		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α StartTimer()ι->TimerAwait::Task;//held until its first suspend; the continuation retakes it.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α ResetTimerUnlocked()ι->void;
		ABSL_LOCKS_EXCLUDED(_mutex) α StopTimer()ι->void;//shutdown: no timer starts again.

		ProtoLogCache _cache ABSL_GUARDED_BY(_mutex);
		TimePoint _dailyFileStart ABSL_GUARDED_BY(_mutex){ TimePoint::max() };//re-seeded in the ctor when a daily file already exists.
		Duration _delay ABSL_GUARDED_BY(_mutex);
		const uint16 _delaySize{8096};
		bool _flushFailed ABSL_GUARDED_BY(_mutex){};//M5: a flush that failed suppresses the size trigger, so the timer retries instead of every log line starting a fresh Save.
		uint _droppedBytes ABSL_GUARDED_BY(_mutex){};//reported and reset when the file becomes writable again.
		const uint32 _maxBufferSize;
		mutable absl::Mutex _mutex;
		bool _needsArchive ABSL_GUARDED_BY(_mutex){false};
		atomic<uint> _running{};
		fs::path _root;
		static constexpr ELogTags _tags{ ELogTags::ExternalLogger };
		up<DurationTimer> _timer ABSL_GUARDED_BY(_mutex);
		const std::chrono::time_zone& _tz;
		std::chrono::year_month_day _today ABSL_GUARDED_BY(_mutex);
		vector<byte> _toSave ABSL_GUARDED_BY(_mutex);
		flat_map<uint,vector<byte>> _inFlight ABSL_GUARDED_BY(_mutex);
		uint _flushId ABSL_GUARDED_BY(_mutex){};
	};
}
