#include "ArchiveAwait.h"
#include <chrono>
#include <filesystem>
#include <jde/fwk/chrono.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/app/log/DailyLoadAwait.h>
#include <jde/app/proto/LogProto.h>

#include <boost/uuid/uuid_io.hpp>

#define let const auto

namespace Jde::App{
	static constexpr ELogTags _tags{ ELogTags::ExternalLogger };
	using Protobuf::ToGuid;
	//The daily file's lock is held from before the read until after Save's fs::remove.  DailyLoadAwait drops it at the end
	//of the read, which left the read→remove window unguarded: a second archive round could read the same entries before
	//the remove ran and archive them twice (`entries` below is a multimap, so the merge preserves the duplicate), and a
	//ProtoLog flush could append entries that the remove then discarded unarchived.
	//The lock covers the daily *file* only - ProtoLog::Write never takes it, it only takes ProtoLog::_mutex.  So the round
	//archives what is durably in the file and nothing else; ProtoLog::Save flushes its buffer into the file before starting
	//a round, and entries written after that stay buffered for the next one.
	//The daily file's entries, by local day, each day's archive holding the strings its entries name.
	Ω byDay( vector<App::Log::Proto::FileEntry>&& entries, const std::chrono::time_zone& tz )ι->flat_map<year_month_day, DayArchive>{
		flat_map<year_month_day, DayArchive> archives;
		std::map<uuid,App::Log::Proto::String> strings;
		for( auto& entry : entries ){
			using enum App::Log::Proto::FileEntry::ValueCase;
			switch( entry.value_case() ){
			case kStr:
				strings[ToGuid( entry.str().id() )] = move( *entry.mutable_str() );
				break;
			case kEntry:
			case kExternalEntry:{//a daily file an older build wrote:  folded, so archives only ever hold `entries`.
				auto logEntry = entry.value_case()==kEntry ? move( *entry.mutable_entry() ) : LogProto::ToEntry( move(*entry.mutable_external_entry()) );
				let day = Chrono::LocalYMD( Protobuf::ToTimePoint(logEntry.time()), tz );
				*archives[day].add_entries() = move( logEntry );
				}break;
			default:
				WARN( "Unhandled FileEntry case '{}' - not archived.", underlying(entry.value_case()) );
				break;
			}
		}
		for( auto&& [ymd,archive] : archives ){
			std::array<flat_set<uuid>,StringKinds.size()> added;
			auto add = [&]( EStringKind kind, const string& idBytes ){
				let id = ToGuid( idBytes );
				if( !added[(uint8)kind].emplace(id).second )
					return;
				if( auto p = strings.find(id); p!=strings.end() )
					*ArchiveStrings( archive, kind ).Add() = p->second;
			};
			for( let& entry : archive.entries() )
				ForEachStringId( entry, add );
		}
		return archives;
	}
	//What is on disk for a day with what this round adds to it:  entries in time order, each string once.
	Ω merge( DayArchive&& existing, DayArchive&& added )ε->DayArchive{
		std::multimap<TimePoint,App::Log::Proto::LogEntryFile> entries;
		std::array<std::map<uuid,App::Log::Proto::String>,StringKinds.size()> strings;
		for( auto* af : {&existing, &added} ){
			for( auto& entry : *af->mutable_entries() )
				entries.emplace( Protobuf::ToTimePoint(entry.time()), move(entry) );
			for( auto& external : *af->mutable_externalentries() ){//an archive an older build wrote:  rewritten folded.
				auto entry = LogProto::ToEntry( move(external) );
				entries.emplace( Protobuf::ToTimePoint(entry.time()), move(entry) );
			}
			for( let kind : StringKinds ){
				for( auto& s : ArchiveStrings(*af, kind) )
					strings[(uint8)kind][ToGuid( s.id() )] = move( s );
			}
		}
		DayArchive y;
		for( auto& [_,entry] : entries )
			*y.add_entries() = move( entry );
		for( let kind : StringKinds ){
			for( auto& [_,s] : strings[(uint8)kind] )
				*ArchiveStrings( y, kind ).Add() = move( s );
		}
		return y;
	}
	//The daily file's lock is held from before the read until after the daily file's fs::remove.  DailyLoadAwait drops it at
	//the end of the read, which left the read→remove window unguarded: a second archive round could read the same entries
	//before the remove ran and archive them twice (`entries` in merge is a multimap, so the merge preserves the duplicate),
	//and a ProtoLog flush could append entries that the remove then discarded unarchived.
	//The lock covers the daily *file* only - ProtoLog::Write never takes it, it only takes ProtoLog::_mutex.  So the round
	//archives what is durably in the file and nothing else; ProtoLog::Save flushes its buffer into the file before starting
	//a round, and entries written after that stay buffered for the next one.
	α ArchiveAwait::Execute()ι->TAwait<CoLockGuard>::Task{
		optional<CoLockGuard> lock;
		try{
			lock = co_await LockKeyAwait{ _dailyFile.string() };
			//the lock is ours & stays ours; the daily file is read, ProtoLog's buffer is not.
			for( auto&& [ymd,archive] : byDay(co_await Any(DailyLoadAwait{_dailyFile, EDailyLoad::Archive}), _tz) ){
				let file = ArchiveDayFile( _path, ymd );
				fs::create_directories( file.parent_path() );
				std::error_code ec;
				if( let exists = fs::exists(file, ec); ec )
					throw IO::IOException{ file, (uint32)ec.value(), Ƒ("Could not stat the archive file: {}", ec.message()), _sl };
				else if( exists ){
					IO::ReadAwait read{ file };//by reference: a ReadAwait does not move.
					archive = merge( Protobuf::Deserialize<DayArchive>(co_await Any(read)), move(archive) );
				}
				//The whole day, truncated in:  the archive holds the full merge, so appending wrote every existing entry a second
				//time (the file grew ~x3.7 per round until it no longer parsed).  Truncating the target in place published a
				//half-written archive: CREATE_ALWAYS puts the name on disk at open, so a reader polling for the file found it a
				//whole write early - and on windows could not even open it, the writer's sharing mode locking readers out until the
				//last chunk landed (ERROR_SHARING_VIOLATION).  A sibling temp renamed over the target is atomic within the
				//filesystem, so the name only ever refers to a complete archive.  One fixed temp name is enough:  days are written
				//one at a time under the daily file's lock, and a leftover is inert either way - ArchiveDayFiles() matches
				//"archive.binpb" exactly, and the next round's CREATE_ALWAYS overwrites it.
				let temp = fs::path{ file }.concat( ".tmp" );
				IO::WriteAwait write{ temp, Protobuf::ToString(archive), true, IO::EWriteMode::Truncate, _tags };
				co_await Any( write );
				if( fs::rename(temp, file, ec); ec ){
					std::error_code removeEc;
					fs::remove( temp, removeEc );//the target still holds the previous archive; a stray temp would only mislead whoever looks at the directory next.
					throw IO::IOException{ file, (uint32)ec.value(), Ƒ("Could not rename '{}' onto the archive: {}", temp.string(), ec.message()), _sl };
				}
			}
			std::error_code ec;
			if( fs::remove(_dailyFile, ec); ec )//the archives are written, but the round's postcondition - the daily file is gone - does not hold, so say so.
				throw IO::IOException{ _dailyFile, (uint32)ec.value(), Ƒ("Could not remove the archived daily file: {}", ec.message()), _sl };
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
	α ArchiveDayFile( const fs::path& root, year_month_day ymd )ι->fs::path{
		return root/std::to_string( (int)ymd.year() )/std::to_string( (unsigned)ymd.month() )/std::to_string( (unsigned)ymd.day() )/archiveFileName;
	}
	Ω numberedDirs( const fs::path& dir )ε->vector<std::pair<int,fs::path>>{
		vector<std::pair<int,fs::path>> y;
		for( let& entry : fs::directory_iterator(dir) )
			if( let v = entry.is_directory() ? Str::TryTo<int>(entry.path().stem().string()) : nullopt; v )
				y.emplace_back( *v, entry.path() );
		return y;
	}
}
namespace Jde{
	α App::ArchiveDayFiles( const fs::path& root )ε->flat_map<year_month_day, fs::path>{
		flat_map<year_month_day, fs::path> y;
		for( let& [yearV, yearDir] : numberedDirs(root) )
			for( let& [monthV, monthDir] : numberedDirs(yearDir) )
				for( let& [dayV, dayDir] : numberedDirs(monthDir) )
					if( let ymd = year_month_day{ year{yearV}, month{(unsigned)monthV}, day{(unsigned)dayV} }; ymd.ok() && fs::exists(dayDir/archiveFileName) )
						y[ymd] = dayDir/archiveFileName;
		return y;
	}
}
