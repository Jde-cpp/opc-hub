#include "File.h"
#include <jde/fwk/exceptions/IOException.h>
#ifdef _WIN32
	#include <windows.h>
#else
	#include <fcntl.h>
	#include <sys/file.h>
	#include <sys/stat.h>
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

	File::File( File&& x )ι:
		_path{ move(x._path) },
		_handle{ std::exchange(x._handle, closed()) },
		_created{ x._created }
	{}
	PathLock::PathLock( PathLock&& x )ι:
		_handle{ std::exchange(x._handle, closed()) }
	{}

#ifdef _WIN32
	α File::Open( fs::path path, SL sl )ε->File{
		let handle = ::CreateFileW( path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
		if( handle==INVALID_HANDLE_VALUE )
			throw failed( path, "CreateFile", sl );
		let created = ::GetLastError()!=ERROR_ALREADY_EXISTS;//what OPEN_ALWAYS leaves for a file that was there.
		return File{ move(path), handle, created };
	}
	File::~File(){
		if( _handle!=closed() )
			::CloseHandle( _handle );
	}
	α File::Size( SL sl )Ε->uint{
		LARGE_INTEGER size;
		if( !::GetFileSizeEx(_handle, &size) )
			throw failed( _path, "GetFileSizeEx", sl );
		return (uint)size.QuadPart;
	}
	α File::Write( uint offset, sv bytes, SL sl )ε->void{
		while( !bytes.empty() ){
			OVERLAPPED at{};
			at.Offset = (DWORD)offset;
			at.OffsetHigh = (DWORD)( offset>>32 );
			DWORD written{};
			if( !::WriteFile(_handle, bytes.data(), (DWORD)std::min<uint>(bytes.size(), 1u<<30), &written, &at) || !written )
				throw failed( _path, "WriteFile", sl );
			offset += written;
			bytes.remove_prefix( written );
		}
	}
	α File::Resize( uint size, SL sl )ε->void{
		LARGE_INTEGER end;
		end.QuadPart = (LONGLONG)size;
		if( !::SetFilePointerEx(_handle, end, nullptr, FILE_BEGIN) || !::SetEndOfFile(_handle) )
			throw failed( _path, "SetEndOfFile", sl );
	}
	α File::Sync( SL sl )ε->void{
		if( !::FlushFileBuffers(_handle) )
			throw failed( _path, "FlushFileBuffers", sl );
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
	α File::Open( fs::path path, SL sl )ε->File{
		auto fd = ::open( path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666 );
		let created = fd!=-1;
		if( !created && errno==EEXIST )
			fd = ::open( path.c_str(), O_WRONLY | O_CLOEXEC );
		if( fd==-1 )
			throw failed( path, "open", sl );
		return File{ move(path), fd, created };
	}
	File::~File(){
		if( _handle!=closed() )
			::close( _handle );
	}
	α File::Size( SL sl )Ε->uint{
		struct stat status;
		if( ::fstat(_handle, &status)==-1 )
			throw failed( _path, "fstat", sl );
		return (uint)status.st_size;
	}
	α File::Write( uint offset, sv bytes, SL sl )ε->void{
		while( !bytes.empty() ){
			let written = ::pwrite( _handle, bytes.data(), bytes.size(), (off_t)offset );
			if( written==-1 && errno==EINTR )
				continue;
			if( written<=0 )
				throw failed( _path, "pwrite", sl, written ? lastError() : (uint32)ENOSPC );//nothing written and no error is a full device.
			offset += (uint)written;
			bytes.remove_prefix( (uint)written );
		}
	}
	α File::Resize( uint size, SL sl )ε->void{
		if( ::ftruncate(_handle, (off_t)size)==-1 )
			throw failed( _path, "ftruncate", sl );
	}
	α File::Sync( SL sl )ε->void{
		while( ::fdatasync(_handle)==-1 ){
			if( errno!=EINTR )
				throw failed( _path, "fdatasync", sl );
		}
	}

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
