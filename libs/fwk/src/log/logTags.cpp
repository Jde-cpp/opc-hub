#include <jde/fwk/log/logTags.h>
#include <jde/fwk/log/ILogger.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#define let const auto

namespace Jde{
	constexpr std::array<sv,28> ELogTagStrings = { "none",
		"access", "app", "cache", "client", "crypto", "dbDriver", "exception", "externalLogger",
		"http", "io", "locks", "parsing", "pedantic", "ql", "read", "scheduler",
		"server", "sessions", "settings", "shutdown", "socket", "sql", "startup", "subscription",
		"test", "threads", "write"
	};

	namespace Logging{
		Ω min( ELogLevel a, ELogLevel b )ι->ELogLevel{
			using enum ELogLevel;
			return a==NoLog || b==NoLog ? std::max( a, b ) : std::min( a, b );
		}
	}

	up<LogTags> _cumulative;
	vector<up<Logging::ITagParser>> _tagParsers;
	α Logging::AddTagParser( up<ITagParser>&& tagParser )ι->void{ _tagParsers.emplace_back(std::move(tagParser)); }

	α Logging::UpdateCumulative( const vector<up<Logging::ILogger>>& loggers )ι->void{
		up<LogTags> cumulative;
		for( let& logger : loggers ){
			if( !cumulative ){
				cumulative = mu<LogTags>( *logger );
				continue;
			}
			*cumulative+=*logger;
		}
		if( cumulative ){
			_cumulative = move( cumulative );
		}
		else if( !_cumulative )
			_cumulative = mu<LogTags>();
		if( auto l = _cumulative->MinLevel(ELogTags::Settings); l>=ELogLevel::Trace )
			Logging::Log( ELogLevel::Trace, (ELogTags)ELogTags::Settings, SRCE_CUR, "Cumulative: {}", _cumulative->ToString() );
	}

	α Logging::ShouldLog( ELogLevel level, ELogTags tags )ι->bool{
		return _cumulative->ShouldLog( level, tags );
	}
	α Logging::Tags( bool user )ι->flat_map<string,uint>{
		flat_map<string,uint> y;
		for( uint i=1; i<ELogTagStrings.size(); ++i )
			y.emplace( string{ELogTagStrings[i]}, 1ul<<(i-1) );
		for( let& parser : _tagParsers ){
			let additional = parser->Tags();
			y.insert( additional.begin(), additional.end() );
		}
		if( user ){
			//named by ToString, not by hand: "socketClientReadSub" was no more a tag name than ["socket","client","read"]
			//was - ToLogTags knows neither - so the catalogue offered the ui rows it could not then save.  It also has to
			//be the *same* spelling the logSetting/instanceTagLevel answers carry, or the ui shows one tag as two rows.
			constexpr array<ELogTags,11> composites{
				ELogTags::HttpClientRead, ELogTags::HttpClientWrite, ELogTags::HttpClientSessions,
				ELogTags::HttpServerRead, ELogTags::HttpServerWrite,
				ELogTags::SocketClientRead, ELogTags::SocketClientReadSub,
				ELogTags::SocketClientWrite, ELogTags::SocketClientWriteSub,
				ELogTags::SocketServerRead, ELogTags::SocketServerWrite
			};
			for( let tags : composites )
				y.emplace( ToString(tags, false), underlying(tags) );
		}
		return y;
	}
}

#pragma warning( disable:4334 )
α Jde::ToValue( ELogTags tags )ι->jvalue{
	if( tags==ELogTags::None )
		return jvalue( jstring{ELogTagStrings[0]} );//parenthesized: jvalue{x} prefers value's initializer_list constructor, which wraps x in an array.
	jarray tagStrings;
	for( uint i=1; i<ELogTagStrings.size(); ++i ){
		if( (uint)tags & (uint)(1ul<<(i-1ul)) )
			tagStrings.push_back( jstring{ELogTagStrings[i]} );
	}
	for( let& parser : _tagParsers ){
		auto additional = parser->ToString( tags );
		if( additional.size() )
			tagStrings.push_back( jstring{additional} );
	}
	return tagStrings.size()==1
		? tagStrings.front()//the one tag, bare - ELogTagStrings[0] here spelled every single-tag value "none".
		: tagStrings.empty()
			? jvalue{}
			: tagStrings;
}
α Jde::ToString( ELogTags tags, bool outputArray )ι->string{
	auto value = ToValue( tags );
	if( outputArray )
		return serialize( move(value) );
	else{
		let isArray = value.is_array();
		string y;
		if( isArray ){
			for( auto&& item : value.get_array() ){
				if( !y.empty() )
					y += TagSeparator;
				y += move( item.as_string() );
			}
		}
		else if( value.is_string() )//null when the tags are set but none of them nameable; get_string would throw out of a noexcept function.
			y = move(value.get_string());
		return y;
	}
}

α Jde::ToLogTags( sv name )ι->ELogTags{
	auto flags = Str::Split( name, TagSeparator );
	if( name=="default" )
		return ELogTags::None;
	ELogTags y{};
	for( let& subName : flags ){
		ELogTags tag{};
		if( auto i = std::distance(ELogTagStrings.begin(), find(ELogTagStrings, subName)); i>0 && i<(std::ptrdiff_t)ELogTagStrings.size() )
			tag |= ( ELogTags )( 1ul<<(i-1) );
		else{
			for( uint i=0; i<_tagParsers.size() && empty(tag); ++i )
				tag |= _tagParsers[i]->ToTag( string{subName} );
		}
		if( empty(tag) )
			WARNT( ELogTags::Settings, "Unknown tag '{}'", subName );
		y |= tag;
	}
	return y;
}
α Jde::ToLogTags( jvalue v )ι->ELogTags{
	if( let s = v.try_as_string(); s )
		return ToLogTags( (sv)*s );
	else if( let arr = v.try_as_array(); arr ){
		ELogTags y{};
		for( let& item : *arr ){
			if( let s = item.try_as_string(); s ){
				y |= ToLogTags( (sv)*s );
			}
			else
				WARNT( ELogTags::Settings, "Expected string in tags array but got {}", Json::Kind(item.kind()) );
		}
		return y;
	}
	else{
		WARNT( ELogTags::Settings, "Expected string or array for tags but got {}", Json::Kind(v.kind()) );
		return ELogTags::None;
	}
}

α Jde::ToTagName( const jvalue& tags )ι->string{
	string y;
	if( let s = tags.try_as_string(); s )
		y = string{ (sv)*s };
	else if( let parts = tags.try_as_array(); parts ){
		for( let& part : *parts ){
			if( let p = part.try_as_string(); p ){
				if( !y.empty() )
					y += TagSeparator;
				y += (sv)*p;
			}
		}
	}
	return y;
}

//A combined tag can only be spelled as an array, and an array is no object key - so the wire carries tags as *values*,
//both in the instanceTagLevel answer (grouped under the level) and in the updateInstanceTagLevel argument (a record per
//override).  SetLevels and the config file key on the tag, so these flatten.  Lossless: the array joins back with the
//'.' ToLogTags splits on, and "default" passes through untouched.
α Jde::ToTagLevels( const jobject& levelTags )ι->jobject{
	jobject y;
	for( let& [level, tags] : levelTags ){
		let array = tags.try_as_array();
		if( !array )
			continue;
		for( let& tag : *array ){
			if( let name = ToTagName(tag); !name.empty() )
				y[name] = jstring{ level };
		}
	}
	return y;
}
α Jde::ToTagLevels( const jarray& tagLevels )ι->jobject{
	jobject y;
	for( let& entry : tagLevels ){
		let o = entry.try_as_object();
		if( !o )
			continue;
		let tags = o->if_contains( "tags" );
		let name = tags ? ToTagName( *tags ) : string{};
		if( name.empty() )
			continue;
		let level = o->if_contains( "level" );
		y[name] = level ? *level : jvalue{};//no level at all reads as null - the same "remove this override" a null does.
	}
	return y;
}
α Jde::ToTagLevelArray( const jobject& tagLevels )ι->jarray{
	jarray y;
	for( let& [name, level] : tagLevels ){
		jarray tags;
		for( let& part : Str::Split(name, TagSeparator) )
			tags.push_back( jstring{part} );
		jobject entry;
		entry["tags"] = move( tags );
		entry["level"] = level;
		y.push_back( move(entry) );
	}
	return y;
}

namespace Jde{
	Ṫ parseTags( const jobject& o )ι->T{
		T y;
		for( auto&& [tagName, level] : o ){
			if( tagName=="default" )
				y.insert_or_assign( ELogTags::None, ToLogLevel(level.get_string()) );
			else{
				let tag = ToLogTags( (sv)tagName );
				if( !empty(tag) && level.is_string() )
					y.insert_or_assign( tag, ToLogLevel(level.get_string()) );
			}
		}
		return y;
	}

	LogTags::LogTags( const jobject& o )ι:
		_configuredTags{ parseTags<concurrent_flat_map<ELogTags,ELogLevel>>(Json::FindDefaultObject(o, "tags")) },
		ExtrapolatedTags{ _configuredTags },
		_defaultLevel{ ELogLevel::Information }{
		_configuredTags.erase_if( ELogTags::None, [&](let& kv){
			_defaultLevel= kv.second; //parseTags puts default in None.
			return true;
		});
		_minLevel = _defaultLevel;
		_settingsTags = _configuredTags;//nothing has been overridden yet: what is configured is what the settings gave.
		_settingsDefault = _defaultLevel;
	}
	LogTags::LogTags( const LogTags& x )ι:
		_configuredTags{ x._configuredTags },
		_settingsTags{ x._settingsTags },
		ExtrapolatedTags{ _configuredTags },
		_minLevel{ x._minLevel },
		_defaultLevel{ x._defaultLevel },
		_settingsDefault{ x._settingsDefault }
	{}
	α LogTags::operator+=( const LogTags& x )ι->LogTags&{
		_minLevel = Logging::min( _minLevel, x._minLevel );
		_defaultLevel = Logging::min( _defaultLevel, x._defaultLevel );
		x._configuredTags.cvisit_all( [this](let& kv){
			this->_configuredTags.insert_or_visit( kv, [&kv](auto& cumulativeValues){
				cumulativeValues.second = Logging::min( cumulativeValues.second, kv.second );
			} );
		} );
		ExtrapolatedTags = _configuredTags;
		return *this;
	}
	α LogTags::SetLevels( const jobject& tagLevels, bool settings )ι->void{
		auto parsedTags = parseTags<flat_map<ELogTags,ELogLevel>>( tagLevels );
		for( auto&& [tag, level] : parsedTags ){
			if( tag==ELogTags::None ){
				_defaultLevel = level;
				if( settings )
					_settingsDefault = level;
			}
			else{
				_configuredTags.insert_or_assign( tag, level );
				if( settings )
					_settingsTags.insert_or_assign( tag, level );
			}
		}
		ExtrapolatedTags = _configuredTags;
	}

	Ω split( ELogTags tags )ι->vector<ELogTags>{
		vector<ELogTags> result;
		for( uint i=1; i<ELogTagStrings.size(); ++i ){
			let flag = ( ELogTags )( 1ul<<(i-1) );
			if( !empty(tags & flag) )
				result.push_back( flag );
		}
		return result;
	}
	α LogTags::MinLevel( ELogTags tags )Ι->ELogLevel{
		optional<ELogLevel> level;
		if( ExtrapolatedTags.cvisit(tags, [&](let& kv){level = kv.second;}) )
			return *level;
		vector<ELogTags> individual = split( tags );
		if( individual.size()>1 ){
			uint matches{};
			ExtrapolatedTags.cvisit_while( [&level,&matches,tags,count=individual.size()](let& kv){
				if( empty(kv.first & tags) )
					return true;
				if( auto iterCount = split(kv.first & tags).size(); iterCount>matches ){
					level = kv.second;
					matches = iterCount;
				}
				return matches+1<count;
			} );
		}
		let levelString = level ? Jde::ToString( *level ) : Ƒ( "unset - {}", Jde::ToString(_defaultLevel) );
		if( !level )
			level = _defaultLevel;
		ExtrapolatedTags.emplace( tags, *level );
		if( auto l = tags==ELogTags::Settings ? *level : _cumulative->MinLevel(ELogTags::Settings); l>=ELogLevel::Trace ){
			let name = Name();
			let tagString = Jde::ToString( tags );
			Logging::Log( ELogLevel::Trace, ELogTags::Settings, SRCE_CUR, "[{}]tag: {}, minLevel: {}", name, tagString, levelString );
		}
		return *level;
	}

	α LogTags::SetLevel( ELogTags tags, ELogLevel level )ι->void{
		_configuredTags.insert_or_assign( tags, level );
		ExtrapolatedTags = _configuredTags;
		UpdateCumulative( Logging::Loggers() );
	}

	α LogTags::ClearLevel( ELogTags tags )ι->void{
		optional<ELogLevel> settingsLevel;
		_settingsTags.cvisit( tags, [&](let& kv){ settingsLevel = kv.second; } );
		if( settingsLevel )//the override sat on a configured level: put that back (install-issues #21) - erasing lost it until a restart.
			_configuredTags.insert_or_assign( tags, *settingsLevel );
		else
			_configuredTags.erase( tags );
		ExtrapolatedTags = _configuredTags;//MinLevel memoizes into ExtrapolatedTags, so the cleared tag has to be dropped from the cache too, not just the configuration.
		UpdateCumulative( Logging::Loggers() );
	}

	α LogTags::ShouldLog( ELogLevel level, ELogTags tags )Ι->bool{
		if( level==ELogLevel::NoLog )
			return false;
		let configuredMin = MinLevel( tags );
		let result = configuredMin!=ELogLevel::NoLog && configuredMin <= level;
		//ASSERT( !result || tags!=ELogTags::Locks );
		return result;
	}

	α LogTags::ToString()ι->string{
		string y = Ƒ( "default: {}\n", Jde::ToString(_defaultLevel) ); y.reserve( 1024 );
		auto appendLevels = [&y]( const concurrent_flat_map<ELogTags,ELogLevel>& tags ){//one line per level, its tags joined - configured first, then the memoized composites.
			flat_map<ELogLevel, vector<string>> levels;
			tags.cvisit_all( [&](let& kv){
				levels.try_emplace( kv.second, vector<string>{} ).first->second.push_back( Jde::ToString(kv.first) );
			});
			for( let& [level, names] : levels )
				y += Ƒ( "{}: {}\n", FromEnum(LogLevelStrings(), level), Str::Join(names, ",") );
		};
		appendLevels( _configuredTags );
		appendLevels( ExtrapolatedTags );
		if( y.size() )
			y.pop_back();
		return y;
	}
}

α Jde::ShouldTrace( ELogTags tags )ι->bool {
	return _cumulative->MinLevel( tags ) == ELogLevel::Trace;
}