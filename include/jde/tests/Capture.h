#pragma once
#ifndef JDE_TESTS_CAPTURE_H
#define JDE_TESTS_CAPTURE_H
#include <deque>

//What a test asserts was logged, for test executables only.  One logger, made by Tests::Main, keeps the lines that the suite's
//levels (/logging/memory/tags) or a live Capture's take, each entry shared and rendered once, and drops what no reader can
//still ask for.
namespace Jde::Logging::Internal{
	struct CaptureLog final : ILogger{
		Ω Instance()ι->CaptureLog&{
			static CaptureLog* instance = []{
				up<CaptureLog> log{ new CaptureLog };
				auto y = log.get();
				AddLogger( move(log) );
				UpdateCumulative( Loggers() );//AddLogger leaves the filter as it was.
				return y;
			}();
			return *instance;
		}
		α Write( const Record& r )ι->void override{
			auto entry = ms<Entry>( r.Entry() );
			entry->OwnNames();
			entry->FillCaches();//before it is shared:  Find reads it unlocked, and rendering it may log.
			ul _{ _mutex };
			_entries.push_back( move(entry) );
		}
		α Shutdown( bool /*terminate*/, SL )ι->void override{
			ul _{ _mutex };
			_front += _entries.size();
			_entries.clear();
		}
		//A Capture's levels in, and its start:  the next line's sequence.
		α Open( TagLevels levels )ι->std::pair<uint,uint>{
			uint id, from;
			{
				ul _{ _mutex };
				id = ++_nextId;
				from = next();
				_marks.emplace( id, from );
			}
			ul _{ _applyMutex };
			_captures.emplace( id, move(levels) );
			apply();
			return { id, from };
		}
		α Close( uint id )ι->void{
			{
				ul _{ _applyMutex };
				_captures.erase( id );
				apply();
			}
			ul _{ _mutex };
			_marks.erase( id );
			trim();
		}
		α Restart( uint id )ι->uint{
			ul _{ _mutex };
			auto& from = _marks[id];
			from = next();
			trim();
			return from;
		}
		α RestartSuite()ι->void{
			ul _{ _mutex };
			_suiteFrom = next();
			trim();
		}
		α SuiteFrom()ι->uint{ rl _{ _mutex }; return _suiteFrom; }
		//The pointers under the lock, the scan without it:  f may log.
		α Find( uint from, const function<bool(const Entry&)>& f )ι->vector<Entry>{
			vector<sp<const Entry>> kept;
			{
				rl _{ _mutex };
				const auto skip = std::min( from>_front ? from-_front : 0, _entries.size() );
				kept.assign( _entries.begin()+skip, _entries.end() );
			}
			vector<Entry> y;
			for( const auto& entry : kept ){
				if( f(*entry) )
					y.push_back( *entry );
			}
			return y;
		}
	private:
		CaptureLog()ι:ILogger{ "Capture", jobject{} }{
			ul _{ _applyMutex };
			_suite = TagLevels{ ELogLevel::NoLog };
			if( const auto tags = Settings::FindObject("/logging/memory/tags"); tags ){
				auto suite = TagLevels::Parse( *tags );
				if( suite.Default && *suite.Default!=ELogLevel::NoLog )
					_suite = move( suite );
			}
			{
				ul _{ _mutex };
				_suiteActive = *_suite.Default!=ELogLevel::NoLog;
			}
			apply();
		}
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_applyMutex) α apply()ι->void{
			auto levels = _suite;
			for( const auto& [_, capture] : _captures )
				levels += capture;//each line at the lowest level the suite or a capture gives it.
			Edit( [&]( TagLevels& overrides ){ overrides = levels; } );
		}
		ABSL_SHARED_LOCKS_REQUIRED(_mutex) α next()Ι->uint{ return _front+_entries.size(); }
		//Every entry before the oldest start still read.  The suite's start reads only when it has levels.
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_mutex) α trim()ι->void{
			auto keep = next();
			if( _suiteActive )
				keep = std::min( keep, _suiteFrom );
			for( const auto& [_, from] : _marks )
				keep = std::min( keep, from );
			for( ; _front<keep; ++_front )
				_entries.pop_front();
		}
		absl::Mutex _applyMutex;//held through an Edit, which logs, so never under _mutex.
		TagLevels _suite ABSL_GUARDED_BY(_applyMutex);
		flat_map<uint,TagLevels> _captures ABSL_GUARDED_BY(_applyMutex);
		absl::Mutex _mutex;
		std::deque<sp<const Entry>> _entries ABSL_GUARDED_BY(_mutex);
		uint _front ABSL_GUARDED_BY(_mutex){};//the sequence of _entries.front().
		flat_map<uint,uint> _marks ABSL_GUARDED_BY(_mutex);//each Capture's start.
		uint _nextId ABSL_GUARDED_BY(_mutex){};
		uint _suiteFrom ABSL_GUARDED_BY(_mutex){};
		bool _suiteActive ABSL_GUARDED_BY(_mutex){};
	};
}
namespace Jde::Logging{
	//The lines logged from construction on, at the levels named on top of the suite's.  The levels are in the process's filter
	//while it lives and out of it after, so a test opens only what it asserts on.  Captures nest, and each reads them all.
	struct Capture final{
		explicit Capture( ELogLevel level )ι:Capture{ TagLevels{level} }{}//every line at level and above.
		Capture( ELogTags tags, ELogLevel level )ι:Capture{ TagLevels{{}, {{tags, level}}} }{}//lines carrying all of tags.
		Capture( const Capture& )=delete;
		α operator=( const Capture& )->Capture& =delete;
		~Capture(){ Internal::CaptureLog::Instance().Close( _id ); }

		α Find( const function<bool(const Entry&)>& f )Ι->vector<Entry>{ return Internal::CaptureLog::Instance().Find( _from, f ); }
		α Find( StringMd5 templateId )Ι->vector<Entry>{ return Find( [templateId](const auto& e){ return e.Id()==templateId; } ); }
		α Find( sv message )Ι->vector<Entry>{ return Find( [message](const auto& e){ return e.Message()==message; } ); }//the whole message.
		α Restart()ι->void{ _from = Internal::CaptureLog::Instance().Restart( _id ); }//from now on.
	private:
		explicit Capture( TagLevels levels )ι{ std::tie( _id, _from ) = Internal::CaptureLog::Instance().Open( move(levels) ); }
		uint _id;
		uint _from;
	};

	//The suite's capture:  its levels, read since the last ClearMemory.
	Ξ StartSuiteCapture()ι->void{ Internal::CaptureLog::Instance(); }//Tests::Main, once startup has read the settings.
	Ξ ClearMemory()ι->void{ Internal::CaptureLog::Instance().RestartSuite(); }
	Ξ Find( const function<bool(const Entry&)>& f )ι->vector<Entry>{
		auto& log = Internal::CaptureLog::Instance();
		return log.Find( log.SuiteFrom(), f );
	}
	Ξ Find( StringMd5 templateId )ι->vector<Entry>{ return Find( [templateId](const auto& e){ return e.Id()==templateId; } ); }
	Ξ Find( sv message )ι->vector<Entry>{ return Find( [message](const auto& e){ return e.Message()==message; } ); }//the whole message.
	//Whether a line would be kept now:  the suite's levels with every live Capture's.
	Ξ Captures( ELogLevel level, ELogTags tags )ι->bool{ return Internal::CaptureLog::Instance().ShouldLog( level, tags ); }
}
#endif