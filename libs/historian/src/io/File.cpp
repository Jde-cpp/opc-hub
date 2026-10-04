#include "File.h"
#include <jde/fwk/exceptions/IOException.h>
#ifdef _WIN32
	#include <windows.h>
#else
	#include <fcntl.h>
	#include <sys/file.h>
	#include <unistd.h>
#endif

#define let const auto

namespace Jde::Opc::Hist{
	namespace{
#ifdef _WIN32
		Ξ closed()ι->void*{ return INVALID_HANDLE_VALUE; }
		Ξ lastError()ι->uint32{ return ::GetLastError(); }
#else
		Ξ closed()ι->int{ return -1; }
		Ξ lastError()ι->uint32{ return (uint32)errno; }
#endif
		Ω failed( const fs::path& path, sv call, SL sl, uint32 code )ι->IO::IOException{
			IO::IOException e{ path, code, string{call}, sl };
			e.SetLevel( ELogLevel::Error );
			return e;
		}
		//With the OS's code for the call that just failed.
		Ω failed( const fs::path& path, sv call, SL sl )ι->IO::IOException{
			let code = lastError();
			return failed( path, call, sl, code );
		}
	}

	PathLock::PathLock( PathLock&& x )ι:
		_handle{ std::exchange(x._handle, closed()) }
	{}

#ifdef _WIN32
	α MakeDirectories( const fs::path& directory, SL sl )ε->void{
		std::error_code ec;
		fs::create_directories( directory, ec );
		if( ec )
			throw failed( directory, "create_directories", sl, (uint32)ec.value() );
	}
	α SyncDirectories( const fs::path&, const fs::path&, SL )ε->void{}

	α PathLock::TryLock( const fs::path& path, SL sl )ε->optional<PathLock>{
		std::error_code ec;
		fs::create_directories( path, ec );
		if( ec )
			throw failed( path, "create_directories", sl, (uint32)ec.value() );
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
			throw failed( file, "LockFileEx", sl, code );
		}
		return PathLock{ handle };
	}
	PathLock::~PathLock(){
		if( _handle!=closed() )
			::CloseHandle( _handle );//which drops the lock.
	}
#else
	α MakeDirectories( const fs::path&, SL )ε->void{}

	Ω syncDirectory( const fs::path& dir, SL sl )ε->void{
		let fd = ::open( dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC );
		if( fd==-1 )
			throw failed( dir, "open", sl );
		let code = ::fsync( fd )==-1 ? lastError() : 0;
		::close( fd );
		if( code && code!=EINVAL )//EINVAL is a file system with no directory fsync, which has nothing to lose.
			throw failed( dir, "fsync", sl, code );
	}
	α SyncDirectories( const fs::path& root, const fs::path& relative, SL sl )ε->void{
		for( auto dir = relative; ; dir = dir.parent_path() ){
			syncDirectory( root/dir, sl );
			if( dir.empty() )
				break;
		}
	}

	α PathLock::TryLock( const fs::path& path, SL sl )ε->optional<PathLock>{
		std::error_code ec;
		fs::create_directories( path, ec );
		if( ec )
			throw failed( path, "create_directories", sl, (uint32)ec.value() );
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
			throw failed( file, "flock", sl, code );
		}
		return PathLock{ fd };
	}
	PathLock::~PathLock(){
		if( _handle!=closed() )
			::close( _handle );//which drops the lock.
	}
#endif
}
