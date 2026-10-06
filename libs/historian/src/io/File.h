#pragma once
#include <jde/fwk/exceptions/IOException.h>

namespace Jde::Opc::Hist{
	//At Error, where IOException's constructors that take an OS code take no level:  history that stops being read or
	//written, which an operator must see.  call names what failed.
	α Failed( const fs::path& path, uint32 code, string call, SRCE )ι->IO::IOException;
	α Failed( const fs::path& path, const std::error_code& ec, SRCE )ι->IO::IOException;

	//What the historian asks of the OS that a write through IO::WriteAwait doesn't do for it.

	//A day file's directories, on Windows, where a write makes none.  Nothing on Linux:  there the file's first write
	//makes them, and fsyncs each one it makes.
	α MakeDirectories( const fs::path& directory, SRCE )ε->void;

	//fsyncs root/relative and each directory above it through root, so a name an earlier process made under root, and
	//may have crashed before it fsynced, survives a power loss along with its data.  Nothing on Windows, which has no
	//such call.
	α SyncDirectories( const fs::path& root, const fs::path& relative={}, SRCE )ε->void;

	//Renames from over to, which is then what from was whatever stops the process:  MoveFileExW with MOVEFILE_WRITE_THROUGH
	//on Windows, and rename on Linux, where the name is durable once SyncDirectories has fsynced to's directory.
	α Replace( const fs::path& from, const fs::path& to, SRCE )ε->void;

	//A day file open to read at any offset, which the merges of its day share.  It reads the file it opened whatever is
	//renamed over the path after:  Windows opens it sharing delete, so the rename isn't refused for it.
	struct ReadHandle final : noncopyable{
		ReadHandle( fs::path path, SRCE )ε;//throws when path can't be opened.
		~ReadHandle();
		//Up to size bytes at offset:  fewer at the end of the file and none past it.  None when the read fails.
		α Read( uint offset, void* buffer, uint size )Ι->optional<uint>;
		const fs::path Path;
	private:
#ifdef _WIN32
		using Handle = void*;
#else
		using Handle = int;
#endif
		Handle _handle;
	};

	//The exclusive OS lock a host takes on <hist.path>/historian.lock, flock on Linux and LockFileEx on Windows, so two
	//processes never append to the same files.  The OS drops it when its process dies, so a crash leaves none stale.
	struct PathLock final{
		//None when another holds it, in this process or any other.  Makes path when it isn't there, and throws when it or
		//the lock file can't be made.
		Ω TryLock( const fs::path& path, SRCE )ε->optional<PathLock>;
		PathLock( PathLock&& x )ι;
		~PathLock();
	private:
#ifdef _WIN32
		using Handle = void*;
#else
		using Handle = int;
#endif
		PathLock( Handle handle )ι:_handle{ handle }{}
		Handle _handle;
	};
}
