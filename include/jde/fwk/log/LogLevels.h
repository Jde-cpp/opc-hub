#pragma once
#include "logTags.h"

namespace Jde{
	#define Φ Γ auto
	//A tag->level map and its default:  a logger's settings, its overrides, or the two together, and the parts += combined in.
	//A plain value.
	struct Γ TagLevels{
		Ω Parse( const jobject& tagLevels )ι->TagLevels;//the config file's flat map, "default" included;  a level that is not a string is skipped.
		α DefaultLevel()Ι->ELogLevel;//a line's level when nothing names its tags:  the default, or a part's lower one.
		//The configured level for tags, in this map:  its own entry, else, for a combined set, the most specific entry the line
		//carries all of, a tie taking the lower level.  nullopt:  the default applies.
		α Find( ELogTags tags )Ι->optional<ELogLevel>;
		α Resolve( ELogTags tags )Ι->ELogLevel;//this map's level for tags, or a part's lower one.
		α Floor()Ι->ELogLevel;//no tag set resolves below it.
		α Over( const TagLevels& base )Ι->TagLevels;//these on top of base.
		α operator+=( const TagLevels& x )ι->TagLevels&;//combining loggers:  each line takes the lower level, and NoLog yields to any.
		α operator==( const TagLevels& x )Ι->bool=default;

		optional<ELogLevel> Default;
		flat_map<ELogTags,ELogLevel> Levels;
		//What += combined in, each kept whole:  merged into Levels, an entry for a set would shadow a lower one for a tag in it
		//(reviews-issue/MemoryLog.md #8).
		vector<TagLevels> Parts;
	};
namespace Logging{
	struct ILogger;
	//The levels lines are tested against, fixed once built:  its memo of resolved tag sets can never go stale, and a reader
	//needs no lock.  A change builds a new one;  the replaced one is kept, as a reader may still be in it.
	struct Γ Gate{
		Gate( sv name, TagLevels levels )ι;
		α ShouldLog( ELogLevel level, ELogTags tags )Ι->bool;
		α MinLevel( ELogTags tags )Ι->ELogLevel;
		α Floor()Ι->ELogLevel{ return _floor; }//no tag set resolves below it, so a line under it is refused without a lookup.
		α Levels()Ι->const TagLevels&{ return _levels; }
		α ToString()Ι->string;
	private:
		sv _name;//whose levels, for MinLevel's trace.
		TagLevels _levels;
		ELogLevel _floor;
		mutable concurrent_flat_map<ELogTags,ELogLevel> _memo;
	};
	//Rebuilds the cumulative Gate, the loggers' levels together, which ShouldLog asks of every line;  LogLevels' edits call it.
	Φ UpdateCumulative( const vector<ILogger*>& loggers )ι->void;
}
	//A logger's levels:  the settings, the runtime overrides on top of them, and the Gate of the two that its lines are tested against.
	struct Γ LogLevels{
		LogLevels( sv name, const jobject& settings )ι;//settings' "tags".
		α Name()Ι->sv{ return _name; }
		α Levels()Ι->const Logging::Gate&{ return *_gate.load( std::memory_order_acquire ); }
		α ShouldLog( ELogLevel level, ELogTags tags )Ι->bool{ return Levels().ShouldLog( level, tags ); }
		α MinLevel( ELogTags tags )Ι->ELogLevel{ return Levels().MinLevel( tags ); }
		α Effective()Ι->const TagLevels&{ return Levels().Levels(); }//the levels in force:  the settings with the overrides on top.
		α DefaultLevel()Ι->ELogLevel{ return Effective().DefaultLevel(); }
		α Overrides()Ι->TagLevels;
		α ToString()Ι->string{ return Levels().ToString(); }

		//Every change publishes this logger's Gate and then the cumulative, once, and only when the levels in force moved.
		α Edit( const function<void(TagLevels& overrides)>& edit )ι->void{ Change( &LogLevels::_overrides, edit ); }
		α SetSettings( const jobject& tagLevels )ι->void;//merged into the settings:  what a cleared override falls back to.
		α SetLevels( const jobject& tagLevels )ι->void;//merged into the overrides:  the instance_tag_levels rows at start (IApp::LoadLogSettings).
		α SetLevel( ELogTags tags, ELogLevel level )ι->void{ Edit( [=]( TagLevels& o ){ o.Levels.insert_or_assign( tags, level ); } ); }
		//Drops the override, the runtime twin of deleting the instance_tag_levels row, so the tag goes back to the level the
		//settings gave it, or to the default when they gave none (install-issues #21).
		α ClearLevel( ELogTags tags )ι->void{ Edit( [=]( TagLevels& o ){ o.Levels.erase( tags ); } ); }
		α SetDefaultLevel( ELogLevel level )ι->void{ Edit( [=]( TagLevels& o ){ o.Default = level; } ); }
		α ClearDefaultLevel()ι->void{ Edit( []( TagLevels& o ){ o.Default.reset(); } ); }
	private:
		α Change( TagLevels LogLevels::*layer, const function<void(TagLevels&)>& change )ι->void;
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(_levelsMutex) α Publish()ι->bool;
		sv _name;
		mutable absl::Mutex _levelsMutex;
		TagLevels _settings ABSL_GUARDED_BY(_levelsMutex);
		TagLevels _overrides ABSL_GUARDED_BY(_levelsMutex);
		std::atomic<const Logging::Gate*> _gate;
		vector<up<Logging::Gate>> _published ABSL_GUARDED_BY(_levelsMutex);//every Gate published, the current last.
	};
	Φ ShouldTrace( ELogTags tags )ι->bool;
namespace Logging{
	Φ ShouldLog( ELogLevel level, ELogTags tags )ι->bool;
	//A per-tag level object from the settings, over the defaults given, gating something other than a logger's writing:
	//ProtoLog's /logging/proto/file and /function, LogStack's /logging/stack.  name, static, is what its trace output shows.
	struct Γ Threshold final : Gate{
		Threshold( sv name, const jobject* settings, const jobject& defaults )ι;
	};
}}
#undef Φ