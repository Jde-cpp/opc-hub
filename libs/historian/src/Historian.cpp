#include <jde/historian/Historian.h>
#include <boost/asio/io_context.hpp>
#include <jde/fwk/process/execution.h>
#include "Store.h"

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

	//Against the current directory, as the lock, the directories and the scans resolve it, since a write through
	//IO::WriteAwait on Windows takes the path as it is:  its \\?\ prefix turns relative resolution off.
	Ω absolute( const fs::path& path, SL sl )ε->fs::path{
		THROW_IFSL( path.empty(), "hist.path is empty." );
		std::error_code ec;
		auto y = fs::absolute( path, ec );
		THROW_IFSL( ec, "hist.path '{}' can't be made absolute:  {}", path.string(), ec.message() );
		return y;
	}

	Settings::Settings( fs::path path, SL sl )ε:
		Path{ absolute(path, sl) },
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
			MaxBuffer = std::max( positive(*p, "maxBuffer", sl), MinBuffer );//as ProtoLog clamps its own.
		if( let p = find(hist, "readLimit"); p )
			ReadLimit = positive( *p, "readLimit", sl );
		Path = absolute( Path, sl );
		THROW_IFSL( Delay<=Duration::zero(), "hist.delay must be positive, not {}.", Chrono::ToString(Delay) );
	}

	Historian::Historian( Settings settings, sp<IClock> clock )ι:
		_store{ ms<Store>(move(settings), move(clock)) }
	{}
	Historian::~Historian(){
		vector<sp<Group>> groups;
		{
			ul _{ _mutex };
			groups = _removed;
			for( let& [_,group] : _groups )
				groups.push_back( group );
		}
		if( let ioc = ExecutorIoc(); ioc && ioc->get_executor().running_in_this_thread() )
			WARNT( ELogTags::IO, "The historian is ending on an executor thread, which its groups' last flushes may need:  with one, they don't write before hist's stop limit." );
		for( let& group : groups )
			group->Stopping();
		let deadline = std::chrono::steady_clock::now()+_store->Config.StopLimit;
		for( let& group : groups )
			group->Stopped( deadline );
		_store->Lock.reset();
	}
	α Historian::Enabled()Ι->bool{ return _store->Disabled.empty(); }
	α Historian::Config()Ι->const Settings&{ return _store->Config; }
	α Historian::Time()Ι->IClock&{ return *_store->Time; }
	α Historian::Buffered()Ι->uint{ return _store->Buffered(); }

	//A group's name is the stem of its files, so it has to be one in any file system.
	Ω fileStem( sv name )ι->bool{
		return !name.empty() && std::ranges::all_of( name, []( char c ){ return std::isalnum((unsigned char)c) || c=='-' || c=='_'; } );
	}
	α Historian::AddGroup( GroupConfig config, vector<Member> members, SL sl )ε->sp<Group>{
		THROW_IFSL( !Enabled(), "The historian is disabled:  {}.", _store->Disabled );
		THROW_IFSL( !fileStem(config.Name), "'{}' cannot name a group's files.", config.Name );
		let name = config.Name;
		{
			ul _{ _mutex };
			THROW_IFSL( _groups.contains(name) || _adding.contains(name), "Group '{}' already exists.", name );
			//One still writing what it buffered holds the files a new group of its name would write.
			std::erase_if( _removed, []( let& group ){ return group->EndIfWritten(); } );
			THROW_IFSL( std::ranges::contains(_removed, name, &Group::Name), "Group '{}' was removed, and what it buffered is not yet written.", name );
			_adding.emplace( name );
		}
		//Its files' first-open scans, outside the lock, which FindGroup and RemoveGroup take:  the name is held meanwhile.
		sp<Group> group;
		try{
			group = ms<Group>( move(config), _store, move(members), sl );
		}
		catch( const runtime_error& r ){
			ul _{ _mutex };
			_adding.erase( name );
			throw;
		}
		ul _{ _mutex };
		_adding.erase( name );
		_groups.emplace( name, group );
		group->Start();
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
			std::erase_if( _removed, []( let& removed ){ return removed->EndIfWritten(); } );
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