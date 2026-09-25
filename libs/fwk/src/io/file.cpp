#include "jde/fwk/exceptions/IOException.h"
#include <filesystem>
#include <jde/fwk/io/file.h>
#include <jde/fwk/str.h>
#include <fstream>
#ifdef _WIN32
	#include <windows.h>
#endif

#define let const auto
namespace Jde{
	constexpr ELogTags _tags{ ELogTags::IO };
	Ω fileSize( const fs::path& path )ε->uint{
		try{
			return fs::file_size( fs::canonical(path) );
		}
		catch( fs::filesystem_error& e ){
			throw IO::IOException( move(e) );
		}
	}
	α IO::CreateDirectories( const fs::path& path, SL sl )ε->bool{
		try{
			return fs::create_directories( path ); //false=already exists
		}
		catch( fs::filesystem_error& e ){
			throw IO::IOException{ move(e), sl };
		}
	}
	//Load and LoadBinary differ only in the container they fill - one body, T = string or vector<char>.
	Ṫ load( const fs::path& path, SL sl )ε->T{
		CHECK_PATH( path, sl );
		let size = fileSize( path );
		TRACESL( "Opening {} - {} bytes ", path.string(), size );
		std::ifstream f( path, std::ios::binary ); THROW_IFX( f.fail(), IO::IOException(path, "Could not open file", sl) );
		T y( size, '\0' );
		f.read( y.data(), (std::streamsize)size );
		THROW_IFX( (uint)f.gcount()!=size, IO::IOException(path, Ƒ("Read {} of {} bytes.", f.gcount(), size), sl) );
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