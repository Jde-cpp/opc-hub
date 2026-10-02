#pragma once
#include <stdexcept>
#include <jde/fwk/utils/paramPack.h>
#include "jde/fwk/log/logTags.h"
#include <jde/fwk/io/crc.h>

namespace Jde{
	static constexpr ELogLevel DefaultExceptionLevel{ ELogLevel::Debug };

	#define THROW(x, ...) throw Jde::Exception{ SRCE_CUR, {}, x __VA_OPT__(,) __VA_ARGS__ }
	#define THROWSL(x, ...) throw Jde::Exception{ sl, {}, x __VA_OPT__(,) __VA_ARGS__ }
	#define THROW_IF(condition, x, ...) if( condition ) THROW( x __VA_OPT__(,) __VA_ARGS__  )
	#define THROW_IFSL(condition, x, ...) if( condition ) throw Jde::Exception{ sl, {}, x __VA_OPT__(,) __VA_ARGS__ }
	#define THROW_IFX(condition, x) if( condition ) throw x
	#define CHECK(condition) if( !(condition) ) throw Jde::Exception( #condition, {Jde::ELogLevel::Error} )
	enum EHttpStatus : uint16{
		None = 0,
		Found = 302,
		BadRequest = 400,
		Unauthorized = 401,
		Forbidden = 403,
		NotFound = 404,
		Conflict = 409,
		IAmATeapot = 418,
		InternalServerError = 500,
		BadGateway = 502,
		ServiceUnavailable = 503,
		GatewayTimeout = 504
	};
	class ExceptionArgs{
	public:
		ExceptionArgs( ELogLevel level=DefaultExceptionLevel, ELogTags tags=ELogTags::Exception, uint32 code=UninitializedCode, EHttpStatus status=EHttpStatus::None )ι: Tags{tags}, _level{level}, _statusCode{status}, _code{code}{}
		ExceptionArgs( ELogTags tags, uint32 code=0, EHttpStatus status=EHttpStatus::None )ι:ExceptionArgs{DefaultExceptionLevel, tags, code, status}{}
		ExceptionArgs( uint32 code )ι:ExceptionArgs{DefaultExceptionLevel, ELogTags::Exception, code}{}
		ExceptionArgs( EHttpStatus status )ι:ExceptionArgs{DefaultExceptionLevel, ELogTags::Exception, UninitializedCode, status}{}

		α HasCode()Ι->bool{ return _code!=UninitializedCode; }
		α Level()Ι->ELogLevel{return _level;} α SetLevel( ELogLevel level )Ι{ _level=level;}

		ELogTags Tags;
	protected:
		mutable ELogLevel _level;
	private:
		static constexpr uint32 UninitializedCode{ (uint32)std::numeric_limits<int32_t>::max() };
	public:
		mutable uint32 _code;
		EHttpStatus _statusCode{ EHttpStatus::None };
	};
	#define $ template<class... Args>
	struct Γ Exception : std::runtime_error, ExceptionArgs{
		using base=std::runtime_error;

		Exception( Exception&& e )ι;
		Exception( const Exception& e )ι;
		Exception( runtime_error&& e, ExceptionArgs args={}, SRCE )ι;
		Ω FromPtr( const std::exception_ptr& e, SRCE )ι->up<Exception>; //preserves dynamic type, unlike a ctor which would slice.

		Exception( string value, ExceptionArgs args={}, SRCE )ι;
		$ Exception( SL sl, ExceptionArgs args, std::exception&& inner, fmt::format_string<Args...> m="", Args&&... sargs )ι;
		$ Exception( SL sl, ExceptionArgs args, fmt::format_string<Args...> m, Args&&... sargs )ι;

		virtual ~Exception();

		β Log()Ι->void;
		//the part of this exception a client may see, empty unless a subclass opts in - what() carries internals. Response bodies append it, so a new funnel gets it without repeating the policy.
		β ClientDetail()Ι->string{ return {}; }
		enum class ECategory : uint8{ Jde, DB };//values mirror Common.proto ECategory - static_assert in app/shared/proto/common.cpp.
		//stored, not just virtual: a wire exception loses its concrete type, so the receiver's SetHttpStatus is all that keeps a 401 a 401.
		β HttpStatus()Ι->EHttpStatus{ return _statusCode ? _statusCode : EHttpStatus::InternalServerError; }
		α SetHttpStatus( EHttpStatus status )ι->void{ _statusCode = status; }
		β Category()Ι->ECategory{ return ECategory::Jde; }
		β CategoryCode()Ι->uint32{ return 0; }
		β what()const noexcept->const char* override;
		α What()Ι->const string&{ what(); return _what; }
		β UserMessage()Ι->const string&{ return What(); }
		α PrependWhat( const string& prepend )ι->void{ What()/*initialize*/; _what = prepend+_what; }
		α Source()Ι->SL{ return _sl; }
		β Move()ι->up<Exception>{ return mu<Exception>(move(*this)); }
		[[noreturn]] β Throw()->void{ throw move(*this); }

		α SetTags( ELogTags tags )ι{ Tags = tags | ELogTags::Exception; }
		Ω EmptyPtr()ι->const up<Exception>&;
	protected:
		Exception( SRCE )ι:base{""},_sl{ sl }{}
		α Format()Ι->sv{ return visit( []( auto&& arg )->sv{return {arg.data(),arg.size()};}, _format ); }
		α BreakLog()Ι->void;

		mutable string _what;
		mutable bool _logged{};//log once: at construction when Level()>=BreakLevel, otherwise at destruction/explicit Log().
		up<exception> _inner;
		variant<sv,string> _format;
		vector<string> _args;
	private:
		α operator=( Exception&& from )ι->Exception&;
	public:
		SL _sl;
		α Code()Ι->uint32{
			if( !HasCode() )
				_code = IO::Crc::Calc32c( Format() );
			return _code;
		}
	};

	Ŧ ToExceptionPtr( T&& e )ι->up<T>{
		auto p = dynamic_cast<Exception*>( &e );
		return p ? p->Move() : up<T>{ mu<T>(e.what()) };
	}
	//The T=std::exception instantiation of the above does not compile outside the MSVC STL:  `exception(const char*)` is a
	//Microsoft extension, and libc++/libstdc++ give std::exception a default ctor only.  A non-Jde inner is only ever read
	//through what(), and runtime_error carries the message on every standard library - which is what the Exception ctor below
	//needs, since it takes its inner as std::exception&& to accept a logic_error (std::stoi/stod).
	Ξ ToExceptionPtr( std::exception&& e )ι->up<std::exception>{//std:: - unqualified `exception` only resolves inside the class above, through std::runtime_error's base.
		auto p = dynamic_cast<Exception*>( &e );
		return p ? up<std::exception>{ p->Move() } : up<std::exception>{ mu<runtime_error>(e.what()) };
	}

	$ Exception::Exception( SL sl, ExceptionArgs args, exception&& inner, fmt::format_string<Args...> m, Args&&... sargs )ι:
		runtime_error{ "" },//not '{}': that picks runtime_error(const char*) with a null pointer - libc++ strlen's it.
		ExceptionArgs{ args },
		_inner{ ToExceptionPtr(move(inner)) }, //preserves derived message/type; mu<runtime_error> would slice.
		_format{ sv{m.get().data(), m.get().size()} },
		_sl{ sl }{
		_args.reserve( sizeof...(sargs) );
		ParamPack::Append( _args, sargs... );
		BreakLog();
	}

	$ Exception::Exception( SL sl, ExceptionArgs args, fmt::format_string<Args...> m, Args&&... sargs )ι:
		runtime_error{ "" },
		ExceptionArgs{ args },
		_format{ sv{m.get().data(), m.get().size()} },
		_sl{ sl }{
		_args.reserve( sizeof...(sargs) );
		ParamPack::Append( _args, sargs... );
		BreakLog();
	}

	[[noreturn]] Ξ Throw( runtime_error&& e )ε->void{
		if( auto p = dynamic_cast<Exception*>( &e ); p )
			p->Throw();
		else
			throw move(e);
	}
}
#undef $