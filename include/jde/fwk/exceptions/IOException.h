#pragma once
#include <system_error>
#include "Exception.h"
#include "jde/fwk/log/logTags.h"

namespace Jde::IO{
	//OS-independent classification of an io failure.  The native code stays in Exception::Code(), where it is an errno on
	//linux and a GetLastError value on windows - callers that branch on what went wrong read Error instead.
	enum class EIOError : uint8{
		None,       //unmapped - the native code is all there is.
		NotFound,   //the file, or a directory on its path.
		Exists,
		Permission,
		Busy,       //held by an open that doesn't share it, or locked.
		NoSpace,    //the device or a quota is full.
		Invalid,    //the request can't be met as asked: a bad offset, a directory where a file is needed.
		Device      //the device failed the io.
	};
	Γ α ToIOError( uint32 nativeCode )ι->EIOError;

#define CHECK_PATH( path, sl ) THROW_IFX( !fs::exists(path), IO::IOException(path, "path does not exist", sl) )
	struct Γ IOException final : Exception{
		IOException( fs::path path, uint32 code, string value, SRCE ):Exception{ move(value), {ELogLevel::Debug, ELogTags::IO, code}, sl }, Error{ ToIOError(code) }, _path{ move(path) }{ SetWhat(); }
		IOException( fs::path path, string value, SRCE ): Exception{ move(value), {ELogLevel::Debug, ELogTags::IO}, sl }, _path{ move(path) }{ SetWhat(); }
		IOException( fs::filesystem_error&& e, SRCE ):Exception{sl}, _underlying( mu<fs::filesystem_error>(move(e)) ){
			if( const auto& code = _underlying->code(); code.category()==std::system_category() )//a generic code is an errno everywhere, which ToIOError would misread on windows.
				Error = ToIOError( (uint32)code.value() );
			SetWhat();
		}
		template<class... Args> IOException( SL sl, const fs::path& path, ELogLevel level, fmt::format_string<Args...> m, Args&&... args ):Exception( sl, {level, ELogTags::IO}, m, std::forward<Args>(args)... ),_path{ path }{ SetWhat(); }

		α Path()Ι->const fs::path&; α SetPath( const fs::path& x )ι{ _path=x; }
		α what()const noexcept->const char* override;
		α Move()ι->up<Exception> override{ return mu<IOException>(move(*this)); }
		[[noreturn]] α Throw()->void override{ throw move(*this); }

		EIOError Error{ EIOError::None };
		bool Written{};//a write put every byte in the file and failed after that, at the sync: the data is there and its durability unknown, so writing it again would duplicate it.
	private:
		α SetWhat()Ι->void;

		up<const fs::filesystem_error> _underlying;
		fs::path _path;
	};

	Ξ IOException::Path()Ι->const fs::path&{
		return  _underlying? _underlying->path1() : _path;
	}
	Ξ IOException::SetWhat()Ι->void{
		if( _underlying )
			_what = _underlying->what();
		else if( HasCode() ) //Code() is lazy & would mark HasCode - only touch it inside this branch.
			_what = Ƒ( "({}) {} - {} path='{}'", Code(), std::system_category().message((int)Code()), Exception::what(), Path().string() );
		else
			_what = Ƒ( "{} path='{}'", Exception::what(), Path().string() );
	}
	Ξ IOException::what()Ι->const char*{
		return _what.c_str();
	}
}