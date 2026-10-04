#pragma once

namespace Jde::Opc::Hist{
	//A file the historian writes, through the OS's own handle, which is what an fsync takes.  Every failure throws an
	//IOException at Error with the OS's code:  history that stops being written, which an operator must see.
	struct File final{
		//Opens path for writing, making it when it isn't there.  Others may read it meanwhile, and rename over it.
		Ω Open( fs::path path, SRCE )ε->File;
		File( File&& x )ι;
		~File();
		α Created()Ι->bool{ return _created; }//by this Open, so its directory holds a name to fsync.
		α Size( SRCE )Ε->uint;
		α Write( uint offset, sv bytes, SRCE )ε->void;
		α Resize( uint size, SRCE )ε->void;
		//The file's bytes, and the size that reaches them, to the device:  fdatasync on Linux, FlushFileBuffers on Windows.
		α Sync( SRCE )ε->void;
	private:
#ifdef _WIN32
		using Handle = void*;
#else
		using Handle = int;
#endif
		File( fs::path path, Handle handle, bool created )ι:_path{ move(path) }, _handle{ handle }, _created{ created }{}
		fs::path _path;
		Handle _handle;
		bool _created;
	};

	//fsyncs root/relative and each directory above it through root, so a name created under root survives a power loss
	//along with its data, whoever made the directories and whenever.  Nothing on Windows, which has no such call:  there
	//a rename itself writes through.
	α SyncDirectories( const fs::path& root, const fs::path& relative={}, SRCE )ε->void;

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
