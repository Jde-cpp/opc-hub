#include <jde/app/log/ProtoLog.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/co/LockKey.h>
#include <jde/fwk/io/durable.h>
#include <jde/fwk/io/file.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/io/records/Scan.h>
#include <jde/app/proto/app.FromClient.h>
#include <jde/app/proto/LogProto.h>
#include "ArchiveAwait.h"

#define let const auto

namespace Jde::App{
	using Protobuf::ToGuid;
	using Log::FileEntry;
	//The day the daily file's *content* belongs to - not today's.  Seeding from now() meant a service that starts and stops
	//within one day never saw a day change, so _needsArchive was never set and log.binpb accumulated across every restart
	//cycle forever, with every logs() query deserializing the whole thing.  Only a process that happened to stay up across
	//midnight ever archived.  Seeded from the file instead, so the first entry of a new day arms the round that the previous
	//run should have done.
	//Last-write time rather than the first entry's timestamp: the file is appended on every flush, and reading it here would
	//scan the entire accumulated file on the startup path - the very cost this is fixing.  Residual window: a file last
	//appended just after midnight but holding only the previous day's entries reads as current, so its round waits for
	//the next day change.  Late, never lost - ArchiveAwait buckets entries by their own day, whenever the round runs.
	Ω dailyFileDay( const fs::path& dailyFile, const std::chrono::time_zone& tz )ι->std::chrono::year_month_day{
		std::error_code ec;
		let written = fs::last_write_time( dailyFile, ec );//ec overload: no file (the normal first-run case) is not an error here.
		//ToClock, because file_clock has no portable conversion to Clock: libc++ offers to_sys, MSVC only to_utc (which needs
		//the leap-second table and throws), and clang has no std::chrono::clock_cast.  Offsetting by both clocks' current
		//readings is exact to the sampling gap - orders of magnitude finer than the day this resolves to.
		return Chrono::LocalYMD( ec ? Clock::now() : Chrono::ToClock<Clock, fs::file_time_type::clock>(written), tz );
	}
	Ω bytesOf( const vector<FileEntry>& records )ι->uint{
		uint y{};
		for( let& r : records )
			y += (uint)r.ByteSizeLong();
		return y;
	}
	ProtoLog::ProtoLog( const jobject& settings )ε:
		BufferedLogger{ settings, std::max<uint32>(Json::FindNumber<uint32>(settings, "maxBuffer").value_or(4*1024*1024), _delaySize*4u) },
		_root{ Json::FindString(settings, "path").value_or((Process::AppDataFolder()/"logs").string()) },
		_tz{ Json::FindTimeZone(settings, "timeZone", *std::chrono::current_zone()) },
		_today{ dailyFileDay(DailyFile(), _tz) }{//_root and _tz are declared before _today, so both are live here.
		_file.Path = DailyFile();
		if( fs::exists(DailyFile()) )
			_dailyFileStart = TimePoint::min();
		Process::AddShutdownFunction( [](bool /*terminate*/, SL){	//member Shutdown gets called after timer thread shutdown.
			if( auto log = Logging::FindLogger<App::ProtoLog>(); log )
				log->StopTimer();
		});
		try{
			fs::create_directories( _root );
		}
		catch( std::filesystem::filesystem_error& e ){
			throw IO::IOException( move(e) );
		}
	}
	ProtoLog::~ProtoLog(){
		Stop();
	}
	α ProtoLog::Init()ι->void{
		Logging::Add<ProtoLog>( "proto" );
	}

	α ProtoLog::Shutdown( bool terminate, SL sl )ι->void{
		if( terminate )
			return;
		//Every other writer of the daily file holds its LockKey; this one did not.  Appending into the middle of a flush's
		//write would leave a run no checkpoint proves, and appending into one an archive round is about to fs::remove
		//loses the entries silently.
		//Try, never wait: loggers are destroyed *after* the executor is (Process::cleanup), so a flush or round suspended
		//mid-flight will never resume to release this key - blocking here would hang the process instead of losing a buffer.
		auto lock = TryLockKey( DailyFile().string() );
		vector<FileEntry> batch;
		uint dropped{};
		{
			ul _{ _mutex };
			if( _toSave.empty() )
				return;
			if( lock ){
				batch = move( _toSave );
				_toSave = {};
				_toSaveBytes = 0;
			}
			else
				dropped = _toSaveBytes;
		}
		if( !lock ){
			ERR( "Shutdown could not take the daily file's lock - {} buffered bytes dropped rather than interleaved into it.", dropped );//outside _mutex: this reaches the loggers that are still alive.
			return;
		}
		//Read and written now:  the executor that the file awaitables complete on may be gone.
		try{
			if( !_file.Opened ){
				Log::Opening opening{ DailyFile(), sl };
				while( opening.Next()==Log::ENext::More )
					IO::Records::FeedNow( opening, sl );
				_file = opening.Take();
			}
			auto run = _file.Prepare( move(batch), sl );
			std::error_code ec;
			if( let size = fs::exists(run.Path, ec) ? fs::file_size(run.Path, ec) : 0; !ec && size>run.Offset )//an outstanding append's bytes, which the run cuts.
				fs::resize_file( run.Path, run.Offset, ec );
			if( ec )
				throw IO::Failed( run.Path, ec, sl );
			IO::SaveBinary( run.Path, std::span<const char>{run.Bytes.data(), run.Bytes.size()}, true, sl );
			_file.Commit( move(run) );
		}
		catch( const runtime_error& )
		{}
	}
	α ProtoLog::Write( const Logging::Entry& e )ι->void{
		if( empty(e.Tags & _tags) )//recursion guard
			Write( e, LogProto::LogEntryFile(e) );
	}
	α ProtoLog::Write( const Logging::Entry& e, App::ProgramPK appPK, App::ProgInstPK instancePK )ι->void{
		if( !appPK || !instancePK || (appPK==_appPK && instancePK==_instancePK) )
			Write( e );
		else if( empty(e.Tags & _tags) )//recursion guard
			Write( e, LogProto::LogEntryFile(e, appPK, instancePK) );//the non-zero app_pk is what marks it forwarded.
	}
	α ProtoLog::Write( const Logging::Entry& e, App::Log::Proto::LogEntryFile&& entry )ι->void{
		FileEntry fileEntry;
		*fileEntry.mutable_entry() = move( entry );
		_mutex.lock();
		_dailyFileStart = std::min<TimePoint>( _dailyFileStart, e.Time );//inside the lock: read by the query coroutine, written by every logging thread.
		if( auto day = Chrono::LocalYMD(e.Time, _tz); _today!=day ){
			_today = day;
			_needsArchive = true;
		}
		AddString( e.Id(), e.Text );
		AddString( e.FileId(), e.File() );
		AddString( e.FunctionId(), e.Function() );
		AddArguments( e.Arguments, fileEntry.entry().args() );
		Add( move(fileEntry) );
		let firstDrop = Cap();//M5: the buffer grows between retries too, not only on the failures themselves.
		if( _toSaveBytes>=_delaySize && !_flushFailed )
			Save();//unlocks _mutex.
		else{
			ArmTimer();
			_mutex.unlock();
		}
		if( firstDrop )//outside the lock - this reaches the other loggers.
			WARN( "The daily log buffer passed its {:L} byte cap while '{}' was unwritable - dropping the oldest.", MaxSize(), DailyFile().string() );
	}
	α ProtoLog::Add( FileEntry&& record )ι->void{
		_toSaveBytes += (uint)record.ByteSizeLong();//its frame in the file:  a FileEntry with one member serializes as that field.
		_toSave.push_back( move(record) );
	}
	α ProtoLog::Return( vector<FileEntry>&& batch )ι->void{
		_toSaveBytes += bytesOf( batch );
		_toSave.insert( _toSave.begin(), make_move_iterator(batch.begin()), make_move_iterator(batch.end()) );
	}

	//M5: the daily file has been unwritable (ENOSPC, EIO, a path made read-only) for as long as it took to pass the cap.  Trims the
	//oldest, down to half the cap.
	//_dailyFileStart is deliberately left where it is: it is a lower bound, and one that is too low only costs a read.
	α ProtoLog::Drop()ι->uint{
		let target = MaxSize()/2;
		uint dropped{};
		auto p = _toSave.begin();
		for( ; p!=_toSave.end() && _toSaveBytes-dropped>target; ++p )
			dropped += (uint)p->ByteSizeLong();
		_toSave.erase( _toSave.begin(), p );
		_toSaveBytes -= dropped;
		//The dropped prefix may have held the only copy of a string a surviving entry names.  Clearing makes the next entry
		//re-emit it, and that is enough: ArchiveQuery collects every string in a pass of its own before resolving any entry,
		//so a record written *after* the entry naming it still resolves it.
		_cache.Clear();
		return dropped;
	}
	//L1: the buffer, plus every batch that has left it and not yet reached the file.  In flush order, and older than anything still
	//buffered - so a query sees the same entries in the same order whether or not a flush happens to be waiting on the key.
	α ProtoLog::Entries()Ε->vector<FileEntry>{
		ul _{ _mutex };
		vector<FileEntry> y;
		for( let& [_, batch] : _inFlight )
			y.insert( y.end(), batch.begin(), batch.end() );
		y.insert( y.end(), _toSave.begin(), _toSave.end() );
		return y;
	}
	α ProtoLog::Save()ι->TAwait<CoLockGuard>::Task{
		Running _{ *this };//M9: this frame resumes into `this` - ~ProtoLog waits for it.
		auto toSave = move( _toSave );
		let flushId = ++_flushId;
		_inFlight.emplace( flushId, toSave );//L1: visible here until it is durable - the copy is one batch.
		ResetTimerUnlocked();//a size flush restarts the round's delay, or ends the round if nothing follows.  _mutex is held on entry and released below.
		_toSave = {};
		_toSave.reserve( toSave.size() );
		_toSaveBytes = 0;
		_cache.Clear();//the batch is a run of its own, so the next run carries its strings again.
		_mutex.unlock();
		Save( move(toSave), flushId, co_await LockKeyAwait{DailyFile().string()} );
	}
	α ProtoLog::Save( vector<FileEntry> toSave, uint flushId, CoLockGuard )ι->VoidAwait::Task{
		Running _{ *this };//M9: this frame resumes into `this` - ~ProtoLog waits for it.
		try{
			TRACE( "Saving {} records to {}", toSave.size(), DailyFile().string() );
			//NOT _dailyFileStart = max() here.  A flush moves entries from the buffer into the daily file - both of which are
			//"local" - so parking the bound at max() after every successful flush told LogQLAwait's read-local check that nothing
			//was local at all: `*_endTime > max` is false for any finite bound, so every time-bounded query silently skipped
			//the whole of today's log and answered out of the archives alone.  The bound is only released when the round
			//below actually takes the file away.
			if( !_file.Opened )//the first open:  its scan.
				_file = co_await Log::DailyOpenAwait{ DailyFile() };
			auto run = _file.Prepare( toSave, SRCE_CUR );//a copy:  a write that fails puts toSave back.
			co_await run.Write();
			_file.Commit( move(run) );
		}
		catch( const runtime_error& ){
			bool firstFailure, firstDrop; Duration delay;
			{
				ul _{ _mutex };
				firstFailure = !_flushFailed;
				_flushFailed = true;
				_inFlight.erase( flushId );//back out of flight before it goes back in the buffer, or Entries() would report it twice.
				Return( move(toSave) );
				firstDrop = Cap();
				delay = Delay();
				ArmTimer();//the retry must not depend on another log line arriving.
			}
			//M5: the failure had no line of its own - only the IOException's, one per log line written, which said nothing about the
			//buffer behind it.  Once per outage, not once per flush.
			if( firstFailure )
				WARN( "Could not write the daily log file '{}' - buffering, and retrying every {}.", DailyFile().string(), Chrono::ToString(delay) );
			if( firstDrop )
				WARN( "The daily log buffer passed its {:L} byte cap while '{}' was unwritable - dropping the oldest.", MaxSize(), DailyFile().string() );
			co_return;
		}
		{//recovered: say so, and account for what the outage cost.
			uint dropped{}; bool recovered{};
			{
				ul _{ _mutex };
				_inFlight.erase( flushId );//durable now, and readable from the file - Entries() must stop reporting it.
				if( (recovered = _flushFailed) ){
					_flushFailed = false;
					dropped = TakeDropped();
				}
			}
			if( recovered )
				INFO( "The daily log file '{}' is writable again{}.", DailyFile().string(), dropped ? Ƒ(" - {:L} buffered bytes were dropped while it was not", dropped) : string{} );
		}
		//An archive round reads the daily file only, so anything written while this flush was in flight has to land in the file
		//first or it is archived a round late.  It must not be archived out of the buffer instead: the round deletes the file but
		//cannot trim the buffer, so those entries would be archived again as soon as the next flush wrote them to disk.
		//Still holding the daily file's LockKey, so no other flush can interleave with the drain.
		vector<FileEntry> pending;
		{
			ul _{ _mutex };
			if( !_needsArchive )
				co_return;
			pending = move( _toSave );
			_toSave = {};
			_toSave.reserve( pending.size() );
			_toSaveBytes = 0;
			_needsArchive = false;//cleared here, under _mutex: a day-changed entry arriving after the drain re-arms it for the next round instead of being stranded in the daily file.
			_cache.Clear();
		}
		if( pending.size() ){
			try{
				auto run = _file.Prepare( pending, SRCE_CUR );
				co_await run.Write();
				_file.Commit( move(run) );
			}
			catch( const runtime_error& ){
				ul _{ _mutex };
				Return( move(pending) );
				_needsArchive = true;//nothing archived - re-arm so the next flush retries the round.
				co_return;//no bound to take back since L1: the drain no longer releases it on the promise of a round.
			}
		}
		Archive();
	}
	α ProtoLog::Archive()ι->VoidAwait::Task{
		Running _{ *this };//M9: this frame resumes into `this` - ~ProtoLog waits for it.
		try{
			co_await ArchiveAwait{ *this };
			bool recovered;
			{
				ul _{ _mutex };
				//L1: parked here, not in the drain - the file is gone now, so nothing local is behind this bound.  Only when nothing has
				//been written since: an entry that arrived during the round is in _toSave, which is local, and already lowered it.
				if( _toSave.empty() && _inFlight.empty() )
					_dailyFileStart = TimePoint::max();
				recovered = std::exchange( _archiveFailed, false );
			}
			if( recovered )
				INFO( "The archive round of '{}' succeeded again.", DailyFile().string() );
		}
		catch( const runtime_error& e ){
			//#235: the round is tried again by the next flush, and adds no archived day twice - each archive says what it
			//took (Log.proto's Merged).  Said at Error once, until a round succeeds.
			ul _{ _mutex };//the round failed, so the daily file survives with everything the drain wrote into it - keep the widest bound.
			_dailyFileStart = TimePoint::min();
			_needsArchive = true;
			if( let x = dynamic_cast<const Exception*>(&e); x )
				x->SetLevel( std::exchange(_archiveFailed, true) ? ELogLevel::Debug : ELogLevel::Error );
		}
	}

	α ProtoLog::AddString( uuid id, sv str )ι->void{
		AddString( id, str, _cache.Strings );
	}
	α ProtoLog::AddString( uuid id, sv str, flat_set<uuid>& cache )ι->void{
		ASSERTX( str.size() || id==EmptyStringMd5 );
		if( !_cache.Touch(cache, id) )
			return;//already in the buffer since the last Clear.
		FileEntry fileEntry;
		*fileEntry.mutable_str() = LogProto::ToString( id, string{str} );
		Add( move(fileEntry) );
	}
	α ProtoLog::AddArguments( const vector<string>& args, const ::google::protobuf::RepeatedPtrField<std::string>& ids )ι->void{
		ASSERTX( args.size()==(uint)ids.size() );
		for( uint i=0; i<args.size(); ++i )
			AddString( ToGuid(ids.Get((int)i)), args[i], _cache.Args );
	}

	α ProtoLogCache::Touch( flat_set<uuid>& cache, uuid id )ι->bool{
		return cache.emplace( id ).second;
	}
	α ProtoLogCache::Clear()ι->void{
		Args.clear();
		Strings.clear();
	}
}