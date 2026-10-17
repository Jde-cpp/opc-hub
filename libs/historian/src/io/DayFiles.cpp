#include "DayFiles.h"
#include <fstream>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/durable.h>
#include <jde/opc/proto/opc.Common.h>
#include "Records.h"

#define let const auto

namespace Jde::Opc::Hist{
	using namespace std::chrono;
	using Proto::HistoryRecord;
	constexpr ELogTags _tags{ ELogTags::IO };

	namespace{
		constexpr Ticks PerSecond{ 10'000'000 };
		constexpr int64_t EpochSeconds{ UA_DATETIME_UNIX_EPOCH/PerSecond };//1601 to 1970.
		constexpr Ticks Latest{ (sys_days{year{9999}/December/31}.time_since_epoch()/1s+86'399+EpochSeconds)*PerSecond };


		//Whether r is a preamble record of the day that starts at start.
		Ω preambled( const HistoryRecord& r, Ticks start )ι->bool{ return r.has_node_added() && Bare( r.node_added() ) && r.node_added().ts()==start; }
		//A preamble record, as the file's map lists it.
		Ω listed( const Proto::NodeAdded& added )ι->Listed{
			return { ProtoUtils::ToExNodeId(added.node()), added.has_start() ? optional<Proto::DataValue>{ added.start() } : nullopt };
		}
		//An archive's head:  its FileStart, its size, and its preamble from the records at its day's start alone, into which
		//its rewrite folded every correction:  without the first-open scan, which reads an archive through.  None when the
		//file isn't an archive.  Throws when its preamble can't be read through.
		struct ArchiveHead final{ Proto::FileStart Start; uint Size{}; Hist::Preamble Preamble; };
		Ω archivePreamble( const sp<IO::OpenFile>& file, SL sl )ε->optional<ArchiveHead>{
			let size = file->Size( sl );
			Reader reader{ Run{.Offset=0, .End=size, .Chain=0, .First=0, .Last=0} };
			HistoryRecord r;
			if( !IO::Records::NextNow(reader, file, r, sl) || !r.has_file_start() || r.file_start().crc()!=IO::Records::StartCrc(r.file_start()) || !r.file_start().generation() )
				return nullopt;
			ArchiveHead y{ r.file_start(), size };
			let start = y.Start.ts();
			while( IO::Records::NextNow(reader, file, r, sl) ){
				if( r.has_issued() )
					continue;
				if( PrimaryTime(r)!=start )
					return y;
				if( r.has_node_added() && Bare(r.node_added()) )
					y.Preamble.insert_or_assign( r.node_added().node_index(), listed(r.node_added()) );
			}
			if( reader.Stop()!=EStop::End )
				throw IO::IOException{ sl, file->Path(), ELogLevel::Error, "{} in its preamble, at byte {}", ToString(*reader.Stop()), reader.Offset() };
			return y;
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

	GroupFiles::GroupFiles( fs::path root, string name, const time_zone& tz, Duration delay, Day today, SL sl )ε:
		_root{ move(root) },
		_name{ move(name) },
		_tz{ tz },
		_delay{ delay },
		_flushed{ IO::Records::Flushed::Load( _root/(_name+".flushed"), sl ) },
		_present{ today }{
		if( let t = _flushed.Time(); t )
			_restored.Flushed = UADateTime{ (UA_DateTime)*t }.Time();
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
		IO::Records::Walk( _root, sl, [&]( Day day ){
			if( settled && (!from || day<*from) )
				return true;
			std::error_code ec;
			if( IO::RemoveTemp(Temp(day)) )
				WARN( "Removed '{}', which a rewrite that didn't finish left.", Temp(day).string() );
			let path = File( day );
			let there = fs::exists( path, ec );
			if( ec && !settled )//it may be the newest.
				throw IO::Failed( path, ec, sl );
			if( !there && !ec )
				return false;
			if( !settled ){
				_days.insert( day );
				let& file = restored ? Open( day, sl, [&]( HistoryRecord& r ){ Fold(r, day); } ) : Open( day, sl, [&]( HistoryRecord& r ){ Restore(r, members, day); } );
				restored = restored || file.Size>0;
				if( fs::exists(ModsFile(day), ec) || ec )//one it can't tell of is read through the edits too.
					_edited.push_back( day );
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
						throw IO::Failed( path, ec, sl );
					let start = IO::Records::Head( ms<IO::OpenFile>(path, sl), StartField, sl );
					live = start && !start->generation();
				}
				catch( const IO::IOException& ){}//said as it goes.
				if( live ){
					_unarchived.insert( day );
					_recover.insert( day );
				}
			}
			else if( ec )
				IO::Failed( path, ec, sl ).Log();
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

	α GroupFiles::Restore( HistoryRecord& r, flat_map<NodeIndex,ExNodeId>& members, Day day )ι->void{
		auto& next = _restored.NextIndex;
		switch( r.record_case() ){
		case HistoryRecord::kIssued:
			next = std::max<NodeIndex>( next, r.issued().next_node_index() );
			break;
		case HistoryRecord::kNodeAdded:{
			let index = r.node_added().node_index();
			members.insert_or_assign( index, ProtoUtils::ToExNodeId(r.node_added().node()) );
			if( let type = r.node_added().data_type() )//one with none, a later preamble record a correction wrote first, leaves it.
				_restored.DataTypes.insert_or_assign( index, type );
			next = std::max<NodeIndex>( next, (NodeIndex)index+1 );//one issued after the file's FileStart was written.
			break;}
		case HistoryRecord::kNodeRemoved:
			members.erase( r.node_removed().node_index() );
			_restored.DataTypes.erase( r.node_removed().node_index() );
			break;
		default:
			break;
		}
		Fold( r, day );
	}
	α GroupFiles::Fold( HistoryRecord& r, Day day )ι->void{
		if( r.has_value() )
			Newer( move(*r.mutable_value()) );
		else if( preambled(r, StartOf(day, _tz)) ){
			auto& added = *r.mutable_node_added();
			if( added.has_start() ){
				added.mutable_start()->set_node_index( added.node_index() );
				Newer( move(*added.mutable_start()), day );
			}
			else if( auto p = _last.find(added.node_index()); p!=_last.end() && p->second.Preamble==day )
				_last.erase( p );//a correction:  nothing of the node is left before the day.
		}
	}

	α GroupFiles::Newer( Proto::DataValue&& stored, optional<Day> preamble )ι->void{
		let t = PrimaryTime( stored );
		if( !t )
			return;
		auto [p, inserted] = _last.try_emplace( stored.node_index() );
		if( inserted || *t>=PrimaryTime(p->second.Value) || (preamble && p->second.Preamble==preamble) )
			p->second = { move(stored), preamble };
	}
	α GroupFiles::Relast( NodeIndex index, optional<Proto::DataValue> newest )ι->void{
		if( newest )
			_last.insert_or_assign( index, Last{move(*newest), nullopt} );
		else
			_last.erase( index );
	}
	α GroupFiles::Touch( NodeIndex index, Ticks time )ι->void{
		_touched.emplace_back( index, time );
	}

	//What a scan makes of the file:  its size, chain and runs, or why the historian won't write to it.  A damaged archive
	//throws when restoring, which must read it through, and is refused otherwise.
	Ω fill( DayFile& file, Scanned&& scanned, bool restoring, SL sl )ε->void{
		file.Size = scanned.Size;
		file.Chain = scanned.Chain;
		file.Runs = move( scanned.Runs );
		if( scanned.Keep() )
			file.Refused = "holds damage a truncation would lose, or is another program's";
		else if( scanned.Start && scanned.Start->generation() ){
			file.Generation = scanned.Start->generation();
			if( scanned.Stop!=EStop::End ){//an archive is whole, so one that can't be read through is damaged.
				if( restoring )
					throw IO::IOException{ sl, file.Path, ELogLevel::Error, "reads {} at byte {}, short of the {} bytes its archive holds", ToString(scanned.Stop), scanned.StopOffset, scanned.Size };
				file.Refused = Ƒ( "is an archive that reads {} at byte {}", ToString(scanned.Stop), scanned.StopOffset );
			}
		}
		if( scanned.Size<scanned.FileSize )
			file.Unopened = move( scanned );
	}

	Opening::Opening( fs::path path, Ticks start, std::function<void( HistoryRecord& )> restore, SL sl )ε:
		_opened{ .Path=move(path) },
		_decoder{ [this, start, restore=move(restore)]( HistoryRecord& r ){
			if( preambled(r, start) )
				_opened.Preamble.insert_or_assign( r.node_added().node_index(), listed(r.node_added()) );
			if( restore )
				restore( r );
		} }{
		try{
			_file = ms<IO::OpenFile>( _opened.Path, sl );
		}
		catch( IO::IOException& e ){
			if( e.Error!=IO::EIOError::NotFound ){
				e.SetLevel( ELogLevel::Error );
				throw;
			}
			return;
		}
		_opened.Stamped = IO::Records::StampOf( _opened.Path );//before the scan, so a change during it shows next time.
		_scanner.emplace( _file, StartField, CheckpointField, _decoder, sl );
	}
	α Opening::Next()ε->ENext{
		if( !_scanner || _opened.Scan )
			return ENext::End;
		let next = _scanner->Next();
		if( next==ENext::End )
			_opened.Scan = _scanner->Result();
		return next;
	}
	α Opening::Wants()Ι->optional<Window>{ return _scanner ? _scanner->Wants() : nullopt; }
	α Opening::Feed( string&& bytes )ι->void{
		if( _scanner )
			_scanner->Feed( move(bytes) );
	}

	α GroupFiles::ToScan( const vector<Day>& days )Ι->vector<std::pair<Day,fs::path>>{
		vector<std::pair<Day,fs::path>> y;
		for( let day : days ){
			auto p = _files.find( day );
			bool open = p==_files.end();
			if( !open && (p->second.Generation || !p->second.Refused.empty()) ){//as Prepare opens again:  changed since its scan, or its rewrite, repaired perhaps.
				let changed = IO::Records::StampOf( p->second.Path );
				open = changed && changed!=p->second.Stamped;
			}
			if( open )
				y.emplace_back( day, File(day) );
		}
		return y;
	}
	α GroupFiles::Adopt( Day day, Opened&& opened )ι->void{
		_failedOpenings.erase( day );//an earlier flush's, which this opening supersedes, adopted or left for Prepare.
		std::error_code ec;
		if( auto p = _files.find(day); p!=_files.end() ){
			if( !(p->second.Generation || !p->second.Refused.empty()) || !opened.Stamped || IO::Records::StampOf(p->second.Path)!=opened.Stamped )
				return;
			_files.erase( p );
		}
		else if( opened.Stamped ? IO::Records::StampOf(opened.Path)!=opened.Stamped : fs::exists(opened.Path, ec) || ec )
			return;//changed since the open, or made since:  Prepare opens it.
		_files.emplace( day, Known(day, move(opened), false, false, SRCE_CUR) );
	}
	α GroupFiles::Known( Day day, Opened&& opened, bool restoring, bool mods, SL sl )ε->DayFile{
		DayFile file{ {.Path=move(opened.Path)}, std::move(opened.Preamble) };//std::, as absl::move is found for the map.
		file.Stamped = opened.Stamped;
		if( opened.Scan )
			fill( file, move(*opened.Scan), restoring, sl );
		if( !mods && file.Size && !file.Generation && file.Refused.empty() )
			_unarchived.insert( day );
		return file;
	}

	α GroupFiles::Adopt( Day day, up<Exception> failed )ι->void{
		if( !_files.contains(day) )
			_failedOpenings.insert_or_assign( day, move(failed) );
	}

	//The first time the process opens the file:  its scan, read now, and one pass over what the scan kept for its preamble
	//and the restore.  The flush opens its days' files first, outside the lock (Opening, Adopt), and one it couldn't read
	//throws here as its own open would have.
	α GroupFiles::Open( Day day, SL sl, const std::function<void( HistoryRecord& )>& restore, bool mods )ε->DayFile&{
		auto& files = mods ? _mods : _files;
		if( auto p = files.find(day); p!=files.end() )
			return p->second;
		if( !mods ){
			if( auto p = _failedOpenings.find(day); p!=_failedOpenings.end() ){
				auto failed = move( p->second );
				_failedOpenings.erase( p );
				failed->Throw();
			}
		}
		//One read:  the scan hands over each record it keeps, for the preamble and the restore.
		Opening opening{ mods ? ModsFile(day) : File(day), StartOf(day, _tz), restore, sl };
		while( opening.Next()==ENext::More )
			IO::Records::FeedNow( opening, sl );
		return files.emplace( day, Known(day, opening.Take(), restore!=nullptr, mods, sl) ).first->second;
	}

	α GroupFiles::Added( NodeIndex index, const Membership::Node& node, Ticks start, const Membership& members, bool withStart )Ε->HistoryRecord{
		HistoryRecord y;
		auto& added = *y.mutable_node_added();
		added.set_node_index( (uint32_t)index );
		*added.mutable_node() = ProtoUtils::ToExNodeId( node.Id );
		added.set_ts( start );
		added.set_data_type( BuiltIn(node.DataType) );
		if( !withStart )
			return y;
		//Its last record before the day:  the node's newest when that is before it, and otherwise, for a file made after
		//its day, the walk back through the files, the edits applied.
		optional<Proto::DataValue> value;
		if( auto p = _last.find(index); p!=_last.end() && *PrimaryTime(p->second.Value)<start )
			value = p->second.Value;
		else if( p!=_last.end() && members.Last )
			value = members.Last( index, start-1 );
		if( value ){
			*added.mutable_start() = move( *value );
			added.mutable_start()->clear_node_index();
		}
		return y;
	}

	//The layer's rewrite with the historian's head:  the records at the day's start, and a stale Issued, which the fresh one
	//in the prologue replaces.  A member's later preamble record there is a corrected start value, which a reader of the
	//live file took in its place:  it goes into the first.  One after the member's NodeRemoved there is a re-add, which
	//stays.  The fold notes the archive's map.
	struct Rewrite::State final{
		State( fs::path path, fs::path temp, sp<IO::OpenFile> file, vector<Run> runs, vector<HistoryRecord> late, Proto::FileStart start, NodeIndex nextIndex, SL sl )ι:
			Start{ start },
			Records{ move(path), move(temp), move(file), move(runs), move(late), start, Prologue(nextIndex), Hooks(start.ts()), IO::Records::PartBytes, sl }
		{}
		Ω Prologue( NodeIndex nextIndex )ι->vector<HistoryRecord>{
			vector<HistoryRecord> y;
			if( nextIndex )
				y.emplace_back().mutable_issued()->set_next_node_index( (uint32_t)nextIndex );
			return y;
		}
		α Hooks( Ticks start )ι->IO::Records::Rewrite<HistoryRecord>::Hooks{
			return {
				.IsHead=[start]( const HistoryRecord& r ){ return r.has_issued() || PrimaryTime(r)==start; },
				.FoldHead=[this]( vector<HistoryRecord>& head ){ Fold( head ); }
			};
		}
		α Fold( vector<HistoryRecord>& head )ι->void{
			vector<HistoryRecord> folded;
			absl::flat_hash_map<NodeIndex,uint> members;
			for( auto& r : head ){
				if( r.has_issued() )
					continue;
				if( r.has_node_removed() )
					members.erase( r.node_removed().node_index() );
				else if( r.has_node_added() && Bare(r.node_added()) ){
					let [p, added] = members.try_emplace( r.node_added().node_index(), folded.size() );
					if( !added ){
						auto& kept = *folded[p->second].mutable_node_added();
						if( let type = r.node_added().data_type() )
							kept.set_data_type( type );
						if( r.node_added().has_start() )
							*kept.mutable_start() = move( *r.mutable_node_added()->mutable_start() );
						else
							kept.clear_start();
						continue;
					}
				}
				folded.push_back( move(r) );
			}
			for( let& r : folded ){
				if( preambled(r, Start.ts()) )
					Preamble.insert_or_assign( r.node_added().node_index(), listed(r.node_added()) );
			}
			head = move( folded );
		}
		Proto::FileStart Start;
		IO::Records::Rewrite<HistoryRecord> Records;
		Hist::Preamble Preamble;//as the archive has it, once it is written.
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
	α Rewrite::Next()ε->IO::Records::EPart{ return _state->Records.Next(); }
	α Rewrite::Wants()Ι->optional<Window>{ return _state->Records.Wants(); }
	α Rewrite::Feed( string&& bytes )ι->void{ _state->Records.Feed( move(bytes) ); }
	α Rewrite::File()Ι->const sp<IO::OpenFile>&{ return _state->Records.File(); }
	α Rewrite::Write( SL sl )ι->IO::WriteAwait{ return _state->Records.Write( sl ); }
	α Rewrite::Unreadable()Ι->bool{ return _state && _state->Records.Unreadable(); }
	α Rewrite::Archive()ι->DayFile{
		return DayFile{ IO::Records::FileState{ .Path=Path, .Generation=_state->Records.Start().generation(), .Size=_state->Records.Size(), .Named=true }, std::move(_state->Preamble) };
	}

	α GroupFiles::Prepare( Day day, vector<HistoryRecord>&& records, const Membership& members, TimePoint now, SL sl, bool create )ε->DayWrite{
		if( auto p = _files.find(day); p!=_files.end() && (p->second.Generation || !p->second.Refused.empty()) ){
			if( let changed = IO::Records::StampOf(p->second.Path); changed && changed!=p->second.Stamped )
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
		IO::MakeDirectories( file.Path.parent_path(), sl );
		let rewrite = ( file.Generation && !restart ) || Past( day, now ) || _recover.contains( day ) || _retired;
		if( file.Unopened && !restart && !rewrite ){
			try{
				IO::Records::Truncate( file.Path, *file.Unopened, sl );
			}
			catch( ... ){//no longer the file that was scanned, perhaps, so the next append scans it again.
				_files.erase( day );
				throw;
			}
			file.Unopened.reset();
		}
		bool existed;
		try{
			let verified = IO::Records::Verify( file, rewrite, restart, sl );
			existed = verified.Existed;
			restart = verified.Restart;
		}
		catch( ... ){//other bytes than the historian wrote, so its next write scans it again.
			_files.erase( day );
			throw;
		}
		const DayFile fresh;
		let& known = restart ? fresh : file;
		if( rewrite && records.empty() && (known.Generation || (!known.Size && !create)) ){//nothing to make an archive of, or it is one.
			_recover.erase( day );
			_unarchived.erase( day );
			_files.erase( day );
			return {};
		}

		let start = StartOf( day, _tz );
		vector<HistoryRecord> preamble;//what a file that isn't there opens with.
		absl::flat_hash_set<NodeIndex> listed;//at the day's start, by this write:  the file's map (spec *Record format*), which a join mid-day doesn't add to.
		if( !known.Size ){
			for( let& [index,node] : members.Current() ){
				preamble.push_back( Added(index, node, start, members) );
				listed.insert( index );
			}
		}
		vector<HistoryRecord> run;
		run.reserve( records.size() );
		for( let& r : records ){
			if( preambled(r, start) )
				listed.insert( r.node_added().node_index() );
		}
		//Sorted, so a node's last is its newest.
		absl::flat_hash_map<NodeIndex,const Proto::DataValue*> newest;
		for( let& r : records ){
			if( !r.has_value() )
				continue;
			let index = r.value().node_index();
			newest[index] = &r.value();
			if( known.Preamble.contains(index) || !listed.insert(index).second )
				continue;
			if( let node = members.Find(index); node ){
				run.push_back( Added(index, *node, start, members) );
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

		if( rewrite ){
			Proto::FileStart first;
			first.set_ts( start );
			first.set_generation( known.Generation+1 );
			//An archive is one run, read from its start.  The preamble and the flush's records follow the file's at any one time.
			auto runs = known.Generation ? vector<Run>{ Run{.Offset=0, .End=known.Size, .Chain=0, .First=std::numeric_limits<Ticks>::min(), .Last=start} } : known.Runs;
			std::ranges::move( run, std::back_inserter(preamble) );
			auto opened = runs.empty() ? nullptr : ms<IO::OpenFile>( file.Path, sl );
			Rewrite y{ day, file.Path, Temp(day), move(stored), mu<Rewrite::State>(file.Path, Temp(day), move(opened), move(runs), move(preamble), first, members.NextIndex, sl) };
			if( file.Generation )
				_files.erase( day );
			return DayWrite{ move(y) };
		}
		auto y = Append( file, day, false, existed, restart, move(preamble), move(run), members.NextIndex );
		y.Stored = move( stored );
		return DayWrite{ move(y) };
	}

	α GroupFiles::PrepareMods( Day day, vector<HistoryRecord>&& records, const Membership& members, SL sl )ε->Pending{
		if( auto p = _mods.find(day); p!=_mods.end() && !p->second.Refused.empty() ){
			if( let changed = IO::Records::StampOf(p->second.Path); changed && changed!=p->second.Stamped )
				_mods.erase( p );//changed since its scan, repaired perhaps, so Open scans it again.
		}
		auto& file = Open( day, sl, {}, true );
		std::error_code ec;
		bool restart{};
		if( !file.Refused.empty() ){
			if( fs::exists(file.Path, ec) || ec )
				throw IO::IOException{ sl, file.Path, ELogLevel::Error, "{}, so the historian won't write to it", file.Refused };
			restart = true;//removed since.
		}
		IO::MakeDirectories( file.Path.parent_path(), sl );
		if( file.Unopened && !restart ){
			try{
				IO::Records::Truncate( file.Path, *file.Unopened, sl );
			}
			catch( ... ){
				_mods.erase( day );
				throw;
			}
			file.Unopened.reset();
		}
		bool existed;
		try{
			let verified = IO::Records::Verify( file, false, restart, sl );
			existed = verified.Existed;
			restart = verified.Restart;
		}
		catch( ... ){
			_mods.erase( day );
			throw;
		}
		vector<HistoryRecord> preamble;
		if( restart || !file.Size ){
			let start = StartOf( day, _tz );
			for( let& [index,node] : members.Current() )
				preamble.push_back( Added(index, node, start, members, false) );
		}
		return Append( file, day, true, existed, restart, move(preamble), move(records), members.NextIndex );
	}

	//The run Prepare and PrepareMods hand out, built by the record layer:  a file with nothing kept opens with its FileStart,
	//an Issued group's next index, and the preamble, sealed on its own; then run, sealed.  The file notes the append as
	//outstanding.
	α GroupFiles::Append( DayFile& file, Day day, bool mods, bool existed, bool restart, vector<HistoryRecord>&& preamble, vector<HistoryRecord>&& run, NodeIndex nextIndex )Ι->Pending{
		const DayFile fresh;
		let& known = restart ? fresh : file;
		let start = StartOf( day, _tz );
		Pending y{ .Date=day, .Mods=mods, .Path=file.Path, .Offset=known.Size, .Existed=existed, .Restart=restart };
		optional<IO::Records::Opening<HistoryRecord>> opening;
		if( !known.Size ){
			Proto::FileStart first;
			first.set_ts( start );
			vector<HistoryRecord> prologue;
			if( nextIndex && !mods )//a modifications file's index is its value file's.
				prologue.emplace_back().mutable_issued()->set_next_node_index( (uint32_t)nextIndex );
			opening.emplace( first, move(prologue), move(preamble) );
		}
		auto built = IO::Records::BuildAppend<HistoryRecord>( known.Size, known.Chain, move(opening), move(run), [&]( const HistoryRecord& r ){
			if( preambled(r, start) )
				y.Preamble.emplace_back( r.node_added().node_index(), listed(r.node_added()) );
		} );
		y.Chain = built.Chain;
		y.End = built.End;
		y.Runs = move( built.Runs );
		y.Bytes = move( built.Bytes );
		file.Hand( y.Offset, y.End );
		return y;
	}

	α Pending::Write( SL sl )ι->IO::WriteAwait{
		return IO::Records::AppendWrite( Path, Offset, move(Bytes), sl );
	}

	α GroupFiles::Commit( Pending&& run, SL sl )ε->void{
		auto& file = ( run.Mods ? _mods : _files ).at( run.Date );
		//Every directory to the root, on a file's first append here:  a new file's write syncs only the names it added,
		//and another group's write, or an earlier one that failed, may have made the rest.
		if( run.Restart || !file.Named )
			IO::SyncDirectories( _root, DayDirectory(run.Date), sl );
		if( run.Restart )
			file = DayFile{ {.Path=move(file.Path)}, {} };
		file.Committed( run.End, run.Chain, move(run.Runs) );
		for( auto& [index,member] : run.Preamble )
			file.Preamble.insert_or_assign( index, move(member) );
		if( run.Mods )
			return;
		for( auto& value : run.Stored ){
			_touched.emplace_back( value.node_index(), *PrimaryTime(value) );
			Newer( move(value) );
		}
		_days.insert( run.Date );
		_unarchived.insert( run.Date );
	}

	α GroupFiles::Commit( Rewrite&& rewrite, TimePoint now, SL sl )ε->void{
		IO::Replace( rewrite.Temp, rewrite.Path, sl );
		auto archive = rewrite.Archive();
		archive.Stamped = IO::Records::StampOf( archive.Path );
		_files.insert_or_assign( rewrite.Date, move(archive) );
		_recover.erase( rewrite.Date );
		_unarchived.erase( rewrite.Date );
		_days.insert( rewrite.Date );
		_rewritten.insert_or_assign( rewrite.Date, now );
		for( auto& value : rewrite.Stored ){
			_touched.emplace_back( value.node_index(), *PrimaryTime(value) );
			Newer( move(value) );
		}
		_unsynced.insert( rewrite.Date );
		try{
			IO::SyncDirectories( _root, DayDirectory(rewrite.Date), sl );
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
					IO::SyncDirectories( _root, DayDirectory(day), sl );
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
		IO::RemoveTemp( rewrite.Temp );
		if( rewrite.Unreadable() )
			_files.erase( rewrite.Date );
	}

	α GroupFiles::Past( Day day, TimePoint now )Ι->bool{
		return now>=DayStart( Day{sys_days{day}+days{1}}, _tz )+_delay;
	}
	α GroupFiles::File( Day day )Ι->fs::path{ return _root/DayDirectory( day )/( _name+".binpb" ); }
	α GroupFiles::ModsFile( Day day )Ι->fs::path{ return _root/DayDirectory( day )/( _name+".mods.binpb" ); }
	α GroupFiles::HasFile( Day day )Ι->bool{
		if( auto p = _files.find(day); p!=_files.end() )
			return p->second.Size>0;
		std::error_code ec;
		return fs::exists( File(day), ec );
	}
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
		return p==_last.end() ? nullptr : &p->second.Value;
	}
	α GroupFiles::Find( Day day )Ι->const DayFile*{
		auto p = _files.find( day );
		return p==_files.end() ? nullptr : &p->second;
	}

	α GroupFiles::Serve( Day day, SL sl )Ε->optional<Served>{ return Serve( _files, File(day), day, sl ); }
	α GroupFiles::ServeMods( Day day, SL sl )ε->optional<Served>{
		std::error_code ec;
		if( !_mods.contains(day) && (fs::exists(ModsFile(day), ec) || ec) ){
			try{
				Open( day, sl, {}, true );
			}
			catch( const IO::IOException& e ){
				e.SetLevel( ELogLevel::Error );//as Serve says one it can't scan.
				throw;
			}
		}
		return Serve( _mods, ModsFile(day), day, sl );
	}
	α GroupFiles::Serve( const std::map<Day,DayFile>& files, const fs::path& path, Day day, SL sl )Ε->optional<Served>{
		sp<IO::OpenFile> file;
		try{
			file = ms<IO::OpenFile>( path, sl );
		}
		catch( IO::IOException& e ){
			if( e.Error!=IO::EIOError::NotFound ){//history a read can't serve, which an operator must see.
				e.SetLevel( ELogLevel::Error );
				throw;
			}
			e.SetLevel( ELogLevel::Debug );//never there, or removed since the process last knew it:  purged, which a read sees as no records.
			return nullopt;
		}
		Served y;
		if( auto p = files.find(day); p!=files.end() ){
			let& known = p->second;
			y.Generation = known.Generation;
			y.Size = known.Size;
			y.Runs = known.Runs;
			y.Preamble = known.Preamble;
		}
		else{
			try{
				if( auto head = archivePreamble(file, sl) ){
					y.Generation = head->Start.generation();
					y.Size = head->Size;
					y.Preamble = std::move( head->Preamble );
				}
				else{//a live file's scan reads it through, so its preamble comes with its runs.
					let start = StartOf( day, _tz );
					Decoder decoder{ [&]( HistoryRecord& r ){
						if( preambled(r, start) )
							y.Preamble.insert_or_assign( r.node_added().node_index(), listed(r.node_added()) );
					} };
					auto scanned = IO::Records::ScanNow( file, StartField, CheckpointField, decoder, sl );
					y.Size = scanned.Size;
					y.Runs = move( scanned.Runs );
				}
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
		y.File = move( file );
		return y;
	}

	α GroupFiles::Corrections( const Membership& members, TimePoint now, SL sl )ι->vector<std::pair<Day,DayWrite>>{
		auto touched = std::exchange( _touched, {} );
		vector<std::pair<Day,DayWrite>> y;
		if( touched.empty() || _days.empty() )
			return y;
		//One look per node and day, from its last record there, since an earlier record's walk reaches no file the last's
		//doesn't and the last correction per member is the one kept:  a look per record read the day once per record.
		std::map<std::pair<NodeIndex,Day>,Ticks> looks;//by node then day, so a node's later days overwrite its earlier days' corrections.
		for( let& [index,time] : touched ){
			auto [p, inserted] = looks.try_emplace( {index, DayOf(time, _tz)}, time );
			p->second = std::max( p->second, time );
		}
		optional<vector<Day>> all;//the directory walk, once, for a record older than the newest day the process knows of.
		std::map<Day,absl::flat_hash_map<NodeIndex,HistoryRecord>> corrections;//each file's, the last per member.
		std::map<Day,optional<Preamble>> archives;//read once for this call and kept no longer.
		let startsOf = [&]( Day day ){ return StartsOf( day, archives, sl ); };
		for( let& [key,time] : looks ){
			let& [index,day] = key;
			if( day>=*_days.rbegin() )
				continue;//no file is later.
			if( !all ){
				try{
					all = Days( _root, _name, false, sl );
				}
				catch( const Exception& ){//said as it went:  nothing can be corrected.
					return y;
				}
			}
			optional<Proto::DataValue> value;
			bool found{};
			for( auto later = std::ranges::upper_bound(*all, day); later!=all->end(); ++later ){
				let starts = startsOf( *later );
				if( !starts )
					continue;
				auto s = starts->find( index );
				if( s==starts->end() )
					continue;//not a member there.
				let& start = s->second.Start;
				let startTime = start ? PrimaryTime( *start ) : nullopt;
				if( startTime && *startTime>time )
					break;//copies a record after the change, as the files after it do.
				if( !std::exchange(found, true) ){
					//The record itself when it is the node's newest; otherwise the last through its time, the edits applied.
					if( auto p = _last.find(index); p!=_last.end() && *PrimaryTime(p->second.Value)==time )
						value = p->second.Value;
					else if( members.Last ){
						try{
							value = members.Last( index, time );
						}
						catch( Exception& e ){//the files after it are left as they are, as one that can't be prepared is.
							e.SetLevel( ELogLevel::Error );
							break;
						}
					}
				}
				if( startTime && *startTime==time && (bool)start==(bool)value && (!value || Same(*start, *value)) )
					continue;//copies the record itself:  made in this flush, after it was written.
				//One the group no longer knows, a rejoined node's earlier index an edit reached, as the file lists it.
				let node = members.Find( index ).value_or( Membership::Node{s->second.Node, true} );
				auto record = Added( index, node, StartOf(*later, _tz), members, false );
				if( value ){
					*record.mutable_node_added()->mutable_start() = *value;
					record.mutable_node_added()->mutable_start()->clear_node_index();
				}
				corrections[*later].insert_or_assign( index, move(record) );
			}
		}
		for( auto& [day,byNode] : corrections ){
			vector<HistoryRecord> records;
			records.reserve( byNode.size() );
			for( auto& [_,r] : byNode )
				records.push_back( move(r) );
			try{
				auto write = Prepare( day, move(records), members, now, sl );
				if( !std::holds_alternative<std::monostate>(write) )
					y.emplace_back( day, move(write) );
			}
			catch( Exception& e ){//the next record or edit that reaches the file corrects it.
				e.SetLevel( ELogLevel::Error );
			}
		}
		return y;
	}

	α GroupFiles::ListedAfter( Day day, const vector<NodeIndex>& indexes, SL sl )ε->vector<NodeIndex>{
		vector<NodeIndex> y;
		if( _days.empty() || day>=*_days.rbegin() )
			return y;//no file is later.
		std::map<Day,optional<Preamble>> archives;
		let all = Days( _root, _name, false, sl );
		for( auto later = std::ranges::upper_bound(all, day); later!=all.end() && y.empty(); ++later ){
			if( let starts = StartsOf(*later, archives, sl) )
				std::ranges::copy_if( indexes, std::back_inserter(y), [starts]( NodeIndex index ){ return starts->contains( index ); } );
		}
		return y;
	}
	α GroupFiles::StartsOf( Day day, std::map<Day,optional<Preamble>>& archives, SL sl )ι->const Preamble*{
		if( !_files.contains(day) ){
			auto [p, inserted] = archives.try_emplace( day );
			if( inserted ){
				try{
					sp<IO::OpenFile> file;
					try{
						file = ms<IO::OpenFile>( File(day), sl );
					}
					catch( const IO::IOException& e ){
						if( e.Error!=IO::EIOError::NotFound )
							throw;
					}
					if( !file )
						p->second.emplace();//no file lists nothing.
					else if( auto head = archivePreamble(file, sl) )
						p->second = std::move( head->Preamble );
				}
				catch( const Exception& ){//said as it went.
					archives.erase( p );
					return nullptr;
				}
			}
			if( p->second )
				return &*p->second;
		}
		try{
			let& file = Open( day, sl );
			return file.Refused.empty() ? &file.Preamble : nullptr;
		}
		catch( const Exception& ){//said as it went.
			return nullptr;
		}
	}

	α Days( const fs::path& root, sv name, bool mods, SL sl )ε->vector<Day>{
		return IO::Records::Days( root, string{name}+(mods ? ".mods.binpb" : ".binpb"), sl );
	}
}