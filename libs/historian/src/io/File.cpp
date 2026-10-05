#include "File.h"
#include <jde/fwk/io/file.h>
#include <absl/container/flat_hash_set.h>
#include <jde/fwk/exceptions/IOException.h>
#ifdef _WIN32
	#include <windows.h>
#else
	#include <cstdio>
	#include <fcntl.h>
	#include <sys/file.h>
	#include <unistd.h>
#endif

#define let const auto

namespace Jde::Opc{
	namespace{
#ifdef _WIN32
		Ξ closed()ι->void*{ return INVALID_HANDLE_VALUE; }
		Ξ lastError()ι->uint32{ return ::GetLastError(); }
#else
		Ξ closed()ι->int{ return -1; }
		Ξ lastError()ι->uint32{ return (uint32)errno; }
#endif
		//With the OS's code for the call that just failed.
		Ω failed( const fs::path& path, sv call, SL sl )ι->IO::IOException{
			let code = lastError();
			return Hist::Failed( path, code, string{call}, sl );
		}
	}
	α Hist::Failed( const fs::path& path, uint32 code, string call, SL sl )ι->IO::IOException{
		IO::IOException e{ path, code, move(call), sl };
		e.SetLevel( ELogLevel::Error );
		return e;
	}
	α Hist::Failed( const fs::path& path, const std::error_code& ec, SL sl )ι->IO::IOException{
		return Hist::Failed( path, (uint32)ec.value(), ec.message(), sl );
	}

#ifdef _WIN32
	α Hist::MakeDirectories( const fs::path& directory, SL sl )ε->void{
		IO::CreateDirectories( directory, sl );
	}
	α Hist::SyncDirectories( const fs::path&, const fs::path&, SL )ε->void{}
	α Hist::Replace( const fs::path& from, const fs::path& to, SL sl )ε->void{
		if( !::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) )
			throw failed( to, "MoveFileEx", sl );
	}
namespace Hist{
	α PathLock::TryLock( const fs::path& path, SL sl )ε->optional<PathLock>{
		IO::CreateDirectories( path, sl );
		let file = path/"historian.lock";
		//Shared, so a second holder gets as far as the lock, whose refusal can only mean that it is held.
		let handle = ::CreateFileW( file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
		if( handle==INVALID_HANDLE_VALUE )
			throw failed( file, "CreateFile", sl );
		OVERLAPPED at{};
		if( !::LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at) ){
			let code = lastError();
			::CloseHandle( handle );
			if( code==ERROR_LOCK_VIOLATION || code==ERROR_IO_PENDING )
				return nullopt;
			throw Failed( file, code, "LockFileEx", sl );
		}
		return PathLock{ handle };
	}
	PathLock::~PathLock(){
		if( _handle!=closed() )
			::CloseHandle( _handle );//which drops the lock.
	}
}
#else
	α Hist::MakeDirectories( const fs::path&, SL )ε->void{}

	Ω syncDirectory( const fs::path& dir, SL sl )ε->void{
		let fd = ::open( dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC );
		if( fd==-1 )
			throw failed( dir, "open", sl );
		let code = ::fsync( fd )==-1 ? lastError() : 0;
		::close( fd );
		if( code && code!=EINVAL )//EINVAL is a file system with no directory fsync, which has nothing to lose.
			throw Hist::Failed( dir, code, "fsync", sl );
	}
	//relative's own directory each time, for the names it gains.  Above it, each directory's name once per process:  the
	//groups share their days' directories, and a name, once durable, stays so.
	α Hist::SyncDirectories( const fs::path& root, const fs::path& relative, SL sl )ε->void{
		static absl::Mutex mutex;
		static absl::flat_hash_set<string> named ABSL_GUARDED_BY( mutex );
		syncDirectory( root/relative, sl );
		for( auto dir = relative; !dir.empty(); dir = dir.parent_path() ){
			let path = ( root/dir ).string();
			{
				ul _{ mutex };
				if( named.contains(path) )
					break;//and those above it, synced with it.
			}
			syncDirectory( root/dir.parent_path(), sl );
			ul _{ mutex };
			named.insert( path );
		}
	}

	α Hist::Replace( const fs::path& from, const fs::path& to, SL sl )ε->void{
		if( ::rename(from.c_str(), to.c_str())==-1 )
			throw failed( to, "rename", sl );
	}
}

namespace Jde::Opc::Hist{
	PathLock::PathLock( PathLock&& x )ι:
		_handle{ std::exchange(x._handle, closed()) }
	{}

	α PathLock::TryLock( const fs::path& path, SL sl )ε->optional<PathLock>{
		IO::CreateDirectories( path, sl );
		let file = path/"historian.lock";
		let fd = ::open( file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666 );
		if( fd==-1 )
			throw failed( file, "open", sl );
		//Held by the open file, not the process, so a second open of the path is refused in this process as in another.
		if( ::flock(fd, LOCK_EX | LOCK_NB)==-1 ){
			let code = lastError();
			::close( fd );
			if( code==EWOULDBLOCK )
				return nullopt;
			throw Failed( file, code, "flock", sl );
		}
		return PathLock{ fd };
	}
	PathLock::~PathLock(){
		if( _handle!=closed() )
			::close( _handle );//which drops the lock.
	}
#endif
}
