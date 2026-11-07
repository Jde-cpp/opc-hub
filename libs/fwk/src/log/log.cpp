#include <jde/fwk/log/log.h>
#include <jde/fwk/log/SpdLog.h>	//no longer reachable through <jde/fwk.h>
#include "AbseilSink.h"

#define let const auto

namespace Jde{
	//Every line logged before Init, when no levels are known to refuse one by, and the whole list until then.  A logger added
	//before Init is given the lines so far as it is added.  Init's own get them all when it closes the buffer, and from then on
	//it forwards them a line from a writer still walking a pre-Init list, so each of theirs arrives once, replayed or direct.
	struct StartupBuffer final : Logging::ILogger{
		StartupBuffer()ι:ILogger{ "Startup", jobject{{"tags",jobject{{"default", "Trace"}}}} }{}
		α Write( const Logging::Record& r )ι->void override;
		α Shutdown( bool /*terminate*/, SL )ι->void override{ ul _{ _mutex }; _entries = {}; }
		α Replay( Logging::ILogger& logger )ι->void;
		α Close( vector<Logging::ILogger*> loggers )ι->void;
	private:
		Ω replay( const vector<Logging::Entry>& entries, Logging::ILogger& logger )ι->void;
		absl::Mutex _mutex;
		vector<Logging::Entry> _entries ABSL_GUARDED_BY(_mutex);
		bool _closed ABSL_GUARDED_BY(_mutex){};
		vector<Logging::ILogger*> _forward;//Init's loggers, set once under _mutex by the close and only read after:  unannotated.
	};
	α StartupBuffer::Write( const Logging::Record& r )ι->void{
		let& entry = r.Entry();
		{
			ul _{ _mutex };
			if( !_closed ){
				_entries.push_back( entry );
				return;
			}
		}
		for( let logger : _forward ){
			if( logger->ShouldLog(r.Level, r.Tags) )
				logger->Write( r );
		}
	}
	α StartupBuffer::replay( const vector<Logging::Entry>& entries, Logging::ILogger& logger )ι->void{
		for( let& entry : entries ){
			if( logger.ShouldLog(entry.Level, entry.Tags) )
				logger.Write( entry );
		}
	}
	α StartupBuffer::Replay( Logging::ILogger& logger )ι->void{
		vector<Logging::Entry> entries;
		{
			ul _{ _mutex };
			entries = _entries;//a copy:  writing them may log more.
		}
		replay( entries, logger );
	}
	α StartupBuffer::Close( vector<Logging::ILogger*> loggers )ι->void{
		vector<Logging::Entry> entries;
		{
			ul _{ _mutex };
			entries = std::exchange( _entries, {} );
			_forward = move( loggers );
			_closed = true;
		}
		for( let logger : _forward )
			replay( entries, *logger );
	}

	//Walked by every Log(), from any thread, and changed rarely - startup's Add<>s, Init, a test's capture, shutdown.
	//A reader takes the current list with one acquire load and walks it unlocked;  a writer never changes a list, but publishes
	//a changed copy under _loggersMutex, and keeps each list and logger it replaces until DestroyLoggers, as a reader may still
	//be walking them.  A push_back here used to move the list under a walk on another thread:  "vector iterators incompatible"
	//in a debug build (HistCollectorTests), a read of freed memory in a release one.
	using LoggerList = vector<Logging::ILogger*>;
	ABSL_CONST_INIT absl::Mutex _loggersMutex{ absl::kConstInit };
	vector<up<Logging::ILogger>> _owned ABSL_GUARDED_BY(_loggersMutex);
	vector<up<const LoggerList>> _lists ABSL_GUARDED_BY(_loggersMutex);//every list published, the current one last.
	StartupBuffer* _startup ABSL_GUARDED_BY(_loggersMutex){};//until Init closes it.
	ABSL_EXCLUSIVE_LOCKS_REQUIRED(_loggersMutex) Ω publish( LoggerList&& list )ι->const LoggerList*{
		_lists.push_back( mu<const LoggerList>(move(list)) );
		return _lists.back().get();
	}
	Ω initLoggers()ι->const LoggerList*{
		ul _{ _loggersMutex };
		auto startup = mu<StartupBuffer>();
		_startup = startup.get();
		LoggerList list{ _startup };
		_owned.push_back( move(startup) );
		Logging::UpdateCumulative( list );
		return publish( move(list) );
	}
	std::atomic<const LoggerList*> _current{ initLoggers() };
	ABSL_EXCLUSIVE_LOCKS_REQUIRED(_loggersMutex) Ω install( LoggerList&& list )ι->void{ _current.store( publish(move(list)), std::memory_order_release ); }
	α Logging::Loggers()->const LoggerList&{
		static const LoggerList none;//a line logged before this file's statics are initialized.
		let p = _current.load( std::memory_order_acquire );
		return p ? *p : none;
	}
	α Logging::AddLogger( up<ILogger>&& logger )ι->ILogger*{
		ASSERT( logger );
		ul _{ _loggersMutex };
		if( _startup )
			_startup->Replay( *logger );
		auto list = Loggers();
		list.push_back( logger.get() );
		_owned.push_back( move(logger) );
		install( move(list) );
		return _owned.back().get();
	}
	α Logging::LogAddFailure( sv configName, const runtime_error& e )ι->void{
		ERRT( ELogTags::Settings, "The '{}' logger is configured but could not be created, so it is not running: {}", configName, e.what() );
	}
	//Published once by Init, read by every LogStack:  the defaults until then.
	std::atomic<const Logging::Threshold*> _stack{};
	α Logging::ShouldCaptureStack( ELogLevel level, ELogTags tags )ι->bool{
		let stack = _stack.load( std::memory_order_acquire );
		return stack ? stack->ShouldLog( level, tags ) : level>=ELogLevel::Warning;
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
				ul _{ _loggersMutex };
				auto list = Loggers();
				if( list.empty() )
					break;
				logger = list.front();
				list.erase( list.begin() );
				install( move(list) );
			}
			logger->Shutdown( terminate );
		}
		ul _{ _loggersMutex };
		_owned.clear();
	};

	α Logging::Init()ι->void{
		//Once per process: the startup buffer closes once.  A process hosting two apps (OpcHub) has two InitLogging entry
		//points; its main calls one, this makes a slip harmless.
		static bool initialized{};
		if( std::exchange(initialized, true) )
			return;
#ifndef NDEBUG
		SetBreakLevel( Settings::FindEnum<ELogLevel>("/logging/breakLevel", ToLogLevel).value_or(ELogLevel::Warning) );
#endif
		vector<up<ILogger>> made;
		if( let settings = Settings::FindObject("/logging/spd"); settings )
			made.push_back( mu<SpdLog>(*settings) );
		static const Threshold stack{ "stack", Settings::FindObject("/logging/stack"), jobject{{"default", "Warning"}} };
		_stack.store( &stack, std::memory_order_release );
		vector<ILogger*> loggers;
		StartupBuffer* startup;
		{
			ul _{ _loggersMutex };
			auto list = Loggers();
			std::erase( list, _startup );
			for( auto& logger : made ){
				loggers.push_back( logger.get() );
				list.push_back( logger.get() );
				_owned.push_back( move(logger) );
			}
			install( move(list) );
			startup = std::exchange( _startup, nullptr );
		}
		startup->Close( move(loggers) );
		Logging::UpdateCumulative( Loggers() );
		AddAbseilSink();
	}
}