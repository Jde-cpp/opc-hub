#include <jde/historian/Historian.h>

#define let const auto

namespace Jde::Opc::Hist{
	Ω utc()ι->const std::chrono::time_zone&{ return *std::chrono::locate_zone( "UTC" ); }

	Settings::Settings( fs::path path )ι:
		Path{ move(path) },
		TimeZone{ &utc() }
	{}
	Settings::Settings( const jobject& hist, fs::path defaultPath, SL sl )ε:
		Path{ Json::FindString(hist, "path").value_or(move(defaultPath).string()) },
		Delay{ Json::FindDuration(hist, "delay", ELogLevel::Error, sl).value_or(1min) },
		MaxBuffer{ Json::FindNumber<uint>(hist, "maxBuffer").value_or(64*1024*1024) },
		TimeZone{ &Json::FindTimeZone(hist, "timeZone", utc(), ELogLevel::Error, sl) },
		ReadLimit{ Json::FindNumber<uint>(hist, "readLimit").value_or(10'000) }{
		THROW_IFSL( Path.empty(), "hist.path is empty." );
		THROW_IFSL( Delay<=Duration::zero(), "hist.delay must be positive, not {}.", Chrono::ToString(Delay) );
		THROW_IFSL( !ReadLimit, "hist.readLimit must be positive." );
	}

	Historian::Historian( Settings settings, sp<IClock> clock )ι:
		_clock{ move(clock) },
		_settings{ move(settings) }
	{}

	//A group's name is the stem of its files, so it has to be one in any file system.
	Ω fileStem( sv name )ι->bool{
		return !name.empty() && std::ranges::all_of( name, []( char c ){ return std::isalnum((unsigned char)c) || c=='-' || c=='_'; } );
	}
	α Historian::AddGroup( GroupConfig config, SL sl )ε->sp<Group>{
		THROW_IFSL( !fileStem(config.Name), "'{}' cannot name a group's files.", config.Name );
		ul _{ _mutex };
		THROW_IFSL( _groups.contains(config.Name), "Group '{}' already exists.", config.Name );
		auto group = ms<Group>( move(config), _clock );
		_groups.emplace( group->Name(), group );
		return group;
	}
	α Historian::FindGroup( sv name )Ι->sp<Group>{
		ul _{ _mutex };
		auto p = _groups.find( name );
		return p==_groups.end() ? nullptr : p->second;
	}
}