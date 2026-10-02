#include <jde/historian/Historian.h>

#define let const auto

namespace Jde::Opc::Hist{
	//libc++ loads the whole tz database for any zone, UTC included, so a host without it, such as a minimal container,
	//fails here:  a startup error naming the cause, not a terminate.
	Ω utc( SL sl )ε->const std::chrono::time_zone&{
		try{
			return *std::chrono::locate_zone( "UTC" );
		}
		catch( const std::runtime_error& e ){
			THROWSL( "hist.timeZone defaults to UTC, which the time zone database could not give - is tzdata installed?  {}", e.what() );
		}
	}

	//A key that is there has to parse:  a silent default would leave the operator believing the config was applied.
	Ω find( const jobject& hist, sv key )ι->const jvalue*{
		let p = hist.if_contains( key );
		return p && !p->is_null() ? p : nullptr;
	}
	Ω asString( const jvalue& v, sv key, SL sl )ε->string{
		THROW_IFSL( !v.is_string(), "hist.{} must be a string, not {}.", key, serialize(v) );
		return string{ v.get_string() };
	}
	Ω positive( const jvalue& v, sv key, SL sl )ε->uint{
		let n = v.try_to_number<uint>();
		THROW_IFSL( !n || !*n, "hist.{} must be a positive integer, not {}.", key, serialize(v) );
		return *n;
	}
	//No fallback for a zone that isn't found:  fixing the name later would move every day's boundary under existing files.
	Ω timeZone( const jobject& hist, SL sl )ε->const std::chrono::time_zone&{
		let p = find( hist, "timeZone" );
		if( !p )
			return utc( sl );
		let name = asString( *p, "timeZone", sl );
		try{
			return *std::chrono::locate_zone( name );
		}
		catch( const std::runtime_error& e ){
			THROWSL( "hist.timeZone '{}' is not a known time zone: {}", name, e.what() );
		}
	}

	Settings::Settings( fs::path path, SL sl )ε:
		Path{ move(path) },
		TimeZone{ &utc(sl) }
	{}
	Settings::Settings( const jobject& hist, fs::path defaultPath, SL sl )ε:
		Path{ move(defaultPath) },
		TimeZone{ &timeZone(hist, sl) }{
		if( let p = find(hist, "path"); p )
			Path = asString( *p, "path", sl );
		if( let p = find(hist, "delay"); p ){
			let iso = asString( *p, "delay", sl );
			let delay = Chrono::TryToDuration( string{iso}, ELogLevel::Debug, sl );
			THROW_IFSL( !delay, "hist.delay '{}' is not an ISO 8601 duration.", iso );
			Delay = *delay;
		}
		if( let p = find(hist, "maxBuffer"); p )
			MaxBuffer = positive( *p, "maxBuffer", sl );
		if( let p = find(hist, "readLimit"); p )
			ReadLimit = positive( *p, "readLimit", sl );
		THROW_IFSL( Path.empty(), "hist.path is empty." );
		THROW_IFSL( Delay<=Duration::zero(), "hist.delay must be positive, not {}.", Chrono::ToString(Delay) );
	}

	Historian::Historian( Settings settings, sp<IClock> clock )ι:
		_clock{ move(clock) },
		_settings{ move(settings) }
	{}

	//A group's name is the stem of its files, so it has to be one in any file system.
	Ω fileStem( sv name )ι->bool{
		return !name.empty() && std::ranges::all_of( name, []( char c ){ return std::isalnum((unsigned char)c) || c=='-' || c=='_'; } );
	}
	α Historian::AddGroup( GroupConfig config, vector<Member> members, SL sl )ε->sp<Group>{
		THROW_IFSL( !fileStem(config.Name), "'{}' cannot name a group's files.", config.Name );
		ul _{ _mutex };
		THROW_IFSL( _groups.contains(config.Name), "Group '{}' already exists.", config.Name );
		auto group = ms<Group>( move(config), _clock, move(members), Restored{}, sl );//#203 restores from the group's newest file.
		_groups.emplace( group->Name(), group );
		return group;
	}
	α Historian::RemoveGroup( sv name, optional<Writer> by, SL sl )ε->void{
		sp<Group> group;
		{
			ul _{ _mutex };
			auto p = _groups.find( name );
			THROW_IFSL( p==_groups.end(), "Group '{}' does not exist.", name );
			group = move( p->second );
			_groups.erase( p );
			_removed.push_back( group );
		}
		group->Close( move(by) );
	}
	α Historian::FindGroup( sv name )Ι->sp<Group>{
		ul _{ _mutex };
		auto p = _groups.find( name );
		return p==_groups.end() ? nullptr : p->second;
	}
}