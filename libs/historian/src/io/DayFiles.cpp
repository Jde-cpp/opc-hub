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
		//Each day under root that holds file, newest first, until found returns true.  Throws for a file it can't tell is
		//there, which may be the newest.
		Ω walk( const fs::path& root, const string& file, SL sl, const std::function<bool( Day )>& found )ε->void{
			for( let y : numbered(root, 1601, 9999, sl) ){//DayOf's range.
				let yearDir = root/std::to_string( y );
				for( let m : numbered(yearDir, 1, 12, sl) ){
					for( let d : numbered(yearDir/std::to_string(m), 1, 31, sl) ){
						const Day day{ year{(int)y}, month{m}, std::chrono::day{d} };
						if( !day.ok() )
							continue;
						let path = root/DayDirectory( day )/file;
						std::error_code ec;
						let there = fs::exists( path, ec );
						if( ec )
							throw Failed( path, ec, sl );
						if( there && found(day) )
							return;
					}
				}
			}
		}
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

	GroupFiles::GroupFiles( fs::path root, string name, const time_zone& tz, Day today, SL sl )ε:
		_root{ move(root) },
		_name{ move(name) },
		_tz{ tz },
		_flushed{ _root/(_name+".flushed"), sl },
		_present{ today }{
		_restored.Flushed = _flushed.Time();
		if( _restored.Flushed )
			Present( DayOf(UADateTime{*_restored.Flushed}.UA(), _tz) );
		//The newest file that holds anything says what the group is:  one a crash cut short in its preamble holds nothing.
		//Each node's newest value is in any file down to the first that isn't after the present, since a future-dated file's
		//preamble holds the start values of when it was made.
		bool restored{};
		flat_map<NodeIndex,ExNodeId> members;
		walk( _root, _name+".binpb", sl, [&]( Day day ){
			_days.insert( day );
			if( !restored )
				restored = Open( day, sl, [&]( HistoryRecord& r ){ Restore(r, members); } ).Size>0;
			else
				Open( day, sl, [&]( HistoryRecord& r ){ Fold(r); } );
			return restored && day<=_present;
		});
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
		DayFile file{ .Path=_root/DayDirectory(day)/(_name+".binpb") };
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
			else if( scanned.Start && scanned.Start->generation() )
				file.Refused = "is an archive, which this build can't merge a late record into";
			if( scanned.Size<scanned.FileSize )
				file.Unopened = move( scanned );
		}
		else if( ec )
			throw Failed( file.Path, ec, sl );
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

	α GroupFiles::Prepare( Day day, vector<HistoryRecord>&& records, const Membership& members, SL sl )ε->optional<Pending>{
		if( auto p = _files.find(day); p!=_files.end() && !p->second.Refused.empty() ){
			if( let now = stamp(p->second.Path); now && now!=p->second.Scanned )
				_files.erase( p );//changed since its scan, repaired perhaps, so Open scans it again.
		}
		auto& file = Open( day, sl );
		std::error_code ec;
		bool restart{};//Commit's to apply, so a write that fails leaves what is known of the file.
		if( !file.Refused.empty() ){
			if( fs::exists(file.Path, ec) || ec ){
				if( !file.Discarded++ )
					ERR( "'{}' {}, so the historian won't append to it:  its {} records, and any after them, are dropped until it is repaired or removed.", file.Path.string(), file.Refused, records.size() );
				return nullopt;
			}
			restart = true;//removed since.
		}
		MakeDirectories( file.Path.parent_path(), sl );
		if( file.Unopened && !restart ){
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
		if( !actual && size )
			restart = true;//purged since, or a parent no longer resolves.
		//More than Size is what an append of this process's left when it failed part-way, which the run's write cuts off.  A
		//file no append has gone to may be one restored or remade since, which only a scan can tell from what that left.
		else if( actual<size || (actual>size && (!size || !outstanding || outstanding->Offset!=size || actual>outstanding->End)) ){
			IO::IOException e{ sl, file.Path, ELogLevel::Error, "holds {} bytes, not the {} the historian wrote, so its next append scans it again", actual, size };
			_files.erase( day );
			throw move( e );
		}
		const DayFile fresh;
		let& known = restart ? fresh : file;

		let start = StartOf( day, _tz );
		string bytes;
		vector<Run> runs;
		absl::flat_hash_set<NodeIndex> mapped;//by this append.
		auto chain = known.Chain;
		if( !known.Size ){
			Appender preamble{ bytes, 0 };
			HistoryRecord first;
			first.mutable_file_start()->set_ts( start );
			first.mutable_file_start()->set_next_node_index( (uint32_t)members.NextIndex );
			preamble.Add( move(first) );
			for( let& [index,node] : members.Current() ){
				preamble.Add( Added(index, node, start) );
				mapped.insert( index );
			}
			chain = preamble.Seal();
			if( !mapped.empty() )
				runs.push_back( {.Offset=0, .End=bytes.size(), .Chain=0, .First=start, .Last=start} );
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
		return y;
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
	}

	α GroupFiles::LaterDays( Day day )ι->vector<Day>{
		vector<Day> y;
		for( auto p = _days.upper_bound(std::max(day, _present)); p!=_days.end(); ){
			std::error_code ec;
			if( !fs::exists(_root/DayDirectory(*p)/(_name+".binpb"), ec) && !ec ){
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
		let path = _root/DayDirectory( day )/( _name+".binpb" );
		std::error_code ec;
		fs::create_directories( path.parent_path(), ec );
		return !ec && std::ofstream{ path, std::ios::binary | std::ios::app }.is_open();
	}
	α GroupFiles::Find( Day day )Ι->const DayFile*{
		auto p = _files.find( day );
		return p==_files.end() ? nullptr : &p->second;
	}
}
