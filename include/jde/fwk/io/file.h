#pragma once
#ifndef FILE_H
#define FILE_H
#include <fstream> //!important
#include <jde/fwk/exceptions/IOException.h>

#define Φ Γ auto
namespace Jde::IO{
	Φ CreateDirectories( const fs::path& path, SRCE )ε->bool;
	Φ Load( const fs::path& path, SRCE )ε->string;
	Φ LoadBinary( const fs::path& path, SRCE )ε->vector<char>;
	Ŧ SaveBinary( const fs::path& path, std::span<T> values, bool append=false, SRCE )ε->void;

#ifdef _WIN32
	α BashToWindows( const fs::path& path )ι->fs::path;
	α InUse( const fs::path& path )ι->bool;//another handle holds the file open - it cannot be renamed.
#endif
}
#undef Φ
namespace Jde{
	Ŧ IO::SaveBinary( const fs::path& path, std::span<T> data, bool append, SL sl )ε->void{
		std::ofstream f( path, append ? std::ios::binary|std::ios::app : std::ios::binary );
		THROW_IFX( f.fail(), IOException(path, "Could not open file", sl) );
		f.write( (char*)data.data(), data.size() );
		f.close();//ofstream buffers: for a payload under the streambuf size fail() is still false right after write() and an ENOSPC would surface only when the destructor closed the stream - silently.  Close first, then check.
		THROW_IFX( f.fail(), IOException(path, "Could not write file", sl) );
	}
}
#endif
