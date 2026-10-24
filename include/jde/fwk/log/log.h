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

#define FormatString const fmt::format_string<Args const&...>
#define ARGS const Args&
#define let const auto
#define Φ Γ auto

#define LOGSL(level, sl, tags, message,...) \
	if( Logging::ShouldLog(level, (ELogTags)tags) && !Process::Finalizing() ){\
 		Logging::Log( level, (ELogTags)tags, sl, message __VA_OPT__(,) __VA_ARGS__ );\
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
	Φ Log( const Entry& entry )ι->void;
	Φ Log( const Entry& entry, uint32 appPK, uint32 instancePK )ι->void;

	ψ Log( ELogLevel level, ELogTags tags, SL sl, FormatString&& m, ARGS... args )ι->void;
#ifdef __cpp_lib_stacktrace
	ψ LogStack( ELogLevel level, ELogTags tags, std::stacktrace::size_type stackTraceIndex, FormatString&& m, ARGS... args )ι->void{
		if( !ShouldLog(level, tags) )
			return;

		if( let stacktrace = std::stacktrace::current( stackTraceIndex+1, 1 ); stacktrace.size() )
			Log( Entry{stacktrace[0], level, tags, FWD(m), FWD(args)...} );
		else
			Log( level, tags, SRCE_CUR, FWD(m), FWD(args)... );
	}
	#else
	ψ LogStack( ELogLevel level, ELogTags tags, boost::stacktrace::stacktrace::size_type stackTraceIndex, FormatString&& m, ARGS... args )ι->void{
		if( !ShouldLog(level, tags) )
			return;
		if( let stacktrace = boost::stacktrace::stacktrace( stackTraceIndex+1, 1 ); stacktrace.size() )//see the std branch above
			Log( Entry{stacktrace[0], level, tags, FWD(m), FWD(args)...} );
		else
			Log( level, tags, SRCE_CUR, FWD(m), FWD(args)... );
	}
	#endif

	Φ MarkLogged( StringMd5 id )ι->bool;
	template<typename... Args>
	α LogOnce( SL sl, ELogLevel level, ELogTags tags, FormatString&& m, ARGS... args )ι->void{
		if( MarkLogged(Entry::GenerateId(sv{m.get().data(), m.get().size()})) )
			Log( level, tags, sl, FWD(m), FWD(args)... );
	}
}

namespace Jde{
	ψ Logging::Log( ELogLevel level, ELogTags tags, SL sl, FormatString&& m, ARGS... args )ι->void{
		BREAK_IF( m.get().size()==0 );
		if( Process::Finalizing() || !ShouldLog(level, tags) )
			return;
		for( auto& logger : Loggers() ){
			if( !logger->ShouldLog(level, tags) )
				continue;
			try{
				//SpdLog formats from these args directly; everything else needs an Entry built for it.
				if( !logger->WriteFormatted(level, sl, fmt::string_view{m.get().data(), m.get().size()}, fmt::make_format_args(args...)) )
					logger->Write( Entry{sl, level, tags, string{m.get().data(), m.get().size()}, FWD(args)...} );
			}
			catch( const fmt::format_error& e ){
				Log( ELogLevel::Critical, ELogTags::App, SRCE_CUR, "could not format '{}' cargs: {} error: '{}'", string{m.get().data(), m.get().size()}, sizeof...(args), string{e.what()} );
			}
		}
		BREAK_IF( underlying(tags)<=(1ull << 32) && level>=BreakLevel() );//don't want to break for opc server.
	}
}

#undef FormatString
#undef ARGS
#undef let
#undef Φ