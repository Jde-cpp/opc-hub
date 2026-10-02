#include <jde/app/log/ProtoLog.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/co/LockKey.h>
#include <jde/fwk/co/Timer.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/process/execution.h>
#include <jde/app/proto/app.FromClient.h>
#include <jde/app/proto/LogProto.h>
#include "ArchiveAwait.h"
#include <thread>

#define let const auto

namespace Jde::App{
	using Protobuf::ToGuid;
	//Held as a local by every ProtoLog coroutine that resumes into `this`, so the count falls when the frame is destroyed however it
	//exits - return, co_return or an exception.  An atomic rather than a _mutex-guarded field: these frames drop and retake _mutex
	//around their awaits, so a destructor that took the lock could deadlock against one of them.
	struct Running final{
		Running( atomic<uint>& n )ι:_n{n}{ ++_n; }
		~Running(){ --_n; }
	private:
		atomic<uint>& _n;
	};
	//The day the daily file's *content* belongs to - not today's.  Seeding from now() meant a service that starts and stops
	//within one day never saw a day change, so _needsArchive was never set and log.binpb accumulated across every restart
	//cycle forever, with every logs() query deserializing the whole thing.  Only a process that happened to stay up across
	//midnight ever archived.  Seeded from the file instead, so the first entry of a new day arms the round that the previous
	//run should have done.
	//Last-write time rather than the first entry's timestamp: the file is appended on every flush, and reading it here would
	//deserialize the entire accumulated file on the startup path - the very cost this is fixing.  Residual window: a file
	//last appended just after midnight but holding only the previous day's entries reads as current, so its round waits for
	//the next day change.  Late, never lost - ArchiveAwait buckets entries by their own day, whenever the round runs.
	Ω dailyFileDay( const fs::path& dailyFile, const std::chrono::time_zone& tz )ι->std::chrono::year_month_day{
		std::error_code ec;
		let written = fs::last_write_time( dailyFile, ec );//ec overload: no file (the normal first-run case) is not an error here.
		//ToClock, because file_clock has no portable conversion to Clock: libc++ offers to_sys, MSVC only to_utc (which needs
		//the leap-second table and throws), and clang has no std::chrono::clock_cast.  Offsetting by both clocks' current
		//readings is exact to the sampling gap - orders of magnitude finer than the day this resolves to.
		return Chrono::LocalYMD( ec ? Clock::now() : Chrono::ToClock<Clock, fs::file_time_type::clock>(written), tz );
	}
	ProtoLog::ProtoLog( const jobject& settings )ε:
		Logging::ILogger{ settings },
		_delay{ Json::FindDuration(settings, "delay", ELogLevel::Error).value_or(1min) },
		_maxBufferSize{ std::max<uint32>(Json::FindNumber<uint32>(settings, "maxBuffer").value_or(4*1024*1024), _delaySize*4u) },
		_root{ Json::FindString(settings, "path").value_or((Process::AppDataFolder()/"logs").string()) },
		_tz{ Json::FindTimeZone(settings, "timeZone", *std::chrono::current_zone()) },
		_today{ dailyFileDay(DailyFile(), _tz) }{//_root and _tz are declared before _today, so both are live here.
		if( fs::exists(DailyFile()) )
			_dailyFileStart = TimePoint::min();
		Executor();//locks up if starts in StartTimer.
		Execution::Run();
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
		StopTimer();//_delay=min() first, so the cancelled timer's continuation re-arms nothing.
		//Poll, as ~RemoteLog does.  The wait is unbounded for the same reason and with the same caveat (C10): a cancel completion
		//posted to an io_context that has already stopped never runs, and then only the watchdog ends this.
		while( _running )
			std::this_thread::sleep_for( 1ms );
	}
	α ProtoLog::Init()ι->void{
		Logging::Add<ProtoLog>( "proto" );
	}

	α ProtoLog::Shutdown( bool terminate, SL )ι->void{
		if( terminate )
			return;
		//Every other writer of the daily file holds its LockKey; this one did not.  The stream is size-prefixed, so
		//appending into the middle of a flush's write leaves a file nothing can parse, and appending into one an archive
		//round is about to fs::remove loses the entries silently.
		//Try, never wait: loggers are destroyed *after* the executor is (Process::cleanup), so a flush or round suspended
		//mid-flight will never resume to release this key - blocking here would hang the process instead of losing a buffer.
		auto lock = TryLockKey( DailyFile().string() );
		uint dropped{};
		{
			ul _{ _mutex };
			if( _toSave.empty() )
				return;
			if( lock ){
				try{
					IO::SaveBinary<byte>( DailyFile(), _toSave, true );
				}
				catch( const runtime_error& )
				{}
				return;
			}
			dropped = _toSave.size();
		}
		ERR( "Shutdown could not take the daily file's lock - {} buffered bytes dropped rather than interleaved into it.", dropped );//outside _mutex: this reaches the loggers that are still alive.
	}
	α ProtoLog::Deserialize( sv bytes )ε->vector<App::Log::Proto::FileEntry>{
		return Protobuf::DeserializeVector<App::Log::Proto::FileEntry>( bytes );
	}

	α ProtoLog::Write( const Logging::Entry& e )ι->void{
		if( !empty(e.Tags & _tags) )//recursion guard
			return;
		auto proto = LogProto::LogEntryFile( e );
		App::Log::Proto::FileEntry fileEntry;
		*fileEntry.mutable_entry() = move( proto );
		Write( e, move(fileEntry) );
	}

	α ProtoLog::Write( const Logging::Entry& e, App::ProgramPK appPK, App::ProgInstPK instancePK )ι->void{
		if( !appPK || !instancePK || (appPK==_appPK && instancePK==_instancePK) )
			return Write( e );
		if( !empty(e.Tags & _tags) )//recursion guard
			return;
		auto proto = LogProto::LogEntryFile( e, appPK, instancePK );
		App::Log::Proto::FileEntry fileEntry;
		*fileEntry.mutable_external_entry() = move( proto );
		Write( e, move(fileEntry) );
	}
	α ProtoLog::Write( const Logging::Entry& e, App::Log::Proto::FileEntry&& fileEntry )ι->void{
		auto data = Protobuf::SizePrefixed( fileEntry );
		_mutex.lock();
		_dailyFileStart = std::min<TimePoint>( _dailyFileStart, e.Time );//inside the lock: read by the query coroutine, written by every logging thread.
		if( auto day = Chrono::LocalYMD(e.Time, _tz); _today!=day ){
			_today = day;
			_needsArchive = true;
		}
		AddString( e.Id(), e.Text );
		AddString( e.FileId(), e.File() );
		AddString( e.FunctionId(), e.Function() );
		switch( fileEntry.value_case() ){
			case App::Log::Proto::FileEntry::kEntry:
				AddArguments( e.Arguments, fileEntry.entry().args() );
			break;
			case App::Log::Proto::FileEntry::kExternalEntry:
				AddArguments( e.Arguments, fileEntry.external_entry().args() );
			break;
			default:
				ASSERTX( false );
		}

		std::copy( data.begin(), data.end(), std::back_inserter(_toSave) );
		let dropped = DropBufferUnlocked();//M5: the buffer grows between retries too, not only on the failures themselves.
		if( _toSave.size()>=_delaySize && !_flushFailed )
			Save();//unlocks _mutex.
		else{
			if( !_timer )
				StartTimer();
			_mutex.unlock();
		}
		if( dropped )//outside the lock - this reaches the other loggers.
			WARN( "The daily log buffer passed its {:L} byte cap while '{}' was unwritable - dropped {:L} buffered bytes.", _maxBufferSize, DailyFile().string(), dropped );
	}

	//M5: the one bound on a buffer that otherwise grows for as long as the daily file is unwritable (ENOSPC, EIO, a path made
	//read-only).  Trims the oldest, down to half the cap - as RemoteLog does ([`RemoteLog.cpp:65-71`]) - so the newest entries, the
	//ones describing whatever is going wrong, are the ones kept.
	//Whole records, never bytes: the buffer is a [4-byte big-endian length][body] stream (Protobuf::SizePrefixed), and cutting
	//mid-record would leave a file DeserializeVector reads as garbage and silently stops at - #2, self-inflicted.
	//_dailyFileStart is deliberately left where it is: it is a lower bound, and one that is too low only costs a read.
	α ProtoLog::DropBufferUnlocked()ι->uint{
		if( _toSave.size()<=_maxBufferSize )
			return 0;
		let target = (uint)_maxBufferSize/2;
		uint offset{};
		while( _toSave.size()-offset>target && offset+4<=_toSave.size() ){
			uint32 length{};
			for( uint i=0; i<4; ++i )
				length = ( length<<8 ) | (uint32)std::to_integer<uint8>( _toSave[offset+i] );
			if( offset+4+length>_toSave.size() )
				break;//only the tail can be partial, and a partial tail is not ours to cut.
			offset += 4+length;
		}
		_toSave.erase( _toSave.begin(), std::next(_toSave.begin(), (ptrdiff_t)offset) );
		//AddString emits each id once and _cache remembers it, so the dropped prefix may have held the only copy of a string a
		//surviving entry names - a repeated template, or the file/function pair emitted with the first entry from a call site.
		//Clearing makes the next entry re-emit them, and that is enough: ArchiveFile::Append collects every string in a pass of its
		//own before resolving any entry, so a record written *after* the entry naming it still resolves it.
		_cache.Clear();
		_droppedBytes += offset;
		return offset;
	}
	//L1: the buffer, plus every batch that has left it and not yet reached the file.  In flush order, and older than anything still
	//buffered - so a query sees the same entries in the same order whether or not a flush happens to be waiting on the key.
	α ProtoLog::Entries()Ε->vector<App::Log::Proto::FileEntry>{
		ul _{ _mutex };
		vector<App::Log::Proto::FileEntry> y;
		auto append = [&y]( const vector<byte>& bytes ){
			auto parsed = Deserialize( sv{(char*)bytes.data(), bytes.size()} );
			y.insert( y.end(), make_move_iterator(parsed.begin()), make_move_iterator(parsed.end()) );
		};
		for( let& [_, batch] : _inFlight )
			append( batch );
		append( _toSave );
		return y;
	}
	α ProtoLog::Save()ι->TAwait<CoLockGuard>::Task{
		Running _{ _running };//M9: this frame resumes into `this` - ~ProtoLog waits for it.
		auto toSave = move( _toSave );
		let flushId = ++_flushId;
		_inFlight.emplace( flushId, toSave );//L1: visible here until it is durable - the copy is one batch, and IO::WriteAwait already takes one.
		ResetTimerUnlocked();//_mutex is held on entry and released below.
		_toSave = {};
		_toSave.reserve( toSave.size() );
		_cache.Trim();
		_mutex.unlock();
		Save( move(toSave), flushId, co_await LockKeyAwait{DailyFile().string()} );
	}
	α ProtoLog::Save( vector<byte> toSave, uint flushId, CoLockGuard )ι->VoidAwait::Task{
		Running _{ _running };//M9: this frame resumes into `this` - ~ProtoLog waits for it.
		try{
			TRACE( "Saving {} bytes to {}", toSave.size(), DailyFile().string() );
			//NOT _dailyFileStart = max() here.  A flush moves entries from the buffer into the daily file - both of which are
			//"local" - so parking the bound at max() after every successful flush told LogAwait::ShouldReadLocal that nothing
			//was local at all: `*_endTime > max` is false for any finite bound, so every time-bounded query silently skipped
			//the whole of today's log and answered out of the archives alone.  The bound is only released when the round
			//below actually takes the file away.
			co_await IO::WriteAwait( DailyFile(), vector<byte>{toSave}, true, IO::EWriteMode::Append, _tags );
		}
		catch( const runtime_error& ){
			bool firstFailure; uint dropped; Duration delay;
			{
				ul _{ _mutex };
				firstFailure = !_flushFailed;
				_flushFailed = true;
				_inFlight.erase( flushId );//back out of flight before it goes back in the buffer, or Entries() would report it twice.
				_toSave.insert( _toSave.begin(), toSave.begin(), toSave.end() );//prepend: strings must precede the entries referencing them.
				dropped = DropBufferUnlocked();
				delay = _delay;
				if( !_timer )
					StartTimer();//the retry must not depend on another log line arriving - StartTimer returns at its first suspend with _mutex still held.
			}
			//M5: the failure had no line of its own - only the IOException's, one per log line written, which said nothing about the
			//buffer behind it.  Once per outage, not once per flush.
			if( firstFailure )
				WARN( "Could not write the daily log file '{}' - buffering, and retrying every {}.", DailyFile().string(), Chrono::ToString(delay) );
			if( dropped )
				WARN( "The daily log buffer passed its {:L} byte cap while '{}' was unwritable - dropped {:L} buffered bytes.", _maxBufferSize, DailyFile().string(), dropped );
			co_return;
		}
		{//recovered: say so, and account for what the outage cost.
			uint dropped{}; bool recovered{};
			{
				ul _{ _mutex };
				_inFlight.erase( flushId );//durable now, and readable from the file - Entries() must stop reporting it.
				if( (recovered = _flushFailed) ){
					_flushFailed = false;
					dropped = std::exchange( _droppedBytes, 0u );
				}
			}
			if( recovered )
				INFO( "The daily log file '{}' is writable again{}.", DailyFile().string(), dropped ? Ƒ(" - {:L} buffered bytes were dropped while it was not", dropped) : string{} );
		}
		//An archive round reads the daily file only, so anything written while this flush was in flight has to land in the file
		//first or it is archived a round late.  It must not be archived out of the buffer instead: the round deletes the file but
		//cannot trim the buffer, so those entries would be archived again as soon as the next flush wrote them to disk.
		//Still holding the daily file's LockKey, so no other flush can interleave with the drain.
		vector<byte> pending;
		{
			ul _{ _mutex };
			if( !_needsArchive )
				co_return;
			pending = move( _toSave );
			_toSave.reserve( pending.size() );
			_needsArchive = false;//cleared here, under _mutex: a day-changed entry arriving after the drain re-arms it for the next round instead of being stranded in the daily file.
			_cache.Clear();
		}
		if( pending.size() ){
			try{
				co_await IO::WriteAwait( DailyFile(), vector<byte>{pending}, true, IO::EWriteMode::Append, _tags );
			}
			catch( const runtime_error& ){
				ul _{ _mutex };
				_toSave.insert( _toSave.begin(), pending.begin(), pending.end() );
				_needsArchive = true;//nothing archived - re-arm so the next flush retries the round.
				co_return;//no bound to take back since L1: the drain no longer releases it on the promise of a round.

			}
		}
		Archive();
	}
	α ProtoLog::Archive()ι->VoidAwait::Task{
		Running _{ _running };//M9: this frame resumes into `this` - ~ProtoLog waits for it.
		try{
			co_await ArchiveAwait{ DailyFile(), _root, _tz };
			ul _{ _mutex };
			//L1: parked here, not in the drain - the file is gone now, so nothing local is behind this bound.  Only when nothing has
			//been written since: an entry that arrived during the round is in _toSave, which is local, and already lowered it.
			if( _toSave.empty() && _inFlight.empty() )
				_dailyFileStart = TimePoint::max();
		}
		catch( const runtime_error& ){
			ul _{ _mutex };//the round failed, so the daily file survives with everything the drain wrote into it - keep the widest bound.
			_dailyFileStart = TimePoint::min();
		}
	}

	α ProtoLog::AddString( uuid id, sv str )ι->void{
		AddString( id, str, _cache.Strings );
	}
	α ProtoLog::AddString( uuid id, sv str, flat_map<uuid,uint>& cache )ι->void{
		ASSERTX( str.size() || id==EmptyStringMd5 );
		if( !_cache.Touch(cache, id) )
			return;//already emitted since the last Clear, and now marked most-recently-used.
		App::Log::Proto::FileEntry fileEntry;
		*fileEntry.mutable_str() = LogProto::ToString( id, string{str} );
		auto data = Protobuf::SizePrefixed( fileEntry );
		std::copy( data.begin(), data.end(), std::back_inserter(_toSave) );//TODO copy in SizePrefixed
	}
	α ProtoLog::AddArguments( const vector<string>& args, const ::google::protobuf::RepeatedPtrField<std::string>& ids )ι->void{
		ASSERTX( args.size()==(uint)ids.size() );
		for( uint i=0; i<args.size(); ++i )
			AddString( ToGuid(ids.Get((int)i)), args[i], _cache.Args );
	}

	//Not analyzed:  entered with _mutex held - the caller's - until the first suspend, then retakes it in the continuation.
	ABSL_NO_THREAD_SAFETY_ANALYSIS α ProtoLog::StartTimer()ι->TimerAwait::Task{
		Running _{ _running };//M9: this frame resumes into `this` - ~ProtoLog waits for it.
		if( _delay==Duration::min() )
			co_return;
		_timer = mu<DurationTimer>( _delay, SRCE_CUR );
		let finished = co_await *_timer;
		if( finished ){
			_mutex.lock();
			_timer = nullptr;//let Write restart the timer for subsequent entries.
			if( !_toSave.empty() )
				Save();//unlocks _mutex.
			else
				_mutex.unlock();
		}
		else{
			ul _{ _mutex };
			if( _toSave.size() )
				StartTimer();
			else
				_timer = nullptr;
		}
	}

	//StartTimer's continuation nulls _timer - destroying the DurationTimer - under this same lock, so
	//an unguarded `if( _timer ) _timer->Cancel()` could pass the test on the shutdown thread and then call Cancel() on
	//freed memory once the io thread ran the continuation.  Every other _timer access was already guarded; this one was
	//not, only because Save() calls it with the lock already held.
	//Safe to hold _mutex across Cancel(): it takes the DurationTimer's own lock and calls asio's cancel(), which *posts*
	//the completion - the continuation that re-takes _mutex never runs on this stack, so there is no re-entry to deadlock
	//on, and the lock order is only ever ProtoLog -> DurationTimer.
	α ProtoLog::ResetTimerUnlocked()ι->void{
		if( _timer )
			_timer->Cancel();
	}
	//One locked operation, because it is two writes: _delay is read by StartTimer under _mutex, so setting it from the
	//shutdown thread without the lock raced the very timer being cancelled.  min() is the sentinel StartTimer bails on.
	α ProtoLog::StopTimer()ι->void{
		ul _{ _mutex };
		_delay = Duration::min();
		ResetTimerUnlocked();
	}
	α ProtoLogCache::Touch( flat_map<uuid,uint>& cache, uuid id )ι->bool{
		let added = cache.try_emplace( id, ++Sequence );
		if( !added.second )
			added.first->second = ++Sequence;//the reordering the deque never did: a hit is now the newest, so Trim keeps it.
		return added.second;
	}
	α ProtoLogCache::Clear()ι->void{
		Args.clear();
		Strings.clear();
		Sequence = 0;
	}
	//Keeps the most recently used, which is the point of the sequence.  nth_element for the threshold rather than a sort, and the
	//survivors are rebuilt in key order so each insert is an append - Trim runs once per flush, so neither is on the hot path.
	Ω trim( flat_map<uuid,uint>& cache )ι->void{
		constexpr uint maxSize{ 1000 };
		if( cache.size()<=maxSize )
			return;
		vector<uint> sequences;
		sequences.reserve( cache.size() );
		for( let& [_, sequence] : cache )
			sequences.push_back( sequence );
		let cut = sequences.size()-maxSize;
		std::nth_element( sequences.begin(), std::next(sequences.begin(), (ptrdiff_t)cut), sequences.end() );
		let threshold = sequences[cut];
		flat_map<uuid,uint> kept;
		for( let& kv : cache ){
			if( kv.second>=threshold )
				kept.insert( kept.end(), kv );//the source is in key order, so every insert is an append.
		}
		cache = move( kept );
	}
	α ProtoLogCache::Trim()ι->void{
		trim( Args );
		trim( Strings );
	}
}