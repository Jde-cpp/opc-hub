#include <jde/flows/FileTrace.h>
#include <condition_variable>
#include <deque>
#include <jde/fwk/io/durable.h>
#include <jde/fwk/io/records/FileState.h>
#include <jde/fwk/io/records/Rewrite.h>
#include "Records.h"

#define let const auto

namespace Jde::Opc::Flows{
	using namespace std::chrono;
	using IO::Records::DayDirectory;
	constexpr ELogTags _tags{ ELogTags::IO };
	constexpr uint _maxBuffer{ 16u<<20 };//bytes a flow's buffer holds:  past it a healthy file's flush comes now, and while the file is unwritable or behind the oldest go.
	constexpr uint _maxAncestors{ 16 };//how far up a lineage walks:  each ancestor is a pass over the days.

	//A day's file as the process knows it:  the record layer's state, from its first-open scan and the appends since.
	struct DayFile final : IO::Records::FileState{ bool Opened{}; };//scanned, or made, by this process:  until then nothing is known of it.
	//One append to a day's file, a run:  what Write takes and Commit records.
	struct Pending final{
		Day Date;
		fs::path Path;
		uint Offset{};//the file's bytes that stay, which the run follows.
		string Bytes;
		bool Restart{};//the file was removed since, so what was known of it goes.
		uint End{};
		int64_t Chain{};
		vector<Run> Runs;
		α Write( SRCE )ι->IO::WriteAwait{ return IO::Records::AppendWrite( Path, Offset, move(Bytes), sl ); }
	};
	struct Served final{ sp<IO::OpenFile> File; vector<Run> Runs; };

	//One flow's:  its buffer and what is known of its files, under Mutex.
	struct TraceStore::Files final{
		Files( string slug, fs::path dir )ι:Slug{ move(slug) }, Dir{ move(dir) }{}
		α File( Day day )Ι->fs::path{ return Dir/DayDirectory( day )/TraceFileName; }
		const string Slug;
		const fs::path Dir;
		std::mutex Mutex;
		vector<Proto::FlowRecord> Buffer;
		uint BufferBytes{};
		bool Flushing{};
		bool Again{};//a Flush asked for while one ran.
		bool Failing{};//the last write failed, said once until one succeeds.
		bool Dropping{};//the buffer passed its cap while failing, said once.
		bool Purged{};
		bool Early{};//Timer is set for now:  the buffer passed its cap.
		Hist::IClock::TimerId Timer{};//the flush's.
		vector<absl::AnyInvocable<void()>> Done;//each Flush's, run once the next flush has written.
		std::map<Day,DayFile> Known;
		std::condition_variable Idle;//Flushing went false.
	};

	struct TraceStore::Impl final{
		Impl( TraceSettings settings, sp<Hist::IClock> clock )ι:
			Settings{ move(settings) },
			Zone{ Settings.Zone ? *Settings.Zone : *current_zone() },
			Clock{ move(clock) }
		{}
		α Find( sv slug, bool make )ι->sp<Files>{
			lg _{ Mutex };
			if( let p = Flows.find(slug); p!=Flows.end() )
				return p->second;
			if( !make || Stopped )
				return nullptr;
			if( !DirectoryName(slug) ){
				ERR( "[{}]The flow's slug is no directory name:  nothing of it is traced.", slug );
				return nullptr;
			}
			return Flows.emplace( string{slug}, ms<Files>(string{slug}, Settings.Root/slug) ).first->second;
		}
		//The host holds a slug to a file name's characters:  one written another way is not followed out of the directory.
		Ω DirectoryName( sv slug )ι->bool{ return !slug.empty() && slug!="." && slug!=".." && slug.find_first_of( "/\\" )==sv::npos; }

		const TraceSettings Settings;
		const time_zone& Zone;
		const sp<Hist::IClock> Clock;
		std::mutex Mutex;
		flat_map<string,sp<Files>,std::less<>> Flows;
		Hist::IClock::TimerId RollTimer{};
		bool Started{};
		std::atomic<bool> Stopped{};//written under Mutex, and read under a flow's too.
	};

	Ω toProtoStatus( const NodeStatus& status )ι->Proto::Status{
		Proto::Status y;
		y.set_fill( status.Fill );
		y.set_shape( status.Shape );
		y.set_text( status.Text );
		return y;
	}
	Ω days( const fs::path& dir, SL sl )ε->vector<Day>{
		std::error_code ec;
		if( !fs::is_directory(dir, ec) )
			return {};
		return IO::Records::Days( dir, TraceFileName, sl );
	}
	Ω dayOf( TimePoint t, const time_zone& tz )ι->Day{ return Hist::DayOf( t, tz ); }
	Ω secondsOf( MessageId id )ι->TimePoint{ return TimePoint{ seconds{(int64_t)(id>>32)} }; }//the gateway's start, which a run's ids carry (MessageIds).

	//A file's first-open scan, read now:  none for a file that isn't there.  Throws when it is there and can't be read through.
	Ω scan( const fs::path& path, SL sl )ε->optional<std::pair<IO::Records::Scanned,optional<IO::Records::Stamp>>>{
		sp<IO::OpenFile> file;
		try{
			file = ms<IO::OpenFile>( path, sl );
		}
		catch( IO::IOException& e ){
			if( e.Error!=IO::EIOError::NotFound ){
				e.SetLevel( ELogLevel::Error );
				throw;
			}
			e.SetLevel( ELogLevel::Debug );
			return nullopt;
		}
		let stamp = IO::Records::StampOf( path );
		Decoder decoder;
		return std::pair{ IO::Records::ScanNow(file, StartField, CheckpointField, decoder, sl), stamp };
	}
	Ω apply( DayFile& file, optional<std::pair<IO::Records::Scanned,optional<IO::Records::Stamp>>>&& scanned )ι->void{
		file.Opened = true;
		if( !scanned )
			return;
		auto& [y, stamp] = *scanned;
		file.Size = y.Size;
		file.Chain = y.Chain;
		file.Runs = move( y.Runs );
		file.Stamped = stamp;
		if( y.Keep() )
			file.Refused = "holds damage a truncation would lose, or is another program's";
		else if( y.Start && y.Start->generation() )
			file.Refused = "is an archive";
		if( y.Size<y.FileSize )
			file.Unopened = move( y );
	}

	//The run that writes records to day's file:  a file with nothing kept opens with its FileStart.  Cuts a torn tail the
	//scan found first.  Throws when the file can't be written to, or holds other bytes than the trace wrote, forgetting
	//what it knew so the next write scans it again.
	Ω prepare( TraceStore::Impl& impl, TraceStore::Files& files, Day day, std::span<const Proto::FlowRecord> records, SL sl )ε->Pending{
		let path = files.File( day );
		bool unopened;
		{
			lg _{ files.Mutex };
			unopened = !files.Known.try_emplace( day, DayFile{{.Path=path}} ).first->second.Opened;
		}
		if( unopened ){
			auto scanned = scan( path, sl );
			lg _{ files.Mutex };
			auto& file = files.Known.try_emplace( day, DayFile{{.Path=path}} ).first->second;
			if( !file.Opened )
				apply( file, move(scanned) );
		}
		lg _{ files.Mutex };
		auto& file = files.Known.try_emplace( day, DayFile{{.Path=path}, true} ).first->second;//the roll may have taken it meanwhile:  then as a file never opened, which Verify finds gone.
		std::error_code ec;
		bool restart{};
		if( !file.Refused.empty() ){
			if( fs::exists(file.Path, ec) || ec )
				throw IO::IOException{ sl, file.Path, file.Discarded++ ? ELogLevel::Debug : ELogLevel::Error, "{}, so the trace won't write to it until it is repaired or removed", file.Refused };
			restart = true;//removed since.
		}
		IO::CreateDirsForWrite( path.parent_path(), sl );
		if( file.Unopened && !restart ){
			try{
				IO::Records::Truncate( file.Path, *file.Unopened, sl );
			}
			catch( ... ){//no longer the file that was scanned, perhaps, so the next write scans it again.
				files.Known.erase( day );
				throw;
			}
			file.Unopened.reset();
		}
		IO::Records::Verified verified;
		try{
			verified = IO::Records::Verify( file, false, restart, sl );
		}
		catch( ... ){//other bytes than the trace wrote, so its next write scans it again.
			files.Known.erase( day );
			throw;
		}
		const DayFile fresh;
		let& known = verified.Restart ? fresh : file;
		Pending y{ .Date=day, .Path=path, .Offset=known.Size, .Restart=verified.Restart };
		optional<IO::Records::Opening<Proto::FlowRecord>> opening;
		if( !known.Size ){
			IO::Proto::FileStart first;
			first.set_ts( Ns(Hist::DayStart(day, impl.Zone)) );
			opening.emplace( first, vector<Proto::FlowRecord>{}, vector<Proto::FlowRecord>{} );
		}
		auto built = IO::Records::BuildAppend<Proto::FlowRecord>( known.Size, known.Chain, move(opening), vector<Proto::FlowRecord>{records.begin(), records.end()}, {} );//a copy:  a write that fails puts records back.
		y.End = built.End;
		y.Chain = built.Chain;
		y.Runs = move( built.Runs );
		y.Bytes = move( built.Bytes );
		file.Hand( y.Offset, y.End );
		return y;
	}
	//Once the run's Write has returned.  The process's first append to a file fsyncs its directories, so a power loss can't
	//take back its name;  one that can't is said, with the records written all the same.
	Ω commit( TraceStore::Impl& impl, TraceStore::Files& files, Pending&& run, SL sl )ι->void{
		lg _{ files.Mutex };
		auto& file = files.Known.try_emplace( run.Date, DayFile{{.Path=run.Path}} ).first->second;
		if( run.Restart )
			file = DayFile{ {.Path=run.Path}, true };
		if( run.Restart || !file.Named ){
			try{
				IO::SyncDirs( impl.Settings.Root, fs::path{files.Slug}/DayDirectory(run.Date), sl );
			}
			catch( const IO::IOException& e ){
				e.SetLevel( ELogLevel::Warning );
			}
		}
		file.Committed( run.End, run.Chain, move(run.Runs) );
	}
	//What a read serves of a day's file:  its runs as the process knows them once it has opened the file, else as a scan
	//finds them.  None for a day with no file, or one with nothing kept.  Throws, at Error, when the file can't be opened or
	//scanned.
	Ω serve( TraceStore::Files& files, Day day, SL sl )ε->optional<Served>{
		sp<IO::OpenFile> file;
		try{
			file = ms<IO::OpenFile>( files.File(day), sl );
		}
		catch( IO::IOException& e ){
			if( e.Error!=IO::EIOError::NotFound ){
				e.SetLevel( ELogLevel::Error );
				throw;
			}
			e.SetLevel( ELogLevel::Debug );
			return nullopt;
		}
		Served y;
		bool known{};
		{
			lg _{ files.Mutex };
			if( let p = files.Known.find(day); p!=files.Known.end() && p->second.Opened ){
				known = true;
				y.Runs = p->second.Runs;
			}
		}
		if( !known ){
			try{
				Decoder decoder;
				y.Runs = IO::Records::ScanNow( file, StartField, CheckpointField, decoder, sl ).Runs;
			}
			catch( const IO::IOException& e ){
				e.SetLevel( ELogLevel::Error );
				throw;
			}
		}
		if( y.Runs.empty() )
			return nullopt;
		y.File = move( file );
		return y;
	}
	//Each record of the day's file in time order, until visit answers false.  Throws when a run can't be read through, and
	//when the store stops, so a read off the executor ends with it.
	Ω each( Served&& served, const TraceStore::Impl& impl, SL sl, const std::function<bool( Proto::FlowRecord& )>& visit )ε->void{
		Merge merge{ move(served.File), move(served.Runs), {}, sl };
		Proto::FlowRecord r;
		while( IO::Records::NextNow(merge, r, sl) ){
			if( impl.Stopped )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::ServiceUnavailable}, "The trace's store has stopped." };
			if( !visit(r) )
				return;
		}
	}
	Ω matches( const TraceQuery& query, const Proto::Hop& hop )ι->bool{
		return ( !query.Node || hop.node_id()==*query.Node ) && ( !query.Msg || hop.msg_id()==*query.Msg )
			&& ( !query.Kind || (uint8)hop.kind()==(uint8)*query.Kind ) && hop.run_id()==query.Run;
	}

	TraceStore::TraceStore( TraceSettings settings, sp<Hist::IClock> clock )ι:
		_impl{ mu<Impl>(move(settings), move(clock)) }
	{}
	TraceStore::~TraceStore(){//the timers alone:  a flush in flight holds the store, so none is.  What a buffer holds is Stop's to write.
		lg _{ _impl->Mutex };
		_impl->Stopped = true;
		if( auto timer = std::exchange(_impl->RollTimer, 0); timer )
			_impl->Clock->Cancel( timer );
		for( let& [_, files] : _impl->Flows ){
			lg l{ files->Mutex };
			if( auto timer = std::exchange(files->Timer, 0); timer )
				_impl->Clock->Cancel( timer );
			if( !files->Buffer.empty() )
				WARN( "[{}]{} trace records were not written:  the store was not stopped.", files->Slug, files->Buffer.size() );
		}
	}
	α TraceStore::Settings()Ι->const TraceSettings&{ return _impl->Settings; }
	α TraceStore::Zone()Ι->const time_zone&{ return _impl->Zone; }

	α TraceStore::Start()ι->void{
		lg _{ _impl->Mutex };
		if( _impl->Started || _impl->Stopped )
			return;
		_impl->Started = true;
		_impl->RollTimer = _impl->Clock->Schedule( Duration::zero(), [weak=weak_from_this()]{
			if( auto self = weak.lock() )
				self->Rolling();
		} );
	}
	α TraceStore::Stop( Duration wait )ι->void{
		vector<sp<Files>> flows;
		{
			lg _{ _impl->Mutex };
			if( _impl->Stopped )
				return;
			_impl->Stopped = true;
			if( auto timer = std::exchange(_impl->RollTimer, 0); timer )
				_impl->Clock->Cancel( timer );
			for( let& [_, files] : _impl->Flows )
				flows.push_back( files );
		}
		struct Wait final{ std::mutex Mutex; std::condition_variable Done; uint Left; };
		auto left = ms<Wait>();
		left->Left = flows.size();
		for( let& files : flows ){
			Flush( files->Slug, [left]{
				{
					lg _{ left->Mutex };
					--left->Left;
				}
				left->Done.notify_all();
			} );
		}
		std::unique_lock l{ left->Mutex };
		if( !left->Done.wait_for(l, wait, [&]{ return !left->Left; }) )
			WARN( "{} flows' traces had not been written after {}.", left->Left, Chrono::ToString(wait) );
	}
	α TraceStore::Trace( sv slug, optional<ETraceLevel> level, sp<Hist::IClock> clock, Tap tap, uint32 run )ι->sp<FileTrace>{
		auto files = _impl->Find( slug, true );
		return ms<FileTrace>( shared_from_this(), move(files), level.value_or(_impl->Settings.Level), move(clock), move(tap), run );
	}
	α TraceStore::Buffered( sv slug )Ι->uint{
		let files = _impl->Find( slug, false );
		if( !files )
			return 0;
		lg _{ files->Mutex };
		return files->BufferBytes;
	}

	//The flow's flush, `after` from now, under f.Mutex.
	Ω scheduleFlush( wp<TraceStore> store, TraceStore::Impl& impl, TraceStore::Files& f, Duration after )ι->void{
		f.Timer = impl.Clock->Schedule( after, [store=move(store), slug=f.Slug]{ if( auto self = store.lock() ) self->Flush( slug ); } );
	}
	//The buffer, for a flush to write, under f.Mutex.
	Ω take( TraceStore::Impl& impl, TraceStore::Files& f )ι->vector<Proto::FlowRecord>{
		if( auto timer = std::exchange(f.Timer, 0); timer )
			impl.Clock->Cancel( timer );
		f.Early = false;
		auto y = move( f.Buffer );
		f.Buffer = {};
		f.BufferBytes = 0;
		f.Again = false;
		return y;
	}
	//The flush:  the buffer taken and written a day at a time, each day's records a run on its file.  A day whose write
	//fails puts its records back at the front of the buffer, says so once, and is tried again Delay on, while the other
	//days are written.  What arrived meanwhile waits for the next, Delay on, unless a Flush asked for it.  Whether to go on
	//and the end are one lock's, so a Flush is either taken by this flush or starts the next.
	Ω flushing( sp<TraceStore> self, TraceStore::Impl& impl, sp<TraceStore::Files> files )ι->VoidTask{
		auto& f = *files;
		vector<Proto::FlowRecord> batch;
		{
			lg _{ f.Mutex };
			batch = take( impl, f );
		}
		for( ;; ){
			std::ranges::stable_sort( batch, {}, []( const Proto::FlowRecord& r ){ return Time( r ).value_or( 0 ); } );
			vector<Proto::FlowRecord> kept;//the days whose write failed.
			string error;
			for( uint i=0; i<batch.size(); ){
				let day = dayOf( FromNs(Time(batch[i]).value_or(0)), impl.Zone );
				uint j = i+1;
				while( j<batch.size() && dayOf(FromNs(Time(batch[j]).value_or(0)), impl.Zone)==day )
					++j;
				try{
					auto run = prepare( impl, f, day, std::span<const Proto::FlowRecord>{batch}.subspan(i, j-i), SRCE_CUR );
					co_await run.Write();
					commit( impl, f, move(run), SRCE_CUR );
				}
				catch( const std::exception& e ){
					if( error.empty() )
						error = e.what();
					kept.insert( kept.end(), std::make_move_iterator(batch.begin()+i), std::make_move_iterator(batch.begin()+j) );
				}
				i = j;
			}
			bool first{}, recovered{}, end;
			vector<absl::AnyInvocable<void()>> done;
			{
				lg _{ f.Mutex };
				if( kept.size() ){
					uint bytes{};
					for( let& r : kept )
						bytes += (uint)r.ByteSizeLong();
					f.Buffer.insert( f.Buffer.begin(), std::make_move_iterator(kept.begin()), std::make_move_iterator(kept.end()) );
					f.BufferBytes += bytes;
					first = !std::exchange( f.Failing, true );
				}
				else if( batch.size() ){
					recovered = std::exchange( f.Failing, false );
					f.Dropping = false;
				}
				end = kept.size() || !f.Again;
				if( end ){
					f.Flushing = false;
					done = std::move( f.Done );
					f.Done.clear();
					if( !f.Buffer.empty() && !f.Timer && !f.Purged && !impl.Stopped )
						scheduleFlush( self, impl, f, impl.Settings.Delay );
				}
				else
					batch = take( impl, f );
			}
			if( first )
				ERR( "[{}]The trace could not be written, and is buffered and tried again every {}:  {}", f.Slug, Chrono::ToString(impl.Settings.Delay), error );
			if( recovered )
				INFO( "[{}]The trace is written again.", f.Slug );
			if( end ){
				f.Idle.notify_all();
				for( auto& d : done )
					d();
				co_return;
			}
		}
	}
	α TraceStore::Flush( sv slug, absl::AnyInvocable<void()> done )ι->void{
		auto files = _impl->Find( slug, false );
		if( !files ){
			if( done )
				done();
			return;
		}
		{
			lg _{ files->Mutex };
			if( files->Flushing ){
				files->Again = true;
				if( done )
					files->Done.push_back( std::move(done) );
				return;
			}
			if( files->Buffer.empty() || files->Purged ){
				if( auto timer = std::exchange(files->Timer, 0); timer )
					_impl->Clock->Cancel( timer );
				files->Early = false;
				files->Buffer.clear();
				files->BufferBytes = 0;
			}
			else{
				files->Flushing = true;
				if( done )
					files->Done.push_back( std::move(done) );
				done = nullptr;
			}
		}
		if( done )
			done();
		else
			flushing( shared_from_this(), *_impl, move(files) );
	}
	α TraceStore::Purge( sv slug )ι->void{
		sp<Files> files;
		{
			lg _{ _impl->Mutex };
			if( let p = _impl->Flows.find(slug); p!=_impl->Flows.end() ){
				files = p->second;
				_impl->Flows.erase( p );
			}
		}
		if( files ){
			std::unique_lock l{ files->Mutex };
			files->Purged = true;
			if( auto timer = std::exchange(files->Timer, 0); timer )
				_impl->Clock->Cancel( timer );
			files->Buffer.clear();
			files->BufferBytes = 0;
			if( !files->Idle.wait_for(l, 10s, [&]{ return !files->Flushing; }) )
				WARN( "[{}]The trace's flush had not finished after 10 s:  its directory is removed under it.", slug );
			files->Known.clear();
		}
		Remove( _impl->Settings.Root, slug );//one traced by an earlier process too, and after Stop.
	}
	α TraceStore::Remove( const fs::path& root, sv slug )ι->void{
		if( !Impl::DirectoryName(slug) ){
			ERR( "[{}]The flow's slug is no directory name:  no trace directory is removed for it.", slug );
			return;
		}
		std::error_code ec;
		if( fs::remove_all(root/slug, ec); ec )
			WARN( "[{}]The flow's trace directory wasn't removed with it:  {}", slug, ec.message() );
	}
	α TraceStore::Days( sv slug, SL sl )ε->vector<Day>{
		return days( _impl->Settings.Root/slug, sl );
	}

	//The daily roll:  every flow's days newest first, each kept while it is within Retention and, with the days before it,
	//within MaxSize;  the first past either and every day older go, across the flows, with the directories they empty.  A
	//day whose file can't be measured is kept and not counted.  What the process knew of a removed file goes with it, so a
	//flush to that day makes the file again.
	α TraceStore::Roll()ι->void{
		let& settings = _impl->Settings;
		let now = _impl->Clock->Now();
		let cutoff = dayOf( now-settings.Retention, _impl->Zone );//a day before it is older than Retention.
		vector<std::pair<Day,string>> all;
		std::error_code ec;
		for( fs::directory_iterator p{settings.Root, ec}, end; !ec && p!=end; p.increment(ec) ){
			if( !p->is_directory(ec) )
				continue;
			let slug = p->path().filename().string();
			try{
				IO::Records::Walk( p->path(), SRCE_CUR, [&]( Day day ){ all.emplace_back( day, slug ); return false; } );
			}
			catch( const std::exception& e ){
				WARN( "[{}]The flow's trace days could not be listed, so none is removed:  {}", slug, e.what() );
			}
		}
		if( ec )
			WARN( "The trace directory '{}' could not be listed, so no day is removed:  {}", settings.Root.string(), ec.message() );
		std::ranges::stable_sort( all, []( let& a, let& b ){ return a.first>b.first; } );
		uint total{};
		bool over{};
		uint removed{};
		for( let& [day, slug] : all ){
			let path = settings.Root/slug/DayDirectory( day )/TraceFileName;
			if( !over ){
				if( day<cutoff )
					over = true;
				else{
					let size = fs::file_size( path, ec );
					if( ec ){//gone since the walk, or not to be measured:  a day the roll can't see is kept, not taken for one past the cap.
						if( ec!=std::errc::no_such_file_or_directory )
							WARN( "[{}]{} could not be measured at the roll, so it stays:  {}", slug, path.string(), ec.message() );
						continue;
					}
					if( total+size>settings.MaxSize )
						over = true;
					else
						total += size;
				}
			}
			if( !over )
				continue;
			if( auto files = _impl->Find(slug, false); files ){
				lg _{ files->Mutex };
				files->Known.erase( day );
			}
			if( fs::remove(path, ec) )
				++removed;
			else if( ec )
				WARN( "[{}]{} wasn't removed at the roll:  {}", slug, path.string(), ec.message() );
			for( auto dir = path.parent_path(); dir!=settings.Root/slug && fs::is_empty(dir, ec) && !ec; dir = dir.parent_path() )
				fs::remove( dir, ec );
		}
		if( removed )
			INFO( "The roll removed {} trace days:  {} bytes stay over the days kept.", removed, total );
	}
	α TraceStore::Rolling()ι->void{
		Roll();
		lg _{ _impl->Mutex };
		if( _impl->Stopped )
			return;
		_impl->RollTimer = _impl->Clock->Schedule( Hist::NextDayStart(_impl->Clock->Now(), _impl->Zone), [weak=weak_from_this()]{
			if( auto self = weak.lock() )
				self->Rolling();
		} );
	}

	α TraceStore::Read( sv slug, const TraceQuery& query, SL sl )ε->TracePage{
		TracePage y;
		auto files = _impl->Find( slug, true );
		if( !files )
			return y;
		let reverse = query.Reverse();
		let lo = query.Start && query.End ? optional<TimePoint>{ std::min(*query.Start, *query.End) } : query.Start;
		let hi = query.Start && query.End ? optional<TimePoint>{ std::max(*query.Start, *query.End) } : query.End;
		let inRange = [&]( TimePoint t ){ return (!lo || t>=*lo) && (!hi || t<=*hi); };
		let past = [&]( TimePoint t ){ return hi && t>*hi; };//nothing after it is in the range.
		auto all = days( files->Dir, sl );//oldest first.
		let& from = query.From;
		std::erase_if( all, [&]( Day day ){
			return ( lo && day<dayOf(*lo, _impl->Zone) ) || ( hi && day>dayOf(*hi, _impl->Zone) )
				|| ( from && (reverse ? day>dayOf(from->Time, _impl->Zone) : day<dayOf(from->Time, _impl->Zone)) );
		} );
		if( reverse )
			std::ranges::reverse( all );
		let limit = std::max<uint>( query.Limit, 1 );
		bool more{};
		for( uint d=0; d<all.size() && y.Records.size()<limit; ++d ){
			auto served = serve( *files, all[d], sl );
			if( !served )
				continue;
			let cursorDay = from && dayOf( from->Time, _impl->Zone )==all[d];
			if( !reverse ){
				uint skipped{};
				each( move(*served), *_impl, sl, [&]( Proto::FlowRecord& r ){
					if( !r.has_hop() || !matches(query, r.hop()) )
						return true;
					let t = FromNs( r.hop().time() );
					if( !inRange(t) )
						return !past( t );
					if( cursorDay && ( t<from->Time || (t==from->Time && skipped++<from->Skip) ) )
						return true;
					if( y.Records.size()==limit ){
						more = true;
						return false;
					}
					y.Records.push_back( FromProto(r.hop()) );
					return true;
				} );
			}
			else{
				let need = limit-y.Records.size();
				let cap = need+( cursorDay ? from->Skip : 0 );
				std::deque<TraceRecord> ring;
				bool overflow{};
				each( move(*served), *_impl, sl, [&]( Proto::FlowRecord& r ){
					if( !r.has_hop() || !matches(query, r.hop()) )
						return true;
					let t = FromNs( r.hop().time() );
					if( !inRange(t) )
						return !past( t );
					if( cursorDay && t>from->Time )
						return false;
					ring.push_back( FromProto(r.hop()) );
					if( ring.size()>cap ){
						ring.pop_front();
						overflow = true;
					}
					return true;
				} );
				if( cursorDay ){
					for( uint skipped=0; skipped<from->Skip && !ring.empty() && ring.back().Time==from->Time; ++skipped )
						ring.pop_back();
				}
				let taken = std::min<uint>( need, ring.size() );
				for( uint i=0; i<taken; ++i )
					y.Records.push_back( move(ring[ring.size()-1-i]) );
				if( taken==need && (overflow || ring.size()>taken) )
					more = true;
			}
			if( y.Records.size()==limit && !more && d+1<all.size() )
				more = true;
		}
		if( more ){
			let& last = y.Records.back();
			uint32 skip = from && from->Time==last.Time ? from->Skip : 0;
			for( let& r : y.Records | std::views::reverse ){
				if( r.Time!=last.Time )
					break;
				++skip;
			}
			y.Next = TraceCursor{ last.Time, skip };
		}
		return y;
	}

	α TraceStore::Lineage( sv slug, MessageId id, optional<TimePoint> start, optional<TimePoint> end, uint limit, uint32 run, SL sl )ε->TraceLineage{
		TraceLineage y;
		auto files = _impl->Find( slug, true );
		if( !files || !id )
			return y;
		let lo = dayOf( start.value_or(secondsOf(id)), _impl->Zone );
		let hi = dayOf( end.value_or(_impl->Clock->Now()), _impl->Zone );
		auto all = days( files->Dir, sl );
		std::erase_if( all, [&]( Day day ){ return day<lo || day>hi; } );
		struct Found final{ TraceRecord Record; uint Day; uint Ordinal; };
		vector<Found> found;
		limit = std::max<uint>( limit, 1 );
		//One pass over the days:  the records of the messages in `set`, and with `grow` of each message made from one, from
		//its first record on;  none after `until`.  Whether the limit cut it short.
		let pass = [&]( flat_set<MessageId>& set, bool grow, optional<TimePoint> until )ε->bool{
			bool past{};
			for( uint d=0; d<all.size() && !past; ++d ){
				if( until && all[d]>dayOf(*until, _impl->Zone) )
					break;
				auto served = serve( *files, all[d], sl );
				if( !served )
					continue;
				uint ordinal{};
				bool cut{};
				each( move(*served), *_impl, sl, [&]( Proto::FlowRecord& r ){
					if( !r.has_hop() )
						return true;
					++ordinal;
					let& hop = r.hop();
					if( hop.run_id()!=run )
						return true;
					if( until && FromNs(hop.time())>*until ){
						past = true;
						return false;
					}
					let own = set.contains( hop.msg_id() );
					if( !own && !(grow && hop.parent_id() && set.contains(hop.parent_id())) )
						return true;
					if( found.size()>=limit ){
						cut = true;
						return false;
					}
					if( !own )
						set.insert( hop.msg_id() );
					found.push_back( {FromProto(hop), d, ordinal} );
					return true;
				} );
				if( cut )
					return true;
			}
			return false;
		};
		flat_set<MessageId> own{ id };
		y.More = pass( own, true, nullopt );
		//The message a pass found first:  its parent, and the time of its first record, past which the parent's pass ends.
		optional<MessageId> parent;
		optional<TimePoint> born;
		for( let& f : found ){
			if( f.Record.Id==id ){
				parent = f.Record.Parent;
				born = f.Record.Time;
				break;
			}
		}
		for( uint up=0; !y.More && parent && *parent && up<_maxAncestors; ++up ){
			flat_set<MessageId> ancestor{ *parent };
			let before = found.size();
			y.More = pass( ancestor, false, born );
			parent.reset();
			for( uint i=before; i<found.size(); ++i ){
				if( found[i].Record.Id==*ancestor.begin() ){
					parent = found[i].Record.Parent;
					born = found[i].Record.Time;
					break;
				}
			}
		}
		std::ranges::sort( found, []( let& a, let& b ){ return a.Day!=b.Day ? a.Day<b.Day : a.Ordinal<b.Ordinal; } );
		y.Records.reserve( found.size() );
		for( auto& f : found )
			y.Records.push_back( move(f.Record) );
		return y;
	}

	α TraceStore::Counts( sv slug, TimePoint start, TimePoint end, Duration by, uint limit, optional<TimePoint> from, uint32 run, SL sl )ε->CountPage{
		CountPage y;
		auto files = _impl->Find( slug, true );
		if( !files || end<=start )
			return y;
		by = std::max<Duration>( by, minutes{1} );
		let lower = from ? std::max( *from, start ) : start;
		auto all = days( files->Dir, sl );
		let lo = dayOf( lower, _impl->Zone ), hi = dayOf( end, _impl->Zone );
		std::erase_if( all, [&]( Day day ){ return day<lo || day>hi; } );
		//A bucket at a time, since the records come in time order:  its rows by the node's first appearance, into the page
		//once a later bucket's record shows it whole, unless the page has no room for it.
		optional<uint> bucket;
		vector<NodeCounts> rows;
		flat_map<string,uint> index;
		let close = [&]()ι->bool{
			if( limit && y.Rows.size() && y.Rows.size()+rows.size()>limit ){
				y.Next = start+by*(int64_t)*bucket;
				return false;
			}
			for( auto& row : rows )
				y.Rows.push_back( {start+by*(int64_t)*bucket, move(row)} );
			rows.clear();
			index.clear();
			return true;
		};
		bool done{};
		for( uint d=0; d<all.size() && !done; ++d ){
			auto served = serve( *files, all[d], sl );
			if( !served )
				continue;
			each( move(*served), *_impl, sl, [&]( Proto::FlowRecord& r ){
				if( !r.has_counts() || r.counts().run_id()!=run )
					return true;
				let t = FromNs( r.counts().time() );
				if( t<lower )
					return true;
				if( t>=end ){
					done = true;
					return false;
				}
				let b = (uint)( (t-start)/by );
				if( bucket && b!=*bucket && !close() ){
					done = true;
					return false;
				}
				bucket = b;
				let p = index.try_emplace( r.counts().node_id(), rows.size() ).first->second;
				if( p==rows.size() )
					rows.push_back( NodeCounts{r.counts().node_id()} );
				auto& counts = rows[p];
				let row = FromProto( r.counts() );
				counts.Received += row.Counts.Received;
				counts.Drops += row.Counts.Drops;
				counts.Errors += row.Counts.Errors;
				if( counts.Sent.size()<row.Counts.Sent.size() )
					counts.Sent.resize( row.Counts.Sent.size() );
				for( uint i=0; i<row.Counts.Sent.size(); ++i )
					counts.Sent[i] += row.Counts.Sent[i];
				return true;
			} );
		}
		if( !y.Next && rows.size() )
			close();
		return y;
	}

	//A record into the flow's buffer, which its flush takes Delay after the first.  Past the buffer's cap a healthy file's
	//flush comes now;  while the file is unwritable, or a flush is behind by the cap, the oldest go, said once per outage.
	Ω add( TraceStore& store, TraceStore::Impl& impl, TraceStore::Files& files, Proto::FlowRecord&& r )ι->void{
		bool dropping{};
		{
			lg _{ files.Mutex };
			if( files.Purged )
				return;
			files.BufferBytes += (uint)r.ByteSizeLong();
			files.Buffer.push_back( move(r) );
			if( files.BufferBytes>_maxBuffer ){
				if( files.Failing || files.Flushing || impl.Stopped ){
					uint dropped{};
					auto p = files.Buffer.begin();
					for( ; p!=files.Buffer.end() && files.BufferBytes-dropped>_maxBuffer/2; ++p )
						dropped += (uint)p->ByteSizeLong();
					files.Buffer.erase( files.Buffer.begin(), p );
					files.BufferBytes -= dropped;
					dropping = !std::exchange( files.Dropping, true );
				}
				else if( !files.Early ){
					if( auto timer = std::exchange(files.Timer, 0); timer )
						impl.Clock->Cancel( timer );
					files.Early = true;
					scheduleFlush( store.weak_from_this(), impl, files, Duration::zero() );
				}
			}
			if( !files.Timer && !files.Flushing && !impl.Stopped )
				scheduleFlush( store.weak_from_this(), impl, files, impl.Settings.Delay );
		}
		if( dropping )
			WARN( "[{}]The trace's buffer passed its {} byte cap while its file was unwritable or behind:  the oldest records are dropped.", files.Slug, _maxBuffer );
	}

	FileTrace::FileTrace( sp<TraceStore> store, sp<TraceStore::Files> files, ETraceLevel level, sp<Hist::IClock> clock, TraceStore::Tap tap, uint32 run )ι:
		HopTrace{ level, move(clock) },
		_store{ move(store) },
		_files{ move(files) },
		_tap{ move(tap) },
		_run{ run }
	{}
	FileTrace::~FileTrace()=default;
	α FileTrace::Write( TraceRecord&& record )ι->void{
		record.Run = _run;
		if( _tap )
			_tap( record );
		if( _files )
			add( *_store, *_store->_impl, *_files, ToProto(record, _store->_impl->Settings.ValueLimit) );
	}
	α FileTrace::WriteMinute( const MinuteCounts& minute )ι->void{
		if( !_files )
			return;
		for( let& node : minute.Nodes )
			add( *_store, *_store->_impl, *_files, ToProto(minute.Minute, node, _run) );
	}

	Ω convert( Proto::FlowRecord& r, int64_t& last, bool toDisk )ι->void{
		let primary = [&]( int64_t stored )ι->int64_t{
			if( !toDisk )
				return last = (int64_t)( (uint64_t)last+(uint64_t)stored );//wrapping, so a garbled time read from disk can't overflow.
			let delta = (int64_t)( (uint64_t)stored-(uint64_t)last );
			last = stored;
			return delta;
		};
		switch( r.record_case() ){
		case Proto::FlowRecord::kFileStart:
			last = r.file_start().ts();
			break;
		case Proto::FlowRecord::kHop:
			r.mutable_hop()->set_time( primary(r.hop().time()) );
			break;
		case Proto::FlowRecord::kCounts:
			r.mutable_counts()->set_time( primary(r.counts().time()) );
			break;
		default:
			break;
		}
	}
}
namespace Jde::Opc{
	α Flows::Time( const Proto::FlowRecord& r )ι->optional<int64_t>{
		switch( r.record_case() ){
		case Proto::FlowRecord::kHop: return r.hop().time();
		case Proto::FlowRecord::kCounts: return r.counts().time();
		default: return nullopt;
		}
	}
	α Flows::CutJson( sv json, uint limit )ι->sv{
		if( !limit || json.size()<=limit )
			return json;
		auto end = limit;
		while( end && ((unsigned char)json[end] & 0xC0)==0x80 )
			--end;
		return json.substr( 0, end );
	}
	α Flows::ToDisk( Proto::FlowRecord& r, int64_t& last )ι->void{ convert( r, last, true ); }
	α Flows::ToMemory( Proto::FlowRecord& r, int64_t& last )ι->void{ convert( r, last, false ); }

	α Flows::ToProto( const TraceRecord& r, uint valueLimit )ι->Proto::FlowRecord{
		Proto::FlowRecord y;
		auto& hop = *y.mutable_hop();
		hop.set_time( Ns(r.Time) );
		hop.set_msg_id( r.Id );
		hop.set_parent_id( r.Parent );
		hop.set_run_id( r.Run );
		hop.set_node_id( r.Node );
		hop.set_kind( (Proto::Kind)r.Kind );
		for( let port : r.Ports )
			hop.add_ports( (uint32_t)port );
		if( r.Json ){
			let kept = CutJson( *r.Json, valueLimit );
			hop.set_json( string{kept} );
			hop.set_cut( r.Cut || kept.size()<r.Json->size() );
		}
		if( r.Error.size() )
			hop.set_error( r.Error );
		if( r.Status )
			*hop.mutable_status() = toProtoStatus( *r.Status );
		for( let& block : r.Blocks )
			hop.add_blocks( block );
		return y;
	}
	α Flows::ToProto( TimePoint minute, const NodeCounts& node, uint32 run )ι->Proto::FlowRecord{
		Proto::FlowRecord y;
		auto& counts = *y.mutable_counts();
		counts.set_time( Ns(minute) );
		counts.set_node_id( node.Node );
		counts.set_received( (uint32_t)node.Received );
		for( let sent : node.Sent )
			counts.add_sent( (uint32_t)sent );
		counts.set_drops( (uint32_t)node.Drops );
		counts.set_errors( (uint32_t)node.Errors );
		counts.set_run_id( (uint32_t)run );
		return y;
	}
	α Flows::FromProto( const Proto::Hop& hop )ι->TraceRecord{
		TraceRecord y{ .Time=FromNs(hop.time()), .Id=hop.msg_id(), .Parent=hop.parent_id(), .Node=hop.node_id(), .Kind=(ERecordKind)hop.kind(), .Run=hop.run_id(), .Cut=hop.cut() };
		for( let port : hop.ports() )
			y.Ports.push_back( port );
		if( hop.has_json() )
			y.Json = hop.json();
		y.Error = hop.error();
		if( hop.has_status() )
			y.Status = NodeStatus{ hop.status().fill(), hop.status().shape(), hop.status().text() };
		for( let& block : hop.blocks() )
			y.Blocks.push_back( block );
		return y;
	}
	α Flows::FromProto( const Proto::Counts& counts )ι->CountRow{
		CountRow y{ FromNs(counts.time()), NodeCounts{counts.node_id(), counts.received(), {}, counts.drops(), counts.errors()} };
		for( let sent : counts.sent() )
			y.Counts.Sent.push_back( sent );
		return y;
	}
}