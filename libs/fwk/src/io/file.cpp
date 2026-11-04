#include <jde/fwk/io/file.h>
#ifdef _WIN32
	#include <windows.h>
#endif
#include <jde/fwk/str.h>
#include <jde/fwk/io/FileAwait.h>

#define let const auto
namespace Jde{
	constexpr ELogTags _tags{ ELogTags::IO };
	α IO::CreateDirs( const fs::path& path, SL sl )ε->bool{
		try{
			return fs::create_directories( path ); //false=already exists
		}
		catch( fs::filesystem_error& e ){
			throw IO::IOException{ move(e), sl };
		}
	}
	//Load and LoadBinary differ only in the container they fill - one body, T = string or vector<char>.  Sized from the
	//open file, not its path, so a file replaced in between can't be read at the other's length;  one that shrinks
	//after the open comes back short.
	Ṫ load( const fs::path& path, SL sl )ε->T{
		IO::OpenFile f{ path, sl };
		T y( f.Size(sl), '\0' );
		TRACESL( "Loading {} - {} bytes", path.string(), y.size() );
		y.resize( f.Read(0, y.data(), y.size(), sl) );
		return y;
	}
	α IO::Load( const fs::path& path, SL sl )ε->string{ return load<string>( path, sl ); }
	α IO::LoadBinary( const fs::path& path, SL sl )ε->vector<char>{ return load<vector<char>>( path, sl ); }
#ifdef _WIN32
	α IO::BashToWindows( const fs::path& path )ι->fs::path{
		auto str = path.string();
		if( str.length()>3 && str[0]=='/' && str[1]!='/' && str[2]=='/' ){ //if /c/
			str[0] = str[1];
			str[1] = ':';
		}
		return fs::path{ "\\\\?\\"s+Str::Replace(str, '/', '\\') };
	}
	//An open that shares nothing fails on any other handle - spdlog's among them, which shares read and write but not delete.
	α IO::InUse( const fs::path& path )ι->bool{
		HANDLE h = ::CreateFileW( path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
		if( h==INVALID_HANDLE_VALUE )
			return ::GetLastError()==ERROR_SHARING_VIOLATION;
		::CloseHandle( h );
		return false;
	}
#endif
}