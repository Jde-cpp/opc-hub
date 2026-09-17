#include <jde/fwk/log/log.h>
#include <boost/lexical_cast.hpp>
#ifdef _MSC_VER
	#include <crtdbg.h>
	#include <spdlog/spdlog.h>
	#include <spdlog/sinks/msvc_sink.h>
#endif
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include "jde/fwk/log/logTags.h"
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/log/SpdLog.h>	//no longer reachable through <jde/fwk.h>

#define let const auto

namespace Jde{
	α initLoggers()ι->vector<up<Logging::ILogger>>{
		vector<up<Logging::ILogger>> y;
		y.reserve( 8 );
		y.push_back( mu<Logging::MemoryLog>() );
		Logging::UpdateCumulative( y );
		return y;
	}
	vector<up<Logging::ILogger>> _loggers = initLoggers();
	α Logging::Loggers()->const vector<up<ILogger>>&{ return _loggers; }
	α Logging::AddLogger( up<ILogger>&& logger )ι->ILogger*{ ASSERT(logger); _loggers.push_back(move(logger)); return _loggers.back().get(); }
	α Logging::LogAddFailure( sv configName, const runtime_error& e )ι->void{
		ERRT( ELogTags::Settings, "The '{}' logger is configured but could not be created, so it is not running: {}", configName, e.what() );
	}
	inline constexpr std::array<sv,7> ELogLevelStrings = { "Trace", "Debug", "Information", "Warning", "Error", "Critical", "None" };
}

α Jde::LogLevelStrings()ι->const std::array<sv,7>{ return ELogLevelStrings; }

α Jde::ToString( ELogLevel l )ι->string{
	return l==ELogLevel::NoLog ? "None" : FromEnum( ELogLevelStrings, l );
}
α Jde::ToLogLevel( sv l )ι->ELogLevel{
	let level = ToEnum<ELogLevel>( ELogLevelStrings, l );
	ASSERT( level.has_value() );
	return level.value_or( ELogLevel::Error );
}

namespace Jde{
	α Logging::DestroyLoggers( bool terminate )->void{
		for( auto p=_loggers.begin(); p!=_loggers.end(); ){
			auto logger = move( *p );
			p = _loggers.erase( p );
			logger->Shutdown( terminate );
		}
	};

	α Logging::Init()ι->void{
		//Once per process: a second call adds a second SpdLog and pops whatever sits at _loggers.front().  A process hosting
		//two apps (OpcHub) has two InitLogging entry points; its main calls one, this makes a slip harmless.
		static bool initialized{};
		if( std::exchange(initialized, true) )
			return;
#ifndef NDEBUG
		SetBreakLevel( Settings::FindEnum<ELogLevel>("/logging/breakLevel", ToLogLevel).value_or(ELogLevel::Warning) );
#endif
		Logging::Add<SpdLog>( "spd" );
		auto& memoryLogger = Logging::GetLogger<MemoryLog>();
		for( let& logger : _loggers ){
			if( dynamic_cast<MemoryLog*>(logger.get()) )
				continue;
			memoryLogger.Write( *logger.get() );
		}
		auto memory = Settings::FindObject( "/logging/memory/tags" );
		if( !memory || Json::FindEnum<ELogLevel>(*memory, "default", ToLogLevel).value_or(ELogLevel::NoLog)==ELogLevel::NoLog )
			_loggers.erase( _loggers.begin() );
		else
		 	_loggers.front()->SetLevels( *memory, true );//settings, not overrides: what a cleared override on the memory logger falls back to.
		Logging::UpdateCumulative( _loggers );
	}
}