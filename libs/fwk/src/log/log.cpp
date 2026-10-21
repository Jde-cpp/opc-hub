#include <jde/fwk/log/log.h>
#include <mutex>
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
#include "AbseilSink.h"

#define let const auto

namespace Jde{
	//Walked by every Log(), from any thread, and changed rarely - startup's Add<>s, a test's MemoryLog mid-run, Init's memory
	//logger, shutdown.  A reader takes the current list with one acquire load and walks it unlocked;  a writer never changes a
	//list, but publishes a changed copy under _loggersMutex, and keeps each list and logger it replaces until DestroyLoggers, as
	//a reader may still be walking them.  A push_back here used to move the list under a walk on another thread:  "vector
	//iterators incompatible" in a debug build (HistCollectorTests), a read of freed memory in a release one.
	using LoggerList = vector<Logging::ILogger*>;
	std::mutex _loggersMutex;
	vector<up<Logging::ILogger>> _owned;
	vector<up<const LoggerList>> _lists;//every list published, the current one last.
	Ω publish( LoggerList&& list )ι->const LoggerList*{
		_lists.push_back( mu<const LoggerList>(move(list)) );
		return _lists.back().get();
	}
	Ω initLoggers()ι->const LoggerList*{
		auto memory = mu<Logging::MemoryLog>();
		LoggerList list{ memory.get() };
		_owned.push_back( move(memory) );
		Logging::UpdateCumulative( list );
		return publish( move(list) );
	}
	std::atomic<const LoggerList*> _current{ initLoggers() };
	Ω install( LoggerList&& list )ι->void{ _current.store( publish(move(list)), std::memory_order_release ); }//under _loggersMutex.
	α Logging::Loggers()->const LoggerList&{
		static const LoggerList none;//a line logged before this file's statics are initialized.
		let p = _current.load( std::memory_order_acquire );
		return p ? *p : none;
	}
	α Logging::AddLogger( up<ILogger>&& logger )ι->ILogger*{
		ASSERT( logger );
		std::lock_guard _{ _loggersMutex };
		auto list = Loggers();
		list.push_back( logger.get() );
		_owned.push_back( move(logger) );
		install( move(list) );
		return _owned.back().get();
	}
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
		RemoveAbseilSink();//first: its thread logs into the loggers shut down below.
		for( ;; ){//one at a time, out of the list before its Shutdown:  a line logged meanwhile still reaches the rest.
			ILogger* logger;
			{
				std::lock_guard _{ _loggersMutex };
				auto list = Loggers();
				if( list.empty() )
					break;
				logger = list.front();
				list.erase( list.begin() );
				install( move(list) );
			}
			logger->Shutdown( terminate );
		}
		std::lock_guard _{ _loggersMutex };
		_owned.clear();
	};

	α Logging::Init()ι->void{
		//Once per process: a second call adds a second SpdLog and drops whatever is first in the list.  A process hosting
		//two apps (OpcHub) has two InitLogging entry points; its main calls one, this makes a slip harmless.
		static bool initialized{};
		if( std::exchange(initialized, true) )
			return;
#ifndef NDEBUG
		SetBreakLevel( Settings::FindEnum<ELogLevel>("/logging/breakLevel", ToLogLevel).value_or(ELogLevel::Warning) );
#endif
		Logging::Add<SpdLog>( "spd" );
		auto& memoryLogger = Logging::GetLogger<MemoryLog>();
		for( let logger : Loggers() ){
			if( dynamic_cast<MemoryLog*>(logger) )
				continue;
			memoryLogger.Write( *logger );
		}
		auto memory = Settings::FindObject( "/logging/memory/tags" );
		if( !memory || Json::FindEnum<ELogLevel>(*memory, "default", ToLogLevel).value_or(ELogLevel::NoLog)==ELogLevel::NoLog ){
			std::lock_guard _{ _loggersMutex };
			auto list = Loggers();
			list.erase( list.begin() );//the memory logger, first since initLoggers.
			install( move(list) );
		}
		else
		 	Loggers().front()->SetLevels( *memory, true );//settings, not overrides: what a cleared override on the memory logger falls back to.
		Logging::UpdateCumulative( Loggers() );
		AddAbseilSink();
	}
}