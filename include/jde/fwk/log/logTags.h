#pragma once
#ifndef LOG_TAGS_H
#define LOG_TAGS_H

namespace Jde{
	#define Φ Γ auto
	enum class ELogTags : uint{
		None 					= 0x0,
		Access				= 1ul << 0,
		App 					= 1ul << 1,
		Cache	 	  		= 1ul << 2,
		Client  			= 1ul << 3,
		Crypto				= 1ul << 4,
		DBDriver			= 1ul << 5,
		Exception			= 1ul << 6,
		ExternalLogger= 1ul << 7,
		Http  				= 1ul << 8,
		IO			 			= 1ul << 9,
		Locks	 	  		= 1ul << 10,
		Parsing 			= 1ul << 11,
		Pedantic 			= 1ul << 12,
		QL			 			= 1ul << 13,
		Read	  			= 1ul << 14,
		Scheduler	 		= 1ul << 15,
		Server  			= 1ul << 16,
		Sessions  		= 1ul << 17,
		Settings 			= 1ul << 18,
		Shutdown 			= 1ul << 19,
		Socket  			= 1ul << 20,
		Sql						= 1ul << 21,
		Startup 			= 1ul << 22,
		Subscription 	= 1ul << 23,
		Test					= 1ul << 24,
		Threads				= 1ul << 25,
		Write		 			= 1ul << 26,
		All						= ~0ull,

		HttpClientRead	  = Http | Client | Read,
		HttpClientWrite		= Http | Client | Write,
		HttpClientSessions= Http | Client | Sessions,
		HttpServerRead	  = Http | Server | Read,
		HttpServerWrite		= Http | Server | Write,
		SocketClientRead  = Socket | Client | Read,
		SocketClientReadSub  = Socket | Client | Read | Subscription,
		SocketClientWrite	= Socket | Client | Write,
		SocketClientWriteSub	= Socket | Client | Write | Subscription,
		SocketServerRead  = Socket | Server | Read,
		SocketServerWrite	= Socket | Server | Write
	};

	namespace Logging{
		struct ILogger;
		Φ UpdateCumulative( const vector<up<Logging::ILogger>>& loggers )ι->void;
	}
	struct Γ LogTags{
		LogTags( ELogLevel defaultLevel=ELogLevel::Information ):_minLevel{defaultLevel},_defaultLevel{defaultLevel},_settingsDefault{defaultLevel}{}
		LogTags( const jobject& o )ι;
		LogTags( const LogTags& x )ι;
		α operator+=( const LogTags& other )ι->LogTags&;
		//settings: the map is the configuration (Logging::Init's /logging/memory/tags), remembered as what a cleared override
		//falls back to.  Otherwise the map is overrides - the instance_tag_levels rows at start (IApp::LoadLogSettings) - which
		//sit on top of the settings and can be cleared back off.
		α SetLevels( const jobject& tagLevels, bool settings=false )ι->void;
		β Name()Ι->sv{ return "Cumulative"; }
		α DefaultLevel()Ι->ELogLevel{ return _defaultLevel; }
		α SetDefaultLevel( ELogLevel level )ι->void{ _defaultLevel = level; ExtrapolatedTags = _configuredTags; }
		α ClearDefaultLevel()ι->void{ SetDefaultLevel( _settingsDefault ); }//the default override deleted: back to the settings' default.
		β MinLevel()Ι->ELogLevel{ return _minLevel; }
		β MinLevel( ELogTags tags )Ι->ELogLevel;
		β SetMinLevel( ELogLevel level )ι->void{ _minLevel = level; }
		α SetLevel( ELogTags tags, ELogLevel level )ι->void;
		//drops the override - the runtime twin of deleting the instance_tag_levels row - so the tag goes back to the level the
		//settings gave it, or to _defaultLevel when they gave none.  Erasing the entry outright lost the settings level until a
		//restart (install-issues #21).
		α ClearLevel( ELogTags tags )ι->void;
		β ShouldLog( ELogLevel level, ELogTags tags )Ι->bool;
		β ToString()ι->string;
		α ConfiguredTags()Ι->const concurrent_flat_map<ELogTags,ELogLevel>&{ return _configuredTags; }//the levels in force: the settings' with the overrides on top.
	protected:
		concurrent_flat_map<ELogTags,ELogLevel> _configuredTags;
		concurrent_flat_map<ELogTags,ELogLevel> _settingsTags;//the levels as configured, before any override - what ClearLevel restores.
		mutable concurrent_flat_map<ELogTags,ELogLevel> ExtrapolatedTags;
		ELogLevel	_minLevel;
		ELogLevel _defaultLevel;
		ELogLevel _settingsDefault;
		friend α Logging::UpdateCumulative( const vector<up<Logging::ILogger>>& loggers )ι->void;
	};

	constexpr ELogTags DefaultTag=ELogTags::App;
	constexpr sv TagSeparator{ "." };//how a combined tag is spelled as one name: "socket.client.read" - config keys, ui rows and the joined form of the wire's tag array.
	Φ ShouldTrace( ELogTags tags )ι->bool;
	Φ ToString( ELogTags tags, bool outputArray=true )ι->string;
	Φ ToValue( ELogTags tags )ι->jvalue;
	Φ ToLogTags( sv name )ι->ELogTags;
	Φ ToLogTags( jvalue v )ι->ELogTags;
	Φ ToTagName( const jvalue& tags )ι->string;//["socket","client","read"] | "sql" -> "socket.client.read" | "sql" - the TagSeparator spelling ToLogTags splits.
	//The two wire shapes that carry tags as values, flattened to the tag->level map SetLevels and the config file use:
	Φ ToTagLevels( const jobject& levelTags )ι->jobject;//instanceTagLevel's answer - {"Debug":["sql",["socket","client","read"]]}
	Φ ToTagLevels( const jarray& tagLevels )ι->jobject;//updateInstanceTagLevel's argument - [{tags:["socket","client","read"],level:"Debug"}]
	Φ ToTagLevelArray( const jobject& tagLevels )ι->jarray;//and back the other way, for a caller holding the flat map.
namespace Logging{
	struct ITagParser{
		virtual ~ITagParser()=default;
		β ToTag( str tagName )Ι->ELogTags=0;
		β ToString( ELogTags tags )Ι->string=0;
		β Tags()Ι->flat_map<string,uint> = 0;
	};
	Φ AddTagParser( up<ITagParser>&& tagParser )ι->void;
	Φ ShouldLog( ELogLevel level, ELogTags tags )ι->bool;
	Φ Tags( bool user=false )ι->flat_map<string,uint>;
}}
#undef Φ
#endif