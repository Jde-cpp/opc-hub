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

		Ω failed( const fs::path& path, const std::error_code& ec, SL sl )ι->IO::IOException{
			IO::IOException e{ path, (uint32)ec.value(), ec.message(), sl };
			e.SetLevel( ELogLevel::Error );
			return e;
		}
		//What a stored value is filed and read by.
		Ω primary( const Proto::DataValue& v )ι->optional<Ticks>{
			return v.has_source_ts() ? v.source_ts() : v.has_server_ts() ? optional<Ticks>{ v.server_ts() } : nullopt;
		}
		//dir's subdirectories whose names are numbers, largest first.
		Ω numbered( const fs::path& dir, SL sl )ε->vector<unsigned>{
			vector<unsigned> y;
			std::error_code ec;
			for( fs::directory_iterator p{dir, ec}, end; !ec && p!=end; p.increment(ec) ){
				let name = p->path().filename().string();
				unsigned n{};
				let [last, error] = std::from_chars( name.data(), name.data()+name.size(), n );
				if( error==std::errc{} && last==name.data()+name.size() && p->is_directory(ec) )
					y.push_back( n );
			}
			if( ec )
				throw failed( dir, ec, sl );
			std::ranges::sort( y, std::greater{} );
			return y;
		}
		//Each day under root that holds file, newest first, until found returns true.
		Ω walk( const fs::path& root, const string& file, SL sl, const std::function<bool( Day )>& found )ε->void{
			for( let y : numbered(root, sl) ){
				let yearDir = root/std::to_string( y );
				for( let m : numbered(yearDir, sl) ){
					for( let d : numbered(yearDir/std::to_string(m), sl) ){
						const Day day{ year{(int)y}, month{m}, std::chrono::day{d} };
						std::error_code ec;
						if( day.ok() && fs::exists(root/DayDirectory(day)/file, ec) && found(day) )
							return;
					}
				}
			}
		}
	}

	α DayOf( Ticks t, const time_zone& tz )ι->Day{
		let since1970 = seconds{ std::clamp<Ticks>(t, 0, Latest)/PerSecond-EpochSeconds };
		return Day{ floor<days>(tz.to_local(sys_seconds{since1970})) };
	}
	α StartOf( Day day, const time_zone& tz )ι->Ticks{
		return ( tz.to_sys(local_days{day}, choose::earliest).time_since_epoch()/1s+EpochSeconds )*PerSecond;
	}
	α DayDirectory( Day day )ι->fs::path{
		return fs::path{ std::to_string((int)day.year()) }/std::to_string( (unsigned)day.month() )/std::to_string( (unsigned)day.day() );
	}

	GroupFiles::GroupFiles( fs::path root, string name, const time_zone& tz, Day today, SL sl )ε:
		_root{ move(root) },
		_name{ move(name) },
		_tz{ tz },
		_flushed{ _root/(_name+".flushed"), sl }{
		_restored.Flushed = _flushed.Time();
		//The newest file that holds anything says what the group is:  one a crash cut short in its preamble holds nothing.
		bool restored{};
		flat_map<NodeIndex,ExNodeId> members;
		walk( _root, _name+".binpb", sl, [&]( Day day ){
			_days.insert( day );
			if( !restored )
				restored = Open( day, sl, [&]( HistoryRecord& r ){ Restore(r, members); } ).Size>0;
			return restored && day<=today;
		});
		for( auto&& [index,node] : members )
			_restored.Members.insert_or_assign( move(node), index );
	}

	α GroupFiles::Restore( HistoryRecord& r, flat_map<NodeIndex,ExNodeId>& members )ι->void{
		auto& next = _restored.NextIndex;
		switch( r.record_case() ){
		case HistoryRecord::kFileStart:
			next = std::max<NodeIndex>( next, r.file_start().next_node_index() );
			break;
		case HistoryRecord::kNodeAdded:{
			auto& added = *r.mutable_node_added();
			let index = added.node_index();
			members.insert_or_assign( index, ProtoUtils::ToExNodeId(added.node()) );
			next = std::max<NodeIndex>( next, (NodeIndex)index+1 );//one issued after the file's FileStart was written.
			if( added.has_start() ){
				added.mutable_start()->set_node_index( index );
				Newer( move(*added.mutable_start()) );
			}
			break;}
		case HistoryRecord::kNodeRemoved:
			members.erase( r.node_removed().node_index() );
			break;
		case HistoryRecord::kValue:
			Newer( move(*r.mutable_value()) );
			break;
		default:
			break;
		}
	}

	α GroupFiles::Newer( Proto::DataValue&& stored )ι->void{
		let t = primary( stored );
		if( !t )
			return;
		auto [p, inserted] = _last.try_emplace( stored.node_index() );
		if( inserted || *t>=primary(p->second) )
			p->second = move( stored );
	}

	//The first time the process opens the file:  its scan, and one pass over what the scan kept for the indexes it maps.
	α GroupFiles::Open( Day day, SL sl, const std::function<void( HistoryRecord& )>& restore )ε->DayFile&{
		if( auto p = _files.find(day); p!=_files.end() )
			return p->second;
		DayFile file{ .Path=_root/DayDirectory(day)/(_name+".binpb") };
		std::error_code ec;
		if( fs::exists(file.Path, ec) ){
			auto scanned = Scan( file.Path, sl );
			file.Size = scanned.Size;
			file.Chain = scanned.Chain;
			file.Runs = move( scanned.Runs );
			if( scanned.Keep() )
				file.Refused = "holds damage a truncation would lose, or is another program's";
			else if( scanned.Start && scanned.Start->generation() )
				file.Refused = "is an archive, which this build can't merge a late record into";
			if( file.Size ){
				std::ifstream in{ file.Path, std::ios::binary };
				if( !in )
					throw IO::IOException{ sl, file.Path, ELogLevel::Error, "could not be opened to read" };
				google::protobuf::io::IstreamInputStream stream{ &in };
				Reader reader{ stream, 0, file.Size, 0 };
				for( HistoryRecord r; reader.Next(r); ){
					if( r.has_node_added() )
						file.Mapped.insert( r.node_added().node_index() );
					if( restore )
						restore( r );
				}
				if( reader.Stop()!=EStop::End )
					throw IO::IOException{ sl, file.Path, ELogLevel::Error, "read {} at byte {}, short of the {} bytes its scan kept", ToString(*reader.Stop()), reader.Offset(), file.Size };
			}
			if( scanned.Size<scanned.FileSize )
				file.Unopened = move( scanned );
		}
		else if( ec )
			throw failed( file.Path, ec, sl );
		return _files.emplace( day, move(file) ).first->second;
	}

	α GroupFiles::Added( NodeIndex index, const ExNodeId& node, Ticks start )Ι->HistoryRecord{
		HistoryRecord y;
		auto& added = *y.mutable_node_added();
		added.set_node_index( (uint32_t)index );
		*added.mutable_node() = ProtoUtils::ToExNodeId( node );
		added.set_ts( start );
		//The walk back to the last record before the day, for a value that isn't, is step 5's.
		if( auto p = _last.find(index); p!=_last.end() && *primary(p->second)<start ){
			*added.mutable_start() = p->second;
			added.mutable_start()->clear_node_index();
		}
		return y;
	}

	α GroupFiles::Prepare( Day day, vector<HistoryRecord>&& records, const Membership& members, SL sl )ε->optional<Pending>{
		auto& file = Open( day, sl );
		std::error_code ec;
		if( !file.Refused.empty() ){
			if( fs::exists(file.Path, ec) || ec ){
				if( !file.Discarded++ )
					ERR( "'{}' {}, so the historian won't append to it:  its {} records, and any after them, are dropped until it is repaired or removed.", file.Path.string(), file.Refused, records.size() );
				return nullopt;
			}
			file = DayFile{ .Path=move(file.Path) };//removed since, so it starts again.
		}
		MakeDirectories( file.Path.parent_path(), sl );
		if( file.Unopened ){
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
			throw failed( file.Path, ec, sl );
		//More than Size is an append of this process's that failed part-way, which the run's write cuts off.
		if( actual<file.Size ){
			if( actual ){
				IO::IOException e{ sl, file.Path, ELogLevel::Error, "holds {} bytes, not the {} the historian wrote, so its next append scans it again", actual, file.Size };
				_files.erase( day );
				throw move( e );
			}
			file = DayFile{ .Path=move(file.Path) };//purged since, so it starts again.
		}

		let start = StartOf( day, _tz );
		string bytes;
		vector<Run> runs;
		absl::flat_hash_set<NodeIndex> mapped;//by this append.
		auto chain = file.Chain;
		if( !file.Size ){
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
			if( file.Mapped.contains(index) || !mapped.insert(index).second )
				continue;
			if( let node = members.Find(index); node )
				run.push_back( Added(index, *node, start) );
			else
				ERR( "'{}' takes a value of node_index {}, which it doesn't map and its group doesn't know.", file.Path.string(), index );
		}
		vector<Proto::DataValue> stored;
		stored.reserve( newest.size() );
		for( let& [_,value] : newest )
			stored.push_back( *value );
		std::ranges::move( records, std::back_inserter(run) );

		Run appended{ .Offset=file.Size+bytes.size(), .Chain=chain };
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

		Pending y{ .Date=day, .Path=file.Path, .Offset=file.Size, .Existed=existed, .End=file.Size+bytes.size(), .Chain=chain, .Runs=move(runs) };
		if( timed ){
			appended.End = y.End;
			y.Runs.push_back( appended );
		}
		y.Mapped = std::move( mapped );
		y.Stored = move( stored );
		y.Bytes = move( bytes );
		return y;
	}

	α Pending::Write( SL sl )ι->IO::WriteAwait{
		return IO::WriteAwait{ Path, move(Bytes), IO::WriteOptions{.Create=true, .Mode=IO::EWriteMode::Truncate, .Offset=Offset, .Sync=true}, sl };
	}

	α GroupFiles::Commit( Pending&& run, SL sl )ε->void{
		auto& file = _files.at( run.Date );
		if( !file.Named ){
			if( run.Existed )
				SyncDirectories( _root, DayDirectory(run.Date), sl );
			file.Named = true;
		}
		file.Size = run.End;
		file.Chain = run.Chain;
		std::ranges::move( run.Runs, std::back_inserter(file.Runs) );
		file.Mapped.insert( run.Mapped.begin(), run.Mapped.end() );
		for( auto& value : run.Stored )
			Newer( move(value) );
		_days.insert( run.Date );
	}

	α GroupFiles::LaterDays( Day day )Ι->vector<Day>{
		return { _days.upper_bound(day), _days.end() };
	}
	α GroupFiles::Find( Day day )Ι->const DayFile*{
		auto p = _files.find( day );
		return p==_files.end() ? nullptr : &p->second;
	}
}
