#pragma once
#include <chrono>
#include <jde/fwk/settings.h>
#include <jde/fwk/co/LockKey.h>
#include <jde/app/log/BufferedLogger.h>
#include <jde/app/log/Records.h>
#include <jde/app/usings.h>

namespace Jde::App{
	//The strings the buffer holds a record of, by id:  Touch says whether one is still to be emitted.  Cleared with every
	//flush, so each batch carries every string its entries name and the append leaves out those the daily file holds
	//(Log::DailyFile::Strings), which only it knows, and when Drop trims the buffer, since the dropped prefix may have
	//held the only copy of a string a surviving entry names.
	struct ProtoLogCache{
		α Clear()ι->void;
		α Touch( flat_set<uuid>& cache, uuid id )ι->bool;//true when the id was not cached - the caller emits the string record only then.
		flat_set<uuid> Args;
		flat_set<uuid> Strings;
	};
	struct ProtoLog final : BufferedLogger{
		ProtoLog( const jobject& settings )ε;
		~ProtoLog();
		Ω Init()ι->void;

		α Archive()ι->VoidAwait::Task;
		α Shutdown( bool terminate, SL sl )ι->void override;
		α DailyFile()ι->fs::path{ return _root/"log.binpb"; }
		α DailyFileStart()Ι->TimePoint{ ul _{_mutex}; return _dailyFileStart; }
		α Entries()Ε->vector<App::Log::Proto::FileEntry>;//the buffer *and* whatever is in flight - see the definition (L1).
		α BufferSize()Ι->uint{ ul _{_mutex}; return _toSaveBytes; }//what M5's cap bounds - the unflushed buffer alone, not _inFlight.
		α Name()Ι->sv override{ return "ProtoLog"; }
		α Root()Ι->const fs::path&{ return _root; }
		α SetMinLevel( ELogLevel /*level*/ )ι->void override{}
		α SetAppPKs( App::ProgramPK appPK, App::ProgInstPK instancePK )ι->void{ _appPK = appPK; _instancePK = instancePK; }
		α TimeZone()Ι->const std::chrono::time_zone&{ return _tz; }
		α Today()Ι->std::chrono::year_month_day{ ul _{_mutex}; return _today; }
		α Write( const Logging::Entry& m )ι->void override;
		α Write( const Logging::Entry& m, App::ProgramPK appPK, App::ProgInstPK instancePK )ι->void override;
	private:
		friend struct ArchiveAwait;
		friend struct DailyLoadAwait;
		App::ProgramPK _appPK{};
		App::ProgInstPK _instancePK{};
		α Write( const Logging::Entry& m, App::Log::Proto::LogEntryFile&& entry )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Add( App::Log::FileEntry&& record )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α AddString( uuid id, sv str )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α AddString( uuid id, sv str, flat_set<uuid>& cache )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α AddArguments( const vector<string>& args, const ::google::protobuf::RepeatedPtrField<std::string>& ids )ι->void;//L2: by reference - it was copying the whole field per entry.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Drop()ι->uint override;//whole records, down to half the cap; returns the bytes dropped.
		ABSL_UNLOCK_FUNCTION(_mutex) α Flush()ι->void override{ Save(); }
		ABSL_UNLOCK_FUNCTION(_mutex) α Save()ι->TAwait<CoLockGuard>::Task;//releases _mutex before its first suspend.
		α Save( vector<App::Log::FileEntry> toSave, uint flushId, CoLockGuard l )ι->VoidAwait::Task;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Return( vector<App::Log::FileEntry>&& batch )ι->void;//back into the buffer, ahead of what came since:  its strings precede its entries still.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α Size()Ι->uint override{ return _toSaveBytes; }

		ProtoLogCache _cache ABSL_GUARDED_BY(_mutex);
		TimePoint _dailyFileStart ABSL_GUARDED_BY(_mutex){ TimePoint::max() };//re-seeded in the ctor when a daily file already exists.
		static constexpr uint16 _delaySize{8096};
		bool _flushFailed ABSL_GUARDED_BY(_mutex){};//M5: a flush that failed suppresses the size trigger, so the timer retries instead of every log line starting a fresh Save.
		bool _archiveFailed ABSL_GUARDED_BY(_mutex){};//the last round failed:  said at Error once, and the first flush past _archiveRetry tries again.
		//A failed round's wait before the next, so a cause that lasts isn't paid for at every flush:  Delay() at the first
		//failure, doubled per failure up to an hour.
		Duration _archiveBackoff ABSL_GUARDED_BY(_mutex){};
		TimePoint _archiveRetry ABSL_GUARDED_BY(_mutex){};
		bool _needsArchive ABSL_GUARDED_BY(_mutex){false};
		fs::path _root;
		const std::chrono::time_zone& _tz;
		std::chrono::year_month_day _today ABSL_GUARDED_BY(_mutex);
		//Each record as the daily file will hold it, in order:  every string a run's entries name, then the entry.
		vector<App::Log::FileEntry> _toSave ABSL_GUARDED_BY(_mutex);
		uint _toSaveBytes ABSL_GUARDED_BY(_mutex){};//what _toSave takes in the file, which the cap bounds.
		flat_map<uint,vector<App::Log::FileEntry>> _inFlight ABSL_GUARDED_BY(_mutex);
		uint _flushId ABSL_GUARDED_BY(_mutex){};
		App::Log::DailyFile _file;//as the process knows it:  under the daily file's LockKey, not _mutex.
	};
}