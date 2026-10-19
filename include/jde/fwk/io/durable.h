#pragma once
#include <jde/fwk/exceptions/IOException.h>

#define Φ Γ auto
//What a durable file layout asks of the OS that a write through IO::WriteAwait doesn't do for it.
namespace Jde::IO{
	//At Error, where IOException's constructors that take an OS code take no level:  a file that stops being read or
	//written, which an operator must see.  call names what failed.
	Φ Failed( const fs::path& path, uint32 code, string call, SRCE )ι->IOException;
	Φ Failed( const fs::path& path, const std::error_code& ec, SRCE )ι->IOException;

	//A file's directories, on Windows, where a write makes none.  Nothing on Linux:  there the file's first write makes
	//them, and fsyncs each one it makes.
	Φ MakeDirectories( const fs::path& directory, SRCE )ε->void;
	//fsyncs root/relative and each directory above it through root, so a name an earlier process made under root, and
	//may have crashed before it fsynced, survives a power loss along with its data.  Nothing on Windows, which has no
	//such call.
	Φ SyncDirectories( const fs::path& root, const fs::path& relative={}, SRCE )ε->void;
	//Renames from over to, which is then what from was whatever stops the process:  a POSIX-semantics rename on Windows, so a
	//handle open on to goes on reading the file it opened, as with rename on Linux, where the name is durable once
	//SyncDirectories has fsynced to's directory.  Where Windows has no POSIX rename, MoveFileExW with MOVEFILE_WRITE_THROUGH,
	//which fails while anyone has to open.
	Φ Replace( const fs::path& from, const fs::path& to, SRCE )ε->void;
	//Removes a temp file a rewrite left, but nothing else at its path:  a directory or a link there isn't one, and stays,
	//for the next open to fail on and say.  Whether it removed one.
	Φ RemoveTemp( const fs::path& temp )ι->bool;

	//The exclusive OS lock a process takes on <dir>/<lockFileName>, flock on Linux and LockFileEx on Windows, so two
	//processes never append to the same files.  The OS drops it when its process dies, so a crash leaves none stale.
	struct Γ PathLock final{
		//None when another holds it, in this process or any other.  Makes dir when it isn't there, and throws when it or
		//the lock file can't be made.
		Ω TryLock( const fs::path& dir, sv lockFileName, SRCE )ε->optional<PathLock>;
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
#undef Φ