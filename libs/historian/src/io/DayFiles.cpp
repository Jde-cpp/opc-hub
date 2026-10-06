#include "DayFiles.h"
#include <charconv>
#include <fstream>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/opc/proto/opc.Common.h>
#include "File.h"

#define let const auto

namespace Jde::Opc::Hist{
	using namespace std::chrono;
	using Proto::HistoryRecord;
	constexpr ELogTags _tags{ ELogTags::IO };

	namespace{
		constexpr Ticks PerSecond{ 10'000'000 };
		constexpr int64_t EpochSeconds{ UA_DATETIME_UNIX_EPOCH/PerSecond };//1601 to 1970.
		constexpr Ticks Latest{ (sys_days{year{9999}/December/31}.time_since_epoch()/1s+86'399+EpochSeconds)*PerSecond };

		//A rewrite's temp file, but nothing else at its path:  a directory or a link there isn't one a rewrite left, and
		//stays, for the rewrite's open to fail on and say.
		Ω removeTemp( const fs::path& temp )ι->bool{
			std::error_code ec;
			return fs::symlink_status( temp, ec ).type()==fs::file_type::regular && fs::remove( temp, ec );
		}
		//None for a file that isn't there, or can't be stat'ed.
		Ω stamp( const fs::path& path )ι->optional<Stamp>{
			std::error_code ec;
			let size = fs::file_size( path, ec );
			if( ec )
				return nullopt;
			let written = fs::last_write_time( path, ec );
			return ec ? nullopt : optional<Stamp>{ Stamp{size, written} };
		}
		//dir's subdirectories named as DayDirectory names them, a number from min to max with no padding, largest first:  so
		//the path the walk rebuilds from one is the directory's, and the number fits chrono's year, month or day.
		Ω numbered( const fs::path& dir, unsigned min, unsigned max, SL sl )ε->vector<unsigned>{
			vector<unsigned> y;
			std::error_code ec;
			for( fs::directory_iterator p{dir, ec}, end; !ec && p!=end; p.increment(ec) ){
				let name = p->path().filename().string();
				unsigned n{};
				let [last, error] = std::from_chars( name.data(), name.data()+name.size(), n );
				if( error==std::errc{} && last==name.data()+name.size() && n>=min && n<=max && std::to_string(n)==name && p->is_directory(ec) )
					y.push_back( n );
			}
			if( ec )
				throw Failed( dir, ec, sl );
			std::ranges::sort( y, std::greater{} );
			return y;
		}
		//Each day that has a directory under root, newest first, until visit returns true.
		Ω walk( const fs::path& root, SL sl, const std::function<bool( Day )>& visit )ε->void{
			for( let y : numbered(root, 1601, 9999, sl) ){//DayOf's range.
				let yearDir = root/std::to_string( y );
				for( let m : numbered(yearDir, 1, 12, sl) ){
					for( let d : numbered(yearDir/std::to_string(m), 1, 31, sl) ){
						const Day day{ year{(int)y}, month{m}, std::chrono::day{d} };
						if( day.ok() && visit(day) )
							return;
					}
				}
			}
		}
		constexpr uint PartBytes{ 1<<20 };//what a rewrite writes at a time.
	}

	//Clock.cpp's rules, which its tests cover, for UA's ticks.
	α DayOf( Ticks t, const time_zone& tz )ι->Day{
		return DayOf( TimePoint{sys_seconds{seconds{std::clamp<Ticks>(t, 0, Latest)/PerSecond-EpochSeconds}}}, tz );
	}
	α Fileable( Ticks t )ι->bool{ return t>=0 && t<Latest+PerSecond; }
	α StartOf( Day day, const time_zone& tz )ι->Ticks{
		return ( duration_cast<seconds>(DayStart(day, tz).time_since_epoch()).count()+EpochSeconds )*PerSecond;
	}
	α DayDirectory( Day day )ι->fs::path{
		return fs::path{ std::to_string((int)day.year()) }/std::to_string( (unsigned)day.month() )/std::to_string( (unsigned)day.day() );
	}

	GroupFiles::GroupFiles( fs::path root, string name, const time_zone& tz, Duration delay, Day today, SL sl )ε:
		_root{ move(root) },
		_name{ move(name) },
		_tz{ tz },
		_delay{ delay },
		_flushed{ _root/(_name+".flushed"), sl },
		_present{ today }{
		_restored.Flushed = _flushed.Time();
		//The midnight before the last flush may have had its rewrite cut short, in the `delay` after it, and none since has
		//had one, and an older one's rewrite may have kept failing:  so each file still live from the day .flushed names
		//on is rewritten, but for today's.
		optional<Day> from;
		if( _restored.Flushed ){
			Present( DayOf(UADateTime{*_restored.Flushed}.UA(), _tz) );
			from = _flushed.Recover();
		}
		//The newest file that holds anything says what the group is:  one a crash cut short in its preamble holds nothing.
		//Each node's newest value is in any file down to the first that isn't after the present, since a future-dated file's
		//preamble holds the start values of when it was made.  Nothing older is read, once those are:  settled.
		bool restored{}, settled{};
		flat_map<NodeIndex,ExNodeId> members;
		walk( _root, sl, [&]( Day day ){
			if( settled && (!from || day<*from) )
				return true;
			std::error_code ec;
			if( removeTemp(Temp(day)) )
				WARN( "Removed '{}', which a rewrite that didn't finish left.", Temp(day).string() );
			let path = File( day );
			let there = fs::exists( path, ec );
			if( ec && !settled )//it may be the newest.
				throw Failed( path, ec, sl );
			if( !there && !ec )
				return false;
			if( !settled ){
				_days.insert( day );
				let& file = restored ? Open( day, sl, [&]( HistoryRecord& r ){ Fold(r); } ) : Open( day, sl, [&]( HistoryRecord& r ){ Restore(r, members); } );
				restored = restored || file.Size>0;
				settled = restored && day<=_present;
				if( day<today && file.Size && !file.Generation && file.Refused.empty() && (!from || day>=*from) )
					_recover.insert( day );
			}
			else if( day<today ){
				//Only for its rewrite, so one that can't be read now is taken as live:  its rewrite finds out, and .flushed
				//keeps the day until then.
				bool live{ true };
				try{
					if( ec )
						throw Failed( path, ec, sl );
					let start = ReadStart( path, sl );
					live = start && !start->generation();
				}
				catch( const IO::IOException& ){}//said as it goes.
				if( live ){
					_unarchived.insert( day );
					_recover.insert( day );
				}
			}
			else if( ec )
				Failed( path, ec, sl ).Log();
			return false;
		});
		std::erase_if( _files, []( let& file ){ return file.second.Generation!=0; } );
		//By node, in one sort rather than an insert each:  of two indexes a node has, the later, as an insert each kept.
		vector<std::pair<ExNodeId,NodeIndex>> byNode;
		byNode.reserve( members.size() );
		for( auto&& [index,node] : members )
			byNode.emplace_back( move(node), index );
		std::ranges::sort( byNode, []( let& a, let& b ){ return a.first<b.first || (!(b.first<a.first) && a.second>b.second); } );
		let [last,end] = std::ranges::unique( byNode, {}, &std::pair<ExNodeId,NodeIndex>::first );
		byNode.erase( last, end );
		vector<ExNodeId> nodes;
		vector<NodeIndex> indexes;
		nodes.reserve( byNode.size() );
		indexes.reserve( byNode.size() );
		for( auto&& [node,index] : byNode ){
			nodes.push_back( move(node) );
			indexes.push_back( index );
		}
		_restored.Members = { std::sorted_unique, move(nodes), move(indexes) };
	}

	α GroupFiles::Restore( HistoryRecord& r, flat_map<NodeIndex,ExNodeId>& members )ι->void{
		auto& next = _restored.NextIndex;
		switch( r.record_case() ){
		case HistoryRecord::kFileStart:
			next = std::max<NodeIndex>( next, r.file_start().next_node_index() );
			break;
		case HistoryRecord::kNodeAdded:{
			let index = r.node_added().node_index();
			members.insert_or_assign( index, ProtoUtils::ToExNodeId(r.node_added().node()) );
			next = std::max<NodeIndex>( next, (NodeIndex)index+1 );//one issued after the file's FileStart was written.
			break;}
		case HistoryRecord::kNodeRemoved:
			members.erase( r.node_removed().node_index() );
			break;
		default:
			break;
		}
		Fold( r );
	}
	α GroupFiles::Fold( HistoryRecord& r )ι->void{
		if( r.has_value() )
			Newer( move(*r.mutable_value()) );
		else if( r.has_node_added() && r.node_added().has_start() ){
			auto& added = *r.mutable_node_added();
			added.mutable_start()->set_node_index( added.node_index() );
			Newer( move(*added.mutable_start()) );
		}
	}

	α GroupFiles::Newer( Proto::DataValue&& stored )ι->void{
		let t = PrimaryTime( stored );
		if( !t )
			return;
		auto [p, inserted] = _last.try_emplace( stored.node_index() );
		if( inserted || *t>=PrimaryTime(p->second) )
			p->second = move( stored );
	}

	//The first time the process opens the file:  its scan, and one pass over what the scan kept for the indexes it maps.
	α GroupFiles::Open( Day day, SL sl, const std::function<void( HistoryRecord& )>& restore )ε->DayFile&{
		if( auto p = _files.find(day); p!=_files.end() )
			return p->second;
		DayFile file{ .Path=File(day) };
		std::error_code ec;
		if( fs::exists(file.Path, ec) ){
			file.Scanned = stamp( file.Path );//before the scan, so a change during it shows next time.
			//One read:  the scan hands over each record it keeps, for the indexes the file maps and the restore.
			auto scanned = Scan( file.Path, [&]( HistoryRecord& r ){
				if( r.has_node_added() )
					file.Mapped.insert( r.node_added().node_index() );
				if( restore )
					restore( r );
			}, sl );
			file.Size = scanned.Size;
			file.Chain = scanned.Chain;
			file.Runs = move( scanned.Runs );
			if( scanned.Keep() )
				file.Refused = "holds damage a truncation would lose, or is another program's";
			else if( scanned.Start && scanned.Start->generation() ){
				file.Generation = scanned.Start->generation();
				if( scanned.Stop!=EStop::End ){//an archive is whole, so one that can't be read through is damaged.
					if( restore )
						throw IO::IOException{ sl, file.Path, ELogLevel::Error, "reads {} at byte {}, short of the {} bytes its archive holds", ToString(scanned.Stop), scanned.StopOffset, scanned.Size };
					file.Refused = Ƒ( "is an archive that reads {} at byte {}", ToString(scanned.Stop), scanned.StopOffset );
				}
			}
			if( scanned.Size<scanned.FileSize )
				file.Unopened = move( scanned );
		}
		else if( ec )
			throw Failed( file.Path, ec, sl );
		if( file.Size && !file.Generation && file.Refused.empty() )
			_unarchived.insert( day );
		return _files.emplace( day, move(file) ).first->second;
	}

	α GroupFiles::Added( NodeIndex index, const ExNodeId& node, Ticks start )Ι->HistoryRecord{
		HistoryRecord y;
		auto& added = *y.mutable_node_added();
		added.set_node_index( (uint32_t)index );
		*added.mutable_node() = ProtoUtils::ToExNodeId( node );
		added.set_ts( start );
		//The walk back to the last record before the day, for a value that isn't, is step 5's.
		if( auto p = _last.find(index); p!=_last.end() && *PrimaryTime(p->second)<start ){
			*added.mutable_start() = p->second;
			added.mutable_start()->clear_node_index();
		}
		return y;
	}

	struct Rewrite::State final{
		State( sp<ReadHandle> file, vector<Run> runs, vector<HistoryRecord> late, Proto::FileStart start, absl::flat_hash_set<NodeIndex> mapped, SL sl )ε:
			Records{ move(file), move(runs), move(late), sl },
			Start{ move(start) },
			Mapped{ std::move(mapped) }
		{}
		//Onto the part.  One too large for a file is left out, as an append leaves it.
		α Add( HistoryRecord&& r )ε->void{
			try{
				Writer.Add( move(r) );
			}
			catch( Exception& e ){
				e.SetLevel( ELogLevel::Error );
			}
		}
		Merge Records;
		Proto::FileStart Start;
		absl::flat_hash_set<NodeIndex> Mapped;//the file's and the flush's.
		uint Size{};//written so far.
		string Part;
		Appender Writer{ Part, 0 };//never sealed:  an archive carries no checkpoint.
		optional<HistoryRecord> Ahead;//the record after the part's last, so the last part is known as it is made.
		bool Opened{};
		bool Written{};//the temp file is made.
		bool Last{};
	};
	Rewrite::Rewrite( Day date, fs::path path, fs::path temp, vector<Proto::DataValue> stored, up<State> state )ι:
		Date{ date },
		Path{ move(path) },
		Temp{ move(temp) },
		Stored{ move(stored) },
		_state{ move(state) }
	{}
	Rewrite::Rewrite( Rewrite&& )ι=default;
	α Rewrite::operator=( Rewrite&& )ι->Rewrite& =default;
	Rewrite::~Rewrite()=default;
	α Rewrite::Unreadable()Ι->bool{ return _state && _state->Records.Unreadable(); }
	α Rewrite::Archive()ι->DayFile{
		return DayFile{ .Path=Path, .Generation=_state->Start.generation(), .Size=_state->Size, .Mapped=std::move(_state->Mapped), .Named=true };
	}

	α Rewrite::Next()ε->bool{
		auto& s = *_state;
		if( s.Last )
			return false;
		HistoryRecord r;
		if( !std::exchange(s.Opened, true) ){
			let start = s.Start.ts();
			HistoryRecord first;
			*first.mutable_file_start() = s.Start;
			s.Add( move(first) );
			//The preamble is every record at the day's start.  A member's later preamble record there is a corrected start
			//value, which a reader of the live file took in its place:  it goes into the first.  One after the member's
			//NodeRemoved there is a re-add, which stays.
			vector<HistoryRecord> head;
			absl::flat_hash_map<NodeIndex,uint> members;
			while( s.Records.Next(r) ){
				if( PrimaryTime(r)!=start ){
					s.Ahead = move( r );
					break;
				}
				if( r.has_node_removed() )
					members.erase( r.node_removed().node_index() );
				else if( r.has_node_added() && Bare(r.node_added()) ){
					let [p, added] = members.try_emplace( r.node_added().node_index(), head.size() );
					if( !added ){
						auto& kept = *head[p->second].mutable_node_added();
						if( r.node_added().has_start() )
							*kept.mutable_start() = move( *r.mutable_node_added()->mutable_start() );
						else
							kept.clear_start();
						continue;
					}
				}
				head.push_back( move(r) );
			}
			for( auto& record : head )
				s.Add( move(record) );
		}
		while( s.Ahead ){
			s.Add( move(*s.Ahead) );
			if( s.Records.Next(r) )
				s.Ahead = move( r );
			else
				s.Ahead.reset();
			if( s.Part.size()>=PartBytes && s.Ahead )
				break;
		}
		s.Last = !s.Ahead;
		return true;
	}
	α Rewrite::Write( SL sl )ι->IO::WriteAwait{
		auto& s = *_state;
		let first = !std::exchange( s.Written, true );
		s.Size += s.Part.size();
		return IO::WriteAwait{ Temp, std::exchange(s.Part, {}), IO::WriteOptions{.Create=first, .Mode=first ? IO::EWriteMode::Truncate : IO::EWriteMode::Append, .Sync=s.Last}, sl };
	}

	α GroupFiles::Prepare( Day day, vector<HistoryRecord>&& records, const Membership& members, TimePoint now, SL sl )ε->DayWrite{
		if( auto p = _files.find(day); p!=_files.end() && (p->second.Generation || !p->second.Refused.empty()) ){
			if( let changed = stamp(p->second.Path); changed && changed!=p->second.Scanned )
				_files.erase( p );//changed since its scan, or its rewrite, repaired perhaps, so Open scans it again.
		}
		auto& file = Open( day, sl );
		std::error_code ec;
		bool restart{};//Commit's to apply, so a write that fails leaves what is known of the file.
		if( !file.Refused.empty() ){
			if( fs::exists(file.Path, ec) || ec ){
				_recover.erase( day );
				_unarchived.erase( day );
				if( !records.empty() && !file.Discarded++ )
					ERR( "'{}' {}, so the historian won't write to it:  its {} records, and any after them, are dropped until it is repaired or removed.", file.Path.string(), file.Refused, records.size() );
				return {};
			}
			restart = true;//removed since.
		}
		MakeDirectories( file.Path.parent_path(), sl );
		let rewrite = ( file.Generation && !restart ) || Past( day, now ) || _recover.contains( day ) || _retired;
		if( file.Unopened && !restart && !rewrite ){
			try{
				Truncate( file.Path, *file.Unopened, sl );
			}
			catch( ... ){//no longer the file that was scanned, perhaps, so the next append scans it again.
				_files.erase( day );
				throw;
			}
			file.Unopened.reset();
		}
		let existed = fs::exists( file.Path, ec );
		let actual = existed && !ec ? fs::file_size( file.Path, ec ) : 0;
		if( ec )
			throw Failed( file.Path, ec, sl );
		let size = restart ? 0 : file.Size;
		let& outstanding = file.Outstanding;
		//A rewrite cuts no torn tail:  the file its scan found goes whole, under the rename.
		let scanned = rewrite && !restart && file.Unopened ? file.Unopened->FileSize : size;
		if( !actual && size )
			restart = true;//purged since, or a parent no longer resolves.
		//More than Size is what an append of this process's left when it failed part-way, which the run's write cuts off.  A
		//file no append has gone to may be one restored or remade since, which only a scan can tell from what that left.
		else if( actual<size || (actual>size && actual!=scanned && (!size || !outstanding || outstanding->Offset!=size || actual>outstanding->End)) ){
			IO::IOException e{ sl, file.Path, ELogLevel::Error, "holds {} bytes, not the {} the historian wrote, so its next write scans it again", actual, size };
			_files.erase( day );
			throw move( e );
		}
		const DayFile fresh;
		let& known = restart ? fresh : file;
		if( rewrite && records.empty() && (!known.Size || known.Generation) ){//nothing to make an archive of, or it is one.
			_recover.erase( day );
			_unarchived.erase( day );
			_files.erase( day );
			return {};
		}

		let start = StartOf( day, _tz );
		vector<HistoryRecord> preamble;//what a file that isn't there opens with.
		absl::flat_hash_set<NodeIndex> mapped;//by this write.
		if( !known.Size ){
			for( let& [index,node] : members.Current() ){
				preamble.push_back( Added(index, node, start) );
				mapped.insert( index );
			}
		}
		vector<HistoryRecord> run;
		run.reserve( records.size() );
		for( let& r : records ){
			if( r.has_node_added() )
				mapped.insert( r.node_added().node_index() );
		}
		//Sorted, so a node's last is its newest.
		absl::flat_hash_map<NodeIndex,const Proto::DataValue*> newest;
		for( let& r : records ){
			if( !r.has_value() )
				continue;
			let index = r.value().node_index();
			newest[index] = &r.value();
			if( known.Mapped.contains(index) || !mapped.insert(index).second )
				continue;
			if( let node = members.Find(index); node ){
				run.push_back( Added(index, node->Id, start) );
				if( node->Left ){//as a copy of its NodeRemoved goes to a later file there already was.
					HistoryRecord removed;
					removed.mutable_node_removed()->set_node_index( (uint32_t)index );
					removed.mutable_node_removed()->set_ts( start );
					run.push_back( move(removed) );
				}
			}
			else
				ERR( "'{}' takes a value of node_index {}, which it doesn't map and its group doesn't know.", file.Path.string(), index );
		}
		vector<Proto::DataValue> stored;
		stored.reserve( newest.size() );
		for( let& [_,value] : newest )
			stored.push_back( *value );
		std::ranges::move( records, std::back_inserter(run) );
		HistoryRecord first;
		first.mutable_file_start()->set_ts( start );
		first.mutable_file_start()->set_next_node_index( (uint32_t)members.NextIndex );

		if( rewrite ){
			//An archive is one run, read from its start.  The preamble and the flush's records follow the file's at any one time.
			auto runs = known.Generation ? vector<Run>{ Run{.Offset=0, .End=known.Size, .Chain=0, .First=std::numeric_limits<Ticks>::min(), .Last=start} } : known.Runs;
			std::ranges::move( run, std::back_inserter(preamble) );
			first.mutable_file_start()->set_generation( known.Generation+1 );
			mapped.insert( known.Mapped.begin(), known.Mapped.end() );
			auto opened = runs.empty() ? nullptr : ms<ReadHandle>( file.Path, sl );
			Rewrite y{ day, file.Path, Temp(day), move(stored), mu<Rewrite::State>(move(opened), move(runs), move(preamble), first.file_start(), std::move(mapped), sl) };
			if( file.Generation )
				_files.erase( day );
			return DayWrite{ move(y) };
		}

		string bytes;
		vector<Run> runs;
		auto chain = known.Chain;
		if( !known.Size ){
			Appender opening{ bytes, 0 };
			opening.Add( move(first) );
			for( auto& added : preamble )
				opening.Add( move(added) );
			chain = opening.Seal();
			if( !preamble.empty() )
				runs.push_back( {.Offset=0, .End=bytes.size(), .Chain=0, .First=start, .Last=start} );
		}

		Run appended{ .Offset=known.Size+bytes.size(), .Chain=chain };
		bool timed{};
		Appender appender{ bytes, chain };
		for( auto& r : run ){
			let t = PrimaryTime( r );
			try{
				appender.Add( move(r) );
			}
			catch( Exception& e ){//one record too large for a file, which the run goes on without.
				e.SetLevel( ELogLevel::Error );
				continue;
			}
			if( !t )
				continue;
			appended.First = timed ? std::min( appended.First, *t ) : *t;
			appended.Last = timed ? std::max( appended.Last, *t ) : *t;
			timed = true;
		}
		chain = appender.Seal();

		Pending y{ .Date=day, .Path=file.Path, .Offset=known.Size, .Existed=existed, .Restart=restart, .End=known.Size+bytes.size(), .Chain=chain, .Runs=move(runs) };
		if( timed ){
			appended.End = y.End;
			y.Runs.push_back( appended );
		}
		y.Mapped = std::move( mapped );
		y.Stored = move( stored );
		y.Bytes = move( bytes );
		//One that failed before cutting the file to Offset leaves what an earlier one did.
		if( auto& o = file.Outstanding; o && o->Offset==y.Offset )
			o->End = std::max( o->End, y.End );
		else
			o = DayFile::Append{ y.Offset, y.End };
		return DayWrite{ move(y) };
	}

	α Pending::Write( SL sl )ι->IO::WriteAwait{
		return IO::WriteAwait{ Path, move(Bytes), IO::WriteOptions{.Create=true, .Mode=IO::EWriteMode::Truncate, .Offset=Offset, .Sync=true}, sl };
	}

	α GroupFiles::Commit( Pending&& run, SL sl )ε->void{
		auto& file = _files.at( run.Date );
		//Every directory to the root, on a file's first append here:  a new file's write syncs only the names it added,
		//and another group's write, or an earlier one that failed, may have made the rest.
		if( run.Restart || !file.Named )
			SyncDirectories( _root, DayDirectory(run.Date), sl );
		if( run.Restart )
			file = DayFile{ .Path=move(file.Path) };
		file.Named = true;
		file.Outstanding.reset();
		file.Size = run.End;
		file.Chain = run.Chain;
		std::ranges::move( run.Runs, std::back_inserter(file.Runs) );
		file.Mapped.insert( run.Mapped.begin(), run.Mapped.end() );
		for( auto& value : run.Stored )
			Newer( move(value) );
		_days.insert( run.Date );
		_unarchived.insert( run.Date );
	}

	α GroupFiles::Commit( Rewrite&& rewrite, TimePoint now, SL sl )ε->void{
		Replace( rewrite.Temp, rewrite.Path, sl );
		auto archive = rewrite.Archive();
		archive.Scanned = stamp( archive.Path );
		_files.insert_or_assign( rewrite.Date, move(archive) );
		_recover.erase( rewrite.Date );
		_unarchived.erase( rewrite.Date );
		_days.insert( rewrite.Date );
		_rewritten.insert_or_assign( rewrite.Date, now );
		for( auto& value : rewrite.Stored )
			Newer( move(value) );
		_unsynced.insert( rewrite.Date );
		try{
			SyncDirectories( _root, DayDirectory(rewrite.Date), sl );
			_unsynced.erase( rewrite.Date );
		}
		catch( const IO::IOException& ){}//said as it goes.
	}
	α GroupFiles::SyncRenamed( SL sl )ι->bool{
		while( !_unsynced.empty() ){
			let day = *_unsynced.begin();
			std::error_code ec;
			if( fs::exists(File(day), ec) || ec ){//one purged since has no name to keep.
				try{
					SyncDirectories( _root, DayDirectory(day), sl );
				}
				catch( const IO::IOException& e ){
					e.SetLevel( ELogLevel::Debug );//said at Error by its Commit.
					return false;
				}
			}
			_unsynced.erase( day );
		}
		return true;
	}
	α GroupFiles::Abandon( const Rewrite& rewrite )ι->void{
		removeTemp( rewrite.Temp );
		if( rewrite.Unreadable() )
			_files.erase( rewrite.Date );
	}

	α GroupFiles::Past( Day day, TimePoint now )Ι->bool{
		return now>=DayStart( Day{sys_days{day}+days{1}}, _tz )+_delay;
	}
	α GroupFiles::File( Day day )Ι->fs::path{ return _root/DayDirectory( day )/( _name+".binpb" ); }
	α GroupFiles::Temp( Day day )Ι->fs::path{ return _root/DayDirectory( day )/( _name+".binpb.tmp" ); }
	α GroupFiles::Due( TimePoint now )ι->vector<Day>{
		for( auto p = _rewritten.begin(); p!=_rewritten.end(); ){
			if( p->second<=now && now<p->second+2*_delay ){
				++p;
				continue;
			}
			if( auto f = _files.find(p->first); f!=_files.end() && f->second.Generation )
				_files.erase( f );
			p = _rewritten.erase( p );
		}
		vector<Day> y;
		for( let day : _unarchived ){
			if( _recover.contains(day) || Past(day, now) )
				y.push_back( day );
		}
		return y;
	}
	α GroupFiles::Retire()ι->void{
		_retired = true;
		_recover.insert( _unarchived.begin(), _unarchived.end() );
	}
	α GroupFiles::RecoverFrom( TimePoint flushed )Ι->Day{
		const Day before{ sys_days{DayOf(flushed, _tz)}-days{1} };
		return _unarchived.empty() ? before : std::min( before, *_unarchived.begin() );
	}
	α GroupFiles::Deferred( Day day, TimePoint now )Ι->bool{
		auto p = _rewritten.find( day );
		return p!=_rewritten.end() && p->second<=now && now<p->second+_delay;
	}

	α GroupFiles::LaterDays( Day day )ι->vector<Day>{
		vector<Day> y;
		for( auto p = _days.upper_bound(std::max(day, _present)); p!=_days.end(); ){
			std::error_code ec;
			if( !fs::exists(File(*p), ec) && !ec ){
				p = _days.erase( p );//purged since, so no change goes there, and a late record makes it again.
				continue;
			}
			y.push_back( *p++ );
		}
		return y;
	}
	α GroupFiles::Present( Day today )ι->void{
		_present = std::max( _present, today );
	}
	α GroupFiles::Openable( Day day )Ι->bool{
		let path = File( day );
		std::error_code ec;
		fs::create_directories( path.parent_path(), ec );
		return !ec && std::ofstream{ path, std::ios::binary | std::ios::app }.is_open();
	}
	α GroupFiles::Newest( NodeIndex index )Ι->const Proto::DataValue*{
		auto p = _last.find( index );
		return p==_last.end() ? nullptr : &p->second;
	}
	α GroupFiles::Find( Day day )Ι->const DayFile*{
		auto p = _files.find( day );
		return p==_files.end() ? nullptr : &p->second;
	}

	α GroupFiles::Serve( Day day, SL sl )Ε->optional<Served>{
		let path = File( day );
		Served y;
		if( auto p = _files.find(day); p!=_files.end() ){
			let& file = p->second;
			y.Generation = file.Generation;
			y.Size = file.Size;
			y.Runs = file.Runs;
		}
		else{
			std::error_code ec;
			if( !fs::exists(path, ec) && !ec )
				return nullopt;
			try{
				auto scanned = Scan( path, {}, sl );
				y.Generation = scanned.Start ? scanned.Start->generation() : 0;
				y.Size = scanned.Size;
				y.Runs = move( scanned.Runs );
			}
			catch( const IO::IOException& e ){
				e.SetLevel( ELogLevel::Error );//history a read can't serve, which an operator must see.
				throw;
			}
		}
		if( !y.Size )
			return nullopt;
		if( y.Generation )
			y.Runs = { Run{.Offset=0, .End=y.Size, .Chain=0, .First=std::numeric_limits<Ticks>::min(), .Last=std::numeric_limits<Ticks>::max()} };
		try{
			y.File = ms<ReadHandle>( path, sl );
		}
		catch( const IO::IOException& e ){
			if( e.Error!=IO::EIOError::NotFound )
				throw;
			e.SetLevel( ELogLevel::Debug );//removed since the process last knew it:  purged, which a read sees as no records.
			return nullopt;
		}
		return y;
	}

	α Days( const fs::path& root, sv name, SL sl )ε->vector<Day>{
		let file = string{ name }+".binpb";
		vector<Day> y;
		walk( root, sl, [&]( Day day ){
			std::error_code ec;
			if( fs::exists(root/DayDirectory(day)/file, ec) || ec )//one it can't tell of fails the read that opens it.
				y.push_back( day );
			return false;
		});
		std::ranges::reverse( y );
		return y;
	}
}
