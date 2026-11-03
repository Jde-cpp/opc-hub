#include "LinuxDrive.h"
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <jde/fwk/io/file.h>

#define let const auto
namespace Jde::IO::Drive{
	constexpr ELogTags _tags{ ELogTags::IO };

	Drive::NativeDrive _native;
	α Native()ι->IDrive&{ return _native; }

	α GetAttributes( fs::path path )->tuple<TimePoint,TimePoint,uint>{
		struct stat attrib;
		stat( path.string().c_str(), &attrib );
		let size = attrib.st_size;

		const TimePoint modifiedTime = Clock::from_time_t( attrib.st_mtim.tv_sec );//+std::chrono::nanoseconds( attrib.st_mtim.tv_nsec );TODO
		const TimePoint accessedTime = Clock::from_time_t( attrib.st_atim.tv_sec );//+std::chrono::nanoseconds( attrib.st_atim.tv_nsec );
		return make_tuple( modifiedTime,accessedTime, size );
	}
	struct DirEntry : IDirEntry{
		DirEntry( const fs::path& path ):
			DirEntry( fs::directory_entry(path) )
		{}

		DirEntry( const fs::directory_entry& entry ){
			let path = Path = entry.path();
			let status = entry.status();
			if( fs::is_directory(status) )
				Flags = EFileFlags::Directory;

			let& [modified, accessed, size] = GetAttributes( entry );
			Size = size;
			ModifiedTime = modified;
			AccessedTime = accessed;
		}
	};
	α NativeDrive::Get( const fs::path& path )ε->up<IDirEntry>{
		return mu<DirEntry>( path );
	}
	α NativeDrive::Recursive( const fs::path& dir, SL sl )ε->flat_map<string,up<IDirEntry>>{
		CHECK_PATH( dir, sl );
		let dirString = dir.string();
		flat_map<string,up<IDirEntry>> entries;

		std::function<void(const fs::directory_entry&)> fnctn;
		fnctn = [&dirString, &entries, &fnctn]( const fs::directory_entry& entry ){
			let status = entry.status();
			let relativeDir = entry.path().string().substr( dirString.size()+1 );

			if( fs::is_directory(status) || fs::is_regular_file(status) ){
				entries.emplace( relativeDir, mu<DirEntry>(entry.path()) );
				if( fs::is_directory(status) )
					for_each( fs::directory_iterator(entry.path()), fnctn );
			}
		};
		for_each( fs::directory_iterator(dir), fnctn );

		return entries;
	}
	α to_timespec( const TimePoint& time )->timespec{
		let sinceEpoch = time.time_since_epoch();
		let total = duration_cast<std::chrono::nanoseconds>( duration_cast<std::chrono::nanoseconds>( sinceEpoch )-duration_cast<std::chrono::seconds>( sinceEpoch ) ).count();

		return { Clock::to_time_t(time), total };
	}
	//stamps the entry's modified/accessed times onto `path` when it carries one; an unset accessed time follows modified.
	//Shared by CreateFolder and Save - it used to be pasted into each.
	Ω applyTimes( const fs::path& path, const IDirEntry& dirEntry )ι->void{
		if( dirEntry.ModifiedTime.time_since_epoch()==Duration::zero() )
			return;
		let modifiedTime = to_timespec( dirEntry.ModifiedTime );
		let accessedTime = dirEntry.AccessedTime.time_since_epoch()==Duration::zero() ? modifiedTime : to_timespec( dirEntry.AccessedTime );
		timespec values[] = {accessedTime, modifiedTime};
		if( utimensat(AT_FDCWD, path.string().c_str(), values, 0) )
			WARN( "utimensat returned {} on {}", errno, path.string() );
	}
	α NativeDrive::CreateFolder( const fs::path& dir, const IDirEntry& dirEntry )ε->up<IDirEntry>{
		fs::create_directory( dir );
		applyTimes( dir, dirEntry );
		return mu<DirEntry>( dir );
	}
	α NativeDrive::Save( const fs::path& path, const vector<char>& bytes, const IDirEntry& dirEntry )ε->up<IDirEntry>{
		IO::SaveBinary<char>( path, std::span<char>((char*)bytes.data(), bytes.size()) );
		applyTimes( path, dirEntry );
		return mu<DirEntry>( path );
	}

	α NativeDrive::Load( const IDirEntry& dirEntry )ε->vector<char>{//fs::filesystem_error, IOException
		return IO::LoadBinary( dirEntry.Path );
	}

	α NativeDrive::Remove( const fs::path& path )ε->void{
		DBG( "Removing '{}'."sv, path.string() );
		fs::remove( path );
	}
	α NativeDrive::Trash( const fs::path& path )ι->void{
		DBG( "Trashing '{}'."sv, path.string() );
		let pathString = path.string();
		const char* argv[] = { "gio", "trash", "--", pathString.c_str(), nullptr };//no shell - path is a plain argv element.
		pid_t pid; int status;
		if( let err = ::posix_spawnp( &pid, "gio", nullptr, nullptr, const_cast<char**>(argv), environ ); err ){
			WARN( "Trashing '{}' - posix_spawnp failed: {}"sv, pathString, strerror(err) );
		}
		else if( ::waitpid( pid, &status, 0 )==-1 ){
			WARN( "Trashing '{}' - waitpid failed: {}"sv, pathString, strerror(errno) );
		}
		else
			DBG( "Trashing '{}' returned '{}'."sv, pathString, WIFEXITED(status) ? WEXITSTATUS(status) : status );
	}
	α NativeDrive::SoftLink( const fs::path& existingFile, const fs::path& newSymLink )ε->void{
		let result = ::symlink( existingFile.string().c_str(), newSymLink.string().c_str() );
		THROW_IF( result!=0, "symlink creating '{}' referencing '{}' failed ({}){}.", newSymLink.string(), existingFile.string(), result, errno );
		DBG( "Created symlink '{}' referencing '{}'."sv, newSymLink.string(), existingFile.string() );
	}
}