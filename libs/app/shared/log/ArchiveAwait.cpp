#include "ArchiveAwait.h"
#include <chrono>
#include <filesystem>
#include <jde/fwk/chrono.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/io/durable.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/io/records/Days.h>
#include <jde/app/log/ArchiveQuery.h>
#include <jde/app/log/ProtoLog.h>

#include <boost/uuid/uuid_io.hpp>

#define let const auto

namespace Jde::App{
	static constexpr ELogTags _tags{ ELogTags::ExternalLogger };
	using Protobuf::ToGuid;
	using Log::FileEntry;
	using namespace std::chrono;
	namespace{
		//What a round takes of the daily file:  its identity and size, which each archive it writes notes, every string
		//record, and each day's entries, each with its end in the file, so a day whose archive took the file through there
		//already passes over it.  The size is a run's end, so an entry ends at or before it just when its run does.
		struct Snapshot final{
			int64_t Identity{};
			uint Through{};
			std::map<uuid,FileEntry> Strings;
			std::map<year_month_day,vector<std::pair<uint,FileEntry>>> Days;
		};
		//The day's start in tz, ns since the epoch:  the archive's FileStart ts.
		Ω dayStart( year_month_day day, const time_zone& tz )ι->int64_t{
			return Log::Ns( TimePoint{tz.to_sys(local_days{day}, choose::earliest)} );
		}
		//The rewrite's head:  the archive's strings and its Merged, which the new Merged in the prologue replaces.  The
		//strings stay, each once, and the day's new ones follow them.
		Ω hooks( std::map<uuid,FileEntry>& strings )ι->Log::Rewrite::Hooks{
			return {
				.IsHead=[]( const FileEntry& r ){ return r.has_str() || r.has_merged(); },
				.FoldHead=[&strings]( vector<FileEntry>& head ){
					vector<FileEntry> folded;
					flat_set<uuid> seen;
					for( auto& r : head ){
						if( r.has_str() && seen.emplace(ToGuid(r.str().id())).second )
							folded.push_back( move(r) );
					}
					for( auto& [id,s] : strings ){
						if( seen.emplace(id).second )
							folded.push_back( s );
					}
					head = move( folded );
				}
			};
		}
	}
	α ArchiveAwait::Execute()ι->TAwait<CoLockGuard>::Task{
		optional<CoLockGuard> lock;
		try{
			let dailyPath = _log.DailyFile();
			lock = co_await LockKeyAwait{ dailyPath.string() };
			//the lock is ours & stays ours; the daily file is read, ProtoLog's buffer is not.
			auto& file = _log._file;
			if( !file.Current() )
				file = co_await Log::DailyOpenAwait{ dailyPath, file };
			if( !file.Refused.empty() )
				throw IO::IOException{ _sl, file.Path, ELogLevel::Error, "{}, so it can't be archived", file.Refused };
			Snapshot snapshot{ .Identity=file.Identity, .Through=file.Size };
			if( file.Runs.size() ){
				vector<uint> ends;
				auto records = co_await Log::RunsReadAwait{ ms<IO::OpenFile>(file.Path, _sl), file.Runs, &ends };
				for( uint i=0; i<records.size(); ++i ){
					auto& r = records[i];
					if( r.has_str() )
						snapshot.Strings.try_emplace( ToGuid(r.str().id()), move(r) );
					else if( r.has_entry() ){
						let day = Chrono::LocalYMD( Protobuf::ToTimePoint(r.entry().time()), _log.TimeZone() );
						snapshot.Days[day].emplace_back( ends[i], move(r) );
					}
					else
						WARN( "Unhandled FileEntry case '{}' - not archived.", underlying(r.value_case()) );
				}
			}
			for( auto& [day,entries] : snapshot.Days ){
				let path = ArchiveDayFile( _log.Root(), day );
				let temp = fs::path{ path }.concat( ".tmp" );
				fs::create_directories( path.parent_path() );
				sp<IO::OpenFile> existing;
				try{
					existing = ms<IO::OpenFile>( path, _sl );
				}
				catch( IO::IOException& e ){
					if( e.Error!=IO::EIOError::NotFound )
						throw;
					e.SetLevel( ELogLevel::Debug );
				}
				optional<Log::ArchiveHead> head;
				vector<Log::Run> runs;
				vector<FileEntry> late;
				std::map<uuid,FileEntry> strings;//the day's new entries'.
				if( existing ){
					let size = existing->Size( _sl );
					IO::ReadAwait read{ existing, IO::ReadOptions{.Offset=0, .Size=std::min<uint>(size, Log::ArchiveHeadBytes), .Tags=_tags} };//by reference: a ReadAwait does not move.
					head = Log::HeadOf( co_await Any(read) );
					if( !head ){//as the historian leaves a file that isn't its own:  with the records for it dropped.
						ERR( "'{}' isn't an archive the record layer wrote:  its {} entries for {} are dropped, and the log won't write to it until it is repaired or removed.", path.string(), entries.size(), IO::Records::DayDirectory(day).string() );
						continue;
					}
					//an archive is one run, read from its start, before the day's new entries at any one time.
					runs = { Log::Run{.Offset=0, .End=size, .Chain=0, .First=std::numeric_limits<int64_t>::min(), .Last=std::numeric_limits<int64_t>::max()} };
					if( head->Merged && head->Merged->daily()==snapshot.Identity )
						std::erase_if( entries, [&]( let& e ){ return e.first<=head->Merged->through(); } );
					if( entries.empty() )
						continue;//a round that failed after this day took them already.
				}
				for( auto& [_,entry] : entries ){
					ForEachStringId( entry.entry(), [&]( EStringKind, const string& idBytes ){
						let id = ToGuid( idBytes );
						if( auto p = snapshot.Strings.find(id); p!=snapshot.Strings.end() )
							strings.try_emplace( id, p->second );
					});
					late.push_back( move(entry) );
				}
				std::ranges::stable_sort( late, {}, []( let& r ){ return *Log::Time( r ); } );
				IO::Proto::FileStart start;
				start.set_ts( dayStart(day, _log.TimeZone()) );
				start.set_generation( (head ? head->Start.generation() : 0)+1 );
				FileEntry merged;
				merged.mutable_merged()->set_daily( snapshot.Identity );
				merged.mutable_merged()->set_through( snapshot.Through );
				Log::Rewrite rewrite{ path, temp, move(existing), move(runs), move(late), start, {move(merged)}, hooks(strings), IO::Records::PartBytes, _sl };
				try{
					for( auto part = rewrite.Next(); part!=IO::Records::EPart::Done; part = rewrite.Next() ){
						if( part==IO::Records::EPart::More ){//the rewrite's merge wants bytes of the archive it merges.
							let w = *rewrite.Wants();
							IO::ReadAwait read{ rewrite.File(), IO::ReadOptions{.Offset=w.Offset, .Size=w.Size, .Tags=_tags} };
							rewrite.Feed( co_await Any(read) );
						}
						else{
							auto write = rewrite.Write( _sl );
							co_await Any( write );
						}
					}
					IO::Replace( temp, path, _sl );//not fs::rename:  MoveFileEx on Windows, refused an archive a reader has open.
				}
				catch( ... ){
					IO::RemoveTemp( temp );//the target still holds the previous archive; a stray temp would only mislead whoever looks at the directory next.
					if( !rewrite.Unreadable() )
						throw;
					ERR( "'{}' can't be read through:  its {} entries for {} are dropped, and the log won't write to it until it is repaired or removed.", path.string(), entries.size(), IO::Records::DayDirectory(day).string() );
				}
			}
			std::error_code ec;
			if( fs::remove(dailyPath, ec); ec )//the archives are written, but the round's postcondition - the daily file is gone - does not hold, so say so.
				throw IO::IOException{ dailyPath, (uint32)ec.value(), Ƒ("Could not remove the archived daily file: {}", ec.message()), _sl };
			file.Removed();
		}
		catch( runtime_error& e ){
			lock.reset();//the daily file survives a failed round - drop the lock so the next flush & round can proceed.
			ResumeExp( move(e) );
			co_return;
		}
		lock.reset();//before resuming: Resume() runs the continuation on this stack, which must not inherit the daily file's lock.
		Resume();
	}
	constexpr sv archiveFileName{ "archive.binpb" };
}
namespace Jde{
	α App::ArchiveDayFile( const fs::path& root, year_month_day ymd )ι->fs::path{
		return root/IO::Records::DayDirectory( ymd )/archiveFileName;
	}
	α App::ArchiveDayFiles( const fs::path& root )ε->flat_map<year_month_day, fs::path>{
		flat_map<year_month_day, fs::path> y;
		for( let day : IO::Records::Days(root, archiveFileName) )
			y.emplace( day, ArchiveDayFile(root, day) );
		return y;
	}
}