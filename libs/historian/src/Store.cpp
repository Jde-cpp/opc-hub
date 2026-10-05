#include "Store.h"

#define let const auto

namespace Jde::Opc::Hist{
	constexpr ELogTags _tags{ ELogTags::IO };

	Store::Store( Settings settings, sp<IClock> clock )ι:
		Config{ move(settings) },
		Time{ move(clock) }{
		try{
			if( auto lock = PathLock::TryLock(Config.Path); lock )
				Lock.emplace( move(*lock) );
			else
				Disabled = Ƒ( "another historian holds the lock on '{}'", Config.Path.string() );
		}
		catch( const std::exception& e ){
			Disabled = e.what();
		}
		if( !Lock )
			CRITICAL( "The historian is disabled, and its host runs without it:  {}.", Disabled );
	}

	α Store::Register( Group& group )ι->void{
		ul _{ _mutex };
		_groups.push_back( &group );
	}
	α Store::Unregister( Group& group )ι->void{
		ul _{ _mutex };
		std::erase( _groups, &group );
	}

	α Store::RequestTrim()ι->void{
		if( !_trimRequested.exchange(true) ){
			Time->Schedule( Duration::zero(), [weak=weak_from_this()]{
				if( auto store = weak.lock() )
					store->Trim();
			});
		}
	}
	α Store::Trim()ι->void{
		_trimRequested = false;//one asked for from here on follows this.
		vector<string> began;//each group whose drops begin a streak.
		{
			ul _{ _mutex };
			let low = Buffered()>Config.MaxBuffer ? Config.MaxBuffer-Config.MaxBuffer/8 : Config.MaxBuffer;
			for( auto buffered = Buffered(); buffered>low; buffered = Buffered() ){
				Group* oldest{};
				constexpr auto none = std::numeric_limits<uint>::max();
				uint first{ none }, second{ none };//the oldest value of any group's, and the oldest of the others'.
				for( auto group : _groups ){
					let sequence = group->Oldest();
					if( !sequence )
						continue;
					if( *sequence<first ){
						second = first;
						first = *sequence;
						oldest = group;
					}
					else
						second = std::min( second, *sequence );
				}
				if( !oldest )//membership changes are all that is left, and they are never dropped.
					break;
				bool streak{};
				oldest->Drop( second, buffered-low, streak );
				if( streak )
					began.push_back( oldest->Name() );
			}
		}
		for( let& group : began )
			WARN( "The historian's buffers passed hist.maxBuffer, {} bytes:  group '{}' drops its oldest values until it can write the rest, and each node that loses any is marked Bad_DataLost.", Config.MaxBuffer, group );
	}
}
