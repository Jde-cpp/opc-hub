#include "ArchiveAwait.h"
#include <chrono>
#include <filesystem>
#include <jde/fwk/chrono.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/app/log/DailyLoadAwait.h>

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
	α ArchiveAwait::Execute()ι->TAwait<CoLockGuard>::Task{
		Archive( co_await LockKeyAwait{_dailyFile.string()} );
	}
	α ArchiveAwait::Archive( optional<CoLockGuard> lock )ι->TAwait<vector<App::Log::Proto::FileEntry>>::Task{
		vector<App::Log::Proto::FileEntry> entries;
		try{
			entries = co_await DailyLoadAwait{ _dailyFile, EDailyLoad::Archive };//the lock is ours & stays ours; the daily file is read, ProtoLog's buffer is not.
		}
		catch( runtime_error& e ){
			lock.reset();
			ResumeExp( move(e) );
			co_return;
		}
		using namespace std::chrono;
		flat_map<year_month_day, App::Log::Proto::ArchiveFile> archives;
		std::map<uuid,App::Log::Proto::String> strings;
		for( auto& entry : entries ){
			using enum App::Log::Proto::FileEntry::ValueCase;
			switch( entry.value_case() ){
			case kStr:
				strings[ToGuid( entry.str().id() )] = move( *entry.mutable_str() );
				break;
			case kEntry:{
				let day = Chrono::LocalYMD( Protobuf::ToTimePoint(entry.entry().time()), _tz );
				*archives[day].add_entries() = move( *entry.mutable_entry() );
				}break;
			case kExternalEntry:{
				let day = Chrono::LocalYMD( Protobuf::ToTimePoint(entry.external_entry().time()), _tz );
				*archives[day].add_externalentries() = move( *entry.mutable_external_entry() );
				}break;
			default:
				WARN( "Unhandled FileEntry case '{}' - not archived.", underlying(entry.value_case()) );
				break;
			}
		}
		for( auto&& [ymd,archive] : archives ){
			flat_set<uuid> templateIds, fileIds, functionIds, argIds;
			auto add = [&strings]( str idBytes, flat_set<uuid>& added, auto* collection ){
				let id = ToGuid( idBytes );
				if( !added.emplace(id).second )
					return;
				if( auto p = strings.find(id); p!=strings.end() )
					*collection->Add() = p->second;
			};
			auto addStrings = [&]( auto& entry ){
				add( entry.template_id(), templateIds, archive.mutable_templates() );
				add( entry.file_id(), fileIds, archive.mutable_files() );
				add( entry.function_id(), functionIds, archive.mutable_functions() );
				for( let& argId : entry.args() )
					add( argId, argIds, archive.mutable_args() );
			};
			for( int i=0; i<archive.entries_size(); ++i )
				addStrings( *archive.mutable_entries(i) );
			for( int i=0; i<archive.externalentries_size(); ++i )
				addStrings( *archive.mutable_externalentries(i) );
		}
		Save( move(archives), move(lock) );
	}
	struct ArchiveFileAwait final : VoidAwait{
		ArchiveFileAwait( year_month_day ymd, const fs::path& root, App::Log::Proto::ArchiveFile&& archive, SL sl )ε;
		α Suspend()ι->void override;
	private:
		α Append()ι->TAwait<string>::Task;
		α Save( App::Log::Proto::ArchiveFile&& values )ι->VoidAwait::Task;
		App::Log::Proto::ArchiveFile _archive;
		fs::path _file;
	};
	α ArchiveAwait::Save( flat_map<year_month_day, App::Log::Proto::ArchiveFile> archives, optional<CoLockGuard> lock )ι->VoidAwait::Task{
		for( auto&& [ymd,archive] : archives ){
			try{
				co_await ArchiveFileAwait{ ymd, _path, move(archive), _sl };
			}
			catch( runtime_error& e ){
				lock.reset();//the daily file survives a failed archive - drop the lock so the next flush & round can proceed.
				ResumeExp( move(e) );
				co_return;
			}
		}
		std::error_code ec;
		fs::remove( _dailyFile, ec );
		lock.reset();//before resuming: Resume() runs the continuation on this stack, which must not inherit the daily file's lock.
		if( ec )//the archives are written, but the round's postcondition - the daily file is gone - does not hold, so say so.
			ResumeExp( IO::IOException{_dailyFile, (uint32)ec.value(), Ƒ("Could not remove the archived daily file: {}", ec.message()), _sl} );
		else
			Resume();
	}
	Ω getFile( year_month_day ymd, const fs::path& root )ε->fs::path{
		let dir = root/std::to_string( (int)ymd.year() )/std::to_string( (unsigned)ymd.month() )/std::to_string( (unsigned)ymd.day() );
		if( !fs::exists(dir) )
			fs::create_directories( dir );
		return dir/"archive.binpb";
	}
	ArchiveFileAwait::ArchiveFileAwait( year_month_day ymd, const fs::path& root, App::Log::Proto::ArchiveFile&& archive, SL sl )ε:
		VoidAwait{ sl },
		_archive{ move(archive) },
		_file{ getFile(ymd, root) }
	{}

	α ArchiveFileAwait::Suspend()ι->void{
		std::error_code ec;
		if( let exists = fs::exists(_file, ec); ec )
			ResumeExp( IO::IOException{_file, (uint32)ec.value(), Ƒ("Could not stat the archive file: {}", ec.message()), _sl} );
		else if( exists )
			Append();
		else
			Save( move(_archive) );
	}
	α ArchiveFileAwait::Append()ι->TAwait<string>::Task{
		App::Log::Proto::ArchiveFile cumulative;
		try{ //the whole merge is guarded: an unparseable archive must fail this await, not escape into a coroutine nobody is awaiting.
			auto content = co_await IO::ReadAwait( _file );
			std::map<uuid,App::Log::Proto::String> args, templates, files, functions;
			std::multimap<TimePoint,App::Log::Proto::LogEntryFile> entries;
			std::multimap<TimePoint,App::Log::Proto::LogEntryFileExternal> externalEntries;
			auto existing = Protobuf::Deserialize<App::Log::Proto::ArchiveFile>( move(content) );
			auto addEntries = [&entries]( App::Log::Proto::ArchiveFile& af ){
				for( int i=0; i<af.entries_size(); ++i ){
					auto& entry = *af.mutable_entries( i );
					entries.emplace_hint( entries.end(), Protobuf::ToTimePoint(entry.time()), move(entry) );
				}
			};
			auto addExternalEntries = [&externalEntries]( App::Log::Proto::ArchiveFile& af ){
				for( int i=0; i<af.externalentries_size(); ++i ){
					auto& entry = *af.mutable_externalentries( i );
					externalEntries.emplace_hint( externalEntries.end(), Protobuf::ToTimePoint(entry.time()), move(entry) );
				}
			};
			addEntries( existing );
			addEntries( _archive );
			addExternalEntries( existing );
			addExternalEntries( _archive );
#define ADD_STRINGS( collection, af ) \
			for( int i=0; i<af.collection##_size(); ++i ){ \
				auto& s = *af.mutable_##collection( i ); \
				collection[ToGuid( s.id() )] = move( s ); \
			}
			ADD_STRINGS( args, existing );
			ADD_STRINGS( args, _archive );
			ADD_STRINGS( templates, existing );
			ADD_STRINGS( templates, _archive );
			ADD_STRINGS( files, existing );
			ADD_STRINGS( files, _archive );
			ADD_STRINGS( functions, existing );
			ADD_STRINGS( functions, _archive );
#undef ADD_STRINGS

			for( auto& [_,entry] : entries )
				*cumulative.add_entries() = move( entry );
			for( auto& [_,entry] : externalEntries )
				*cumulative.add_externalentries() = move( entry );
			for( auto& [_,s] : args )
				*cumulative.add_args() = move( s );
			for( auto& [_,s] : templates )
				*cumulative.add_templates() = move( s );
			for( auto& [_,s] : files )
				*cumulative.add_files() = move( s );
			for( auto& [_,s] : functions )
				*cumulative.add_functions() = move( s );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			co_return;
		}
		Save( move(cumulative) );
	}
	α ArchiveFileAwait::Save( App::Log::Proto::ArchiveFile&& values )ι->VoidAwait::Task{
		//truncate: values is the complete file - Append already merged what is on disk into it, so appending would write every existing entry a second time (the file grew ~x3.7 per archive round until it no longer parsed).
		//Truncating the target in place published a half-written archive: CREATE_ALWAYS puts the name on disk at open, so a
		//reader polling for the file found it a whole write early - and on windows could not even open it, the writer's sharing
		//mode locking readers out until the last chunk landed (ERROR_SHARING_VIOLATION).  A sibling temp renamed over the target
		//is atomic within the filesystem, so the name only ever refers to a complete archive.  One fixed temp name is enough:
		//ArchiveAwait::Save archives days one at a time under the daily file's lock, and a leftover is inert either way -
		//ArchiveFiles() matches "archive.binpb" exactly, and the next round's CREATE_ALWAYS overwrites it.
		let temp = fs::path{ _file }.concat( ".tmp" );
		try{
			co_await IO::WriteAwait( temp, Protobuf::ToString(values), true, IO::EWriteMode::Truncate, _tags );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
			co_return;
		}
		std::error_code ec;
		fs::rename( temp, _file, ec );
		if( !ec )
			Resume();
		else{
			std::error_code removeEc;
			fs::remove( temp, removeEc );//the target still holds the previous archive; a stray temp would only mislead whoever looks at the directory next.
			ResumeExp( IO::IOException{_file, (uint32)ec.value(), Ƒ("Could not rename '{}' onto the archive: {}", temp.string(), ec.message()), _sl} );
		}
	}
	α ArchiveLoadAwait::Suspend()ι->void{
		try{
			ArchiveFile archive{ _query.Filter(), move(_dailyFile) };
			let isComplete = archive.IsComplete( _query );
			TRACE( "daily file entries: {}, templates: {}, files: {}, functions: {}, args: {}, complete: {}",
				archive.Entries.size(),
				archive.Templates.size(),
				archive.Files.size(),
				archive.Functions.size(),
				archive.Args.size(),
				isComplete );
			if( isComplete )
				Resume( move(archive) );
			else
			 	LoadArchives( move(archive) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α ArchiveLoadAwait::ArchiveFiles()ε->flat_map<year_month_day, fs::path>{
		flat_map<year_month_day, fs::path> y;
		for( let& yearEntry : fs::directory_iterator(_root) ){
			if( let yearV = yearEntry.is_directory() ? Str::TryTo<int>(yearEntry.path().stem().string()) : nullopt; yearV ){
				for( let& monthEntry : fs::directory_iterator(yearEntry.path()) ){
					if( let monthV = monthEntry.is_directory() ? Str::TryTo<unsigned>(monthEntry.path().stem().string()) : nullopt; monthV ){
						for( let& dayEntry : fs::directory_iterator(monthEntry.path()) ){
							if( let dayV = dayEntry.is_directory() ? Str::TryTo<unsigned>(dayEntry.path().stem().string()) : nullopt; dayV ){
								let ymd = year_month_day{ year{*yearV}, month{*monthV}, day{*dayV} };
								if( ymd.ok() && fs::exists(dayEntry.path()/"archive.binpb") )
									y[ymd] = dayEntry.path()/"archive.binpb";
							}
						}
					}
				}
			}
		}
		return y;
	}

	α ArchiveLoadAwait::LoadArchives( ArchiveFile archive )ι->StringAwait::Task{
		try{
			auto process = [this]( ArchiveFile& archive, string&& content )ε->bool {
				archive.Append( _query, Protobuf::Deserialize<App::Log::Proto::ArchiveFile>(move(content)) );
				return archive.IsComplete( _query );
			};

			let files = ArchiveFiles();
			if( !files.size() ){
				Resume( move(archive) );
				co_return;
			}
			let localStart = _startTime.has_value() ? year_month_day{ floor<days>(_tz.to_local(*_startTime)) } : files.begin()->first;
			let localEnd = _endTime.has_value() ? year_month_day{ ceil<days>(_tz.to_local(*_endTime)) } : files.rbegin()->first;
			auto test = [localStart, localEnd]( year_month_day fileDate )ι->bool {
				return fileDate >= localStart && fileDate <= localEnd;
			};

			if( let& ob = _query.OrderByJson(); ob.size() && ob[0].first=="time" && ob[0].second ){
				for( auto&& [ymd,file] : files ){
					if( !test(ymd) )
						continue;
					auto content = co_await IO::ReadAwait( file );
					if( process(archive, move(content)) ){
						Resume( move(archive) );
						co_return;
					}
				}
			}
			else{
				for( auto it = files.rbegin(); it!=files.rend(); ++it ){
					if( !test(it->first) )
						continue;
					auto content = co_await IO::ReadAwait( it->second );
					if( process(archive, move(content)) ){
						Resume( move(archive) );
						co_return;
					}
				}
			}
			Resume( move(archive) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}