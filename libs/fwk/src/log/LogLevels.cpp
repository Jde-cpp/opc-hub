#include <jde/fwk/log/LogLevels.h>
#include <bit>
#include <jde/fwk/log/ILogger.h>
#include <jde/fwk/str.h>
#define let const auto

namespace Jde{
	namespace Logging{
		Ω min( ELogLevel a, ELogLevel b )ι->ELogLevel{
			using enum ELogLevel;
			return a==NoLog || b==NoLog ? std::max( a, b ) : std::min( a, b );
		}
	}

	//The loggers' levels together, which ShouldLog asks of every line, from any thread, and UpdateCumulative replaces on a level
	//change.  Read with one acquire load;  a replaced one is kept, not freed, as a reader may still be in it - one per level
	//change, as log.cpp keeps its logger lists.  An up<> here freed it under the readers.  A recompute that changes nothing - an
	//instanceTagLevels re-applied on reconnect - publishes nothing, so only a real change costs a copy (opcserver-refactor-review #5).
	std::atomic<const Logging::Gate*> _cumulative;
	std::mutex _cumulativeMutex;
	vector<up<Logging::Gate>> _cumulatives;//every one published, the current last.
	Ω cumulative()ι->const Logging::Gate*{ return _cumulative.load( std::memory_order_acquire ); }

	//Built from the loggers' published Gates under the lock, so of two edits on different loggers the later recompute sees both.
	α Logging::UpdateCumulative( const vector<ILogger*>& loggers )ι->void{
		const Gate* published;
		{
			std::lock_guard _{ _cumulativeMutex };
			if( loggers.empty() && cumulative() )
				return;//the list torn down at shutdown:  what logs meanwhile keeps the levels it had.
			optional<TagLevels> combined;
			for( let logger : loggers ){
				if( combined )
					*combined += logger->Effective();
				else
					combined = logger->Effective();
			}
			auto levels = combined.value_or( TagLevels{ELogLevel::Information} );
			if( !cumulative() || cumulative()->Levels()!=levels ){
				_cumulatives.push_back( mu<Gate>("Cumulative", move(levels)) );
				_cumulative.store( _cumulatives.back().get(), std::memory_order_release );
			}
			published = cumulative();
		}
		if( published->MinLevel(ELogTags::Settings)==ELogLevel::Trace )
			Logging::Log( ELogLevel::Trace, ELogTags::Settings, SRCE_CUR, "Cumulative: {}", published->ToString() );
	}

	α Logging::ShouldLog( ELogLevel level, ELogTags tags )ι->bool{
		return cumulative()->ShouldLog( level, tags );
	}

	α TagLevels::Parse( const jobject& o )ι->TagLevels{
		TagLevels y;
		for( let& [name, level] : o ){
			if( name=="default" ){
				if( level.is_string() )
					y.Default = ToLogLevel( level.get_string() );
			}
			else if( let tags = ToLogTags( (sv)name ); !empty(tags) && level.is_string() )
				y.Levels.insert_or_assign( tags, ToLogLevel(level.get_string()) );
		}
		return y;
	}

	//Every bit, not just the framework's that FwkTags names:  a library's tag (EOpcLogTags) counts toward a match too.
	Ω bits( ELogTags tags )ι->uint{ return (uint)std::popcount( std::to_underlying(tags) ); }
	α TagLevels::Find( ELogTags tags )Ι->optional<ELogLevel>{
		if( let exact = Levels.find(tags); exact!=Levels.end() )
			return exact->second;
		optional<ELogLevel> level;
		if( bits(tags)>1 ){
			uint matched{};
			//An entry applies only when the line carries every tag in it (reviews-issue/logTags.md #8):  sharing any one let
			//http.client.read put opc's read lines at Trace.  Every entry, since a tie takes the lower level, as combining loggers
			//does, not whichever comes first.  An empty entry would be part of every line, and names nothing.
			for( let& [configured, configuredLevel] : Levels ){
				if( empty(configured) || (configured & tags)!=configured )
					continue;
				if( let n = bits(configured); n>matched ){
					level = configuredLevel;
					matched = n;
				}
				else if( n==matched )
					level = Logging::min( *level, configuredLevel );
			}
		}
		return level;
	}
	α TagLevels::Over( const TagLevels& base )Ι->TagLevels{
		auto y = base;
		if( Default )
			y.Default = Default;
		for( let& [tags, level] : Levels )
			y.Levels.insert_or_assign( tags, level );
		return y;
	}
	α TagLevels::operator+=( const TagLevels& x )ι->TagLevels&{
		if( x.Default )
			Default = Default ? Logging::min( *Default, *x.Default ) : *x.Default;
		for( let& [tags, level] : x.Levels ){
			if( auto existing = Levels.find(tags); existing!=Levels.end() )
				existing->second = Logging::min( existing->second, level );
			else
				Levels.emplace( tags, level );
		}
		return *this;
	}

	Logging::Gate::Gate( sv name, TagLevels levels )ι:
		_name{ name },
		_levels{ move(levels) },
		_floor{ _levels.DefaultLevel() }{
		for( let& [_, level] : _levels.Levels )
			_floor = Logging::min( _floor, level );//NoLog yields:  a gate with nothing but NoLog has no floor to test.
	}
	α Logging::Gate::ShouldLog( ELogLevel level, ELogTags tags )Ι->bool{
		if( level==ELogLevel::NoLog || level<_floor )
			return false;
		let configuredMin = MinLevel( tags );
		return configuredMin!=ELogLevel::NoLog && configuredMin<=level;
	}
	α Logging::Gate::MinLevel( ELogTags tags )Ι->ELogLevel{
		optional<ELogLevel> level;
		if( _memo.cvisit(tags, [&](let& kv){ level = kv.second; }) )
			return *level;
		let configured = _levels.Find( tags );
		level = configured.value_or( _levels.DefaultLevel() );
		_memo.emplace( tags, *level );
		//Settings at Trace, asked of the cumulative unless this is it, resolving Settings - which would only ask itself again.
		let all = cumulative();
		let settings = all==this && tags==ELogTags::Settings ? *level : all ? all->MinLevel( ELogTags::Settings ) : ELogLevel::NoLog;
		if( settings==ELogLevel::Trace ){
			let levelString = configured ? Jde::ToString( *configured ) : Ƒ( "unset - {}", Jde::ToString(*level) );
			Logging::Log( ELogLevel::Trace, ELogTags::Settings, SRCE_CUR, "[{}]tag: {}, minLevel: {}", _name, ToName(tags), levelString );
		}
		return *level;
	}
	α Logging::Gate::ToString()Ι->string{
		string y = Ƒ( "default: {}\n", Jde::ToString(_levels.DefaultLevel()) ); y.reserve( 1024 );
		flat_map<ELogLevel, vector<string>> configured, memoized;//one line per level, its tags joined:  the configured, then the memoized.
		for( let& [tags, level] : _levels.Levels )
			configured[level].push_back( ToName(tags) );
		_memo.cvisit_all( [&](let& kv){ memoized[kv.second].push_back( ToName(kv.first) ); } );
		for( let levels : {&configured, &memoized} ){
			for( let& [level, names] : *levels )
				y += Ƒ( "{}: {}\n", FromEnum(LogLevelStrings(), level), Str::Join(names, ",") );
		}
		y.pop_back();
		return y;
	}

	Ω thresholdLevels( const jobject* settings, const jobject& defaults )ι->TagLevels{
		let y = TagLevels::Parse( defaults );
		return settings ? TagLevels::Parse( *settings ).Over( y ) : y;
	}
	Logging::Threshold::Threshold( sv name, const jobject* settings, const jobject& defaults )ι:
		Gate{ name, thresholdLevels(settings, defaults) }
	{}

	LogLevels::LogLevels( sv name, const jobject& settings )ι:
		_name{ name }{
		ul _{ _levelsMutex };
		_settings = TagLevels::Parse( Json::FindDefaultObject(settings, "tags") );
		Publish();
	}
	α LogLevels::Overrides()Ι->TagLevels{
		rl _{ _levelsMutex };
		return _overrides;
	}
	α LogLevels::SetSettings( const jobject& tagLevels )ι->void{
		Change( &LogLevels::_settings, [&]( TagLevels& s ){ s = TagLevels::Parse( tagLevels ).Over( s ); } );
	}
	α LogLevels::SetLevels( const jobject& tagLevels )ι->void{
		Edit( [&]( TagLevels& o ){ o = TagLevels::Parse( tagLevels ).Over( o ); } );
	}
	//The cumulative is rebuilt outside _levelsMutex:  it reads every logger's Gate, this one's included.
	α LogLevels::Change( TagLevels LogLevels::*layer, const function<void(TagLevels&)>& change )ι->void{
		bool published;
		{
			ul _{ _levelsMutex };
			change( this->*layer );
			published = Publish();
		}
		if( published )
			Logging::UpdateCumulative( Logging::Loggers() );
	}
	α LogLevels::Publish()ι->bool{
		auto effective = _overrides.Over( _settings );
		if( let current = _gate.load(std::memory_order_relaxed); current && current->Levels()==effective )
			return false;
		_published.push_back( mu<Logging::Gate>(_name, move(effective)) );
		_gate.store( _published.back().get(), std::memory_order_release );
		return true;
	}
}

α Jde::ShouldTrace( ELogTags tags )ι->bool {
	return cumulative()->MinLevel( tags ) == ELogLevel::Trace;
}