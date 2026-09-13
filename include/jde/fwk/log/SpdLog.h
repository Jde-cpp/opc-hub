#pragma once
#ifndef SPDLOG
#define SPDLOG
#include <jde/fwk/log/ILogger.h>
#include <jde/fwk/log/Entry.h>
#include <spdlog/common.h>
#include <spdlog/logger.h>

#define Φ Γ auto
#define FormatString const fmt::format_string<Args const&...>
#define ARGS const Args&

namespace Jde::Logging{
	Φ FormatSourceUri( sv file )ι->string;
	Ξ ToSpdSL( SL sl )ι->spdlog::source_loc{ return {sl.file_name(), (int)sl.line(), sl.function_name()}; }
	struct SpdLog final : ILogger{
		Γ SpdLog( const jobject& settings )ι;
		ψ Write( ELogLevel level, SL sl, FormatString&& m, ARGS... args )ε{
			_logger.log( ToSpdSL(sl), (spdlog::level::level_enum)level, FWD(m), FWD(args)... );
		}
		α Shutdown( bool /*terminate*/, SL )ι->void override{ _logger.flush(); }
		α Name()Ι->sv override{ return _logger.name(); }
		α FlushLevel()Ι->ELogLevel{ return (ELogLevel)_logger.flush_level(); }
		α SetMinLevel( ELogLevel /*level*/ )ι->void override{}
		α WriteFormatted( ELogLevel level, SL sl, fmt::string_view m, fmt::format_args args )ι->bool override{
			_logger.log( ToSpdSL(sl), (spdlog::level::level_enum)level, fmt::vformat(m, args) );
			return true;
		}
		α Write( const Entry& m )ι->void override{
			_logger.log( m.SourceLocation(), (spdlog::level::level_enum)m.Level, m.Message() );
		}
		α Write( const Entry& m, uint32 /*appPK*/, uint32 /*instancePK*/ )ι->void override{ Write(m); }
	private:
		spdlog::logger _logger;
	};
}
#undef FormatString
#undef ARGS
#undef Φ
#endif