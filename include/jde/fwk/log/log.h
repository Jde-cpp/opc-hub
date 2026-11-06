#pragma once
#ifdef __cpp_lib_stacktrace
	#include <stacktrace>
#else
	#include <boost/stacktrace.hpp>
#endif
#include "../process/process.h"
#include "break.h"
#include "ILogger.h"
#include "Entry.h"
#ifdef _MSC_VER
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
#endif

#define FormatString const Jde::Logging::Format<Args const&...>
#define ARGS const Args&
#define let const auto
#define Φ Γ auto

#define LOGSL(level, sl, tags, message,...) \
	if( Logging::ShouldLog(level, (ELogTags)tags) && !Process::Finalizing() ){\
 		Logging::Emit( level, (ELogTags)tags, sl, message __VA_OPT__(,) __VA_ARGS__ );\
	}
#define LOG(level, tags, message,...) LOGSL( level, SRCE_CUR, tags, message __VA_OPT__(,) __VA_ARGS__ )
#define CRITICAL(message,...) CRITICALT( _tags, message __VA_OPT__(,) __VA_ARGS__ )
#define CRITICALT(tags, message,...) LOG( Jde::ELogLevel::Critical, tags, message __VA_OPT__(,) __VA_ARGS__ )
#define ERR( message, ... ) ERRT( _tags, message __VA_OPT__(,) __VA_ARGS__ )
#define ERRT(tags, message,...) LOG( ELogLevel::Error, tags, message __VA_OPT__(,) __VA_ARGS__ )
#define WARN(message,...) WARNT( _tags, message __VA_OPT__(,) __VA_ARGS__ )
#define WARNT(tags, message,...) LOG( ELogLevel::Warning, tags, message __VA_OPT__(,) __VA_ARGS__ )
#define INFO(message,...) INFOT( _tags, message __VA_OPT__(,) __VA_ARGS__ )
#define INFOT(tags, message,...) LOG( ELogLevel::Information, tags, message __VA_OPT__(,) __VA_ARGS__ )
#define DBG(message,...) DBGT( _tags, message __VA_OPT__(,) __VA_ARGS__ )
#define DBGT(tags, message,...) LOG( ELogLevel::Debug, tags, message __VA_OPT__(,) __VA_ARGS__ )
#define DBGSL(message,...) LOGSL( ELogLevel::Debug, sl, _tags, message __VA_OPT__(,) __VA_ARGS__ )
#define RETURN_IF( predicate, level, message, ... ) if( predicate ){ LOG(level, _tags, message __VA_OPT__(,) __VA_ARGS__); return; }
#define TRACE(message,...) TRACET( _tags, message __VA_OPT__(,) __VA_ARGS__ )
#define TRACET(tags, message,...) LOG( ELogLevel::Trace, tags, message __VA_OPT__(,) __VA_ARGS__ )
#define TRACESL(message,...) LOGSL( ELogLevel::Trace, sl, _tags, message __VA_OPT__(,) __VA_ARGS__ )

namespace Jde{
	struct Exception;
	Φ ToString( ELogLevel l )ι->string;
	Φ ToLogLevel( sv op )ι->ELogLevel;
	α LogLevelStrings()ι->const std::array<sv,7>;
}

namespace Jde::Logging{
	//An Entry already built:  an exception's, a stack frame's, or, with its origin on the record, one forwarded from another
	//instance, which never breaks into the debugger.  Gated here.
	Φ Log( const Record& record )ι->void;

	//A line, ungated:  the macros check ShouldLog and Finalizing before evaluating their arguments, and LogStack and LogOnce
	//check them too, so this doesn't again.  The fan-out to the loggers is out of line, with the arguments' types erased.
	Φ Emit( ELogLevel level, ELogTags tags, SL sl, const FormatView& m, fmt::format_args args )ι->void;
	ψ Emit( ELogLevel level, ELogTags tags, SL sl, FormatString&& m, ARGS... args )ι->void{ Emit( level, tags, sl, m.View(), fmt::make_format_args(args...) ); }
	//A line from code that doesn't use the macros:  gated once, then emitted.
	ψ Log( ELogLevel level, ELogTags tags, SL sl, FormatString&& m, ARGS... args )ι->void{
		if( ShouldLog(level, tags) && !Process::Finalizing() )
			Emit( level, tags, sl, FWD(m), FWD(args)... );
	}
	//Whether LogStack walks the stack for a line's file, line and function:  /logging/stack, a per-tag level object read at
	//Init, { default: "Warning" } without it.  Below it the line has no source, on the console and in the files.
	Φ ShouldCaptureStack( ELogLevel level, ELogTags tags )ι->bool;
#ifdef __cpp_lib_stacktrace
	ψ LogStack( ELogLevel level, ELogTags tags, std::stacktrace::size_type stackTraceIndex, FormatString&& m, ARGS... args )ι->void{
		if( !ShouldLog(level, tags) || Process::Finalizing() )
			return;

		if( !ShouldCaptureStack(level, tags) )
			Emit( level, tags, SL{}, FWD(m), FWD(args)... );
		else if( let stacktrace = std::stacktrace::current( stackTraceIndex+1, 1 ); stacktrace.size() )
			Log( Entry{stacktrace[0], level, tags, FWD(m), FWD(args)...} );
		else
			Emit( level, tags, SRCE_CUR, FWD(m), FWD(args)... );
	}
	#else
	ψ LogStack( ELogLevel level, ELogTags tags, boost::stacktrace::stacktrace::size_type stackTraceIndex, FormatString&& m, ARGS... args )ι->void{
		if( !ShouldLog(level, tags) || Process::Finalizing() )
			return;
		if( !ShouldCaptureStack(level, tags) )
			Emit( level, tags, SL{}, FWD(m), FWD(args)... );
		else if( let stacktrace = boost::stacktrace::stacktrace( stackTraceIndex+1, 1 ); stacktrace.size() )//see the std branch above
			Log( Entry{stacktrace[0], level, tags, FWD(m), FWD(args)...} );
		else
			Emit( level, tags, SRCE_CUR, FWD(m), FWD(args)... );
	}
	#endif

	Φ MarkLogged( StringMd5 id )ι->bool;
	//Gated before the id is claimed, so a line below every logger's level is still unclaimed when it would first be seen.
	template<typename... Args>
	α LogOnce( SL sl, ELogLevel level, ELogTags tags, FormatString&& m, ARGS... args )ι->void{
		if( ShouldLog(level, tags) && !Process::Finalizing() && MarkLogged(m.Id) )
			Emit( level, tags, sl, FWD(m), FWD(args)... );
	}
}

#undef FormatString
#undef ARGS
#undef let
#undef Φ