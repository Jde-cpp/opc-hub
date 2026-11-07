#pragma once
#include <jde/fwk/settings.h>

#define let const auto
namespace Jde{
	//A ttl map.  Get answers an entry until its deadline and Set replaces one.  Expiry is checked by the read, so nothing runs
	//between calls; Set drops the expired entries once the map holds twice what the last drop left, so they can't accumulate.
	//
	//A value read from somewhere slow is Set with the Ticket taken before the read began, so when two reads of one key finish
	//out of order the later read stays cached, not the slower one.  A plain Set counts as a read taken at the Set.  A writer
	//re-reads or Clears after its write, so its own later ticket covers what an earlier read missed.  Clear leaves the key a
	//marker holding its ticket for the default ttl, so a read ticketed before the Clear can't land after it.
	template<class K, class V> struct Cache final{
		static constexpr steady_clock::duration Forever = steady_clock::duration::max();
		explicit Cache( steady_clock::duration defaultTtl )ι:_defaultTtl{ defaultTtl }{}
		Cache()ι:Cache{ Settings::FindDuration("/cache/default/duration").value_or(1h) }{}//reads settings, so a static instance goes behind a function.
		α DefaultTtl()Ι->steady_clock::duration{ return _defaultTtl; }
		α Get( const K& key )Ι->sp<const V>;//null past the deadline, as for a key never set.
		α Ticket()ι->uint{ return ++_sequence; }
		//ttl unset is the default, Forever sets no deadline.  A live entry from a later ticket is kept, and this value only
		//returned.  Zero-copy - the cache shares the caller's instance.
		α Set( K key, sp<const V> value, optional<steady_clock::duration> ttl={}, optional<uint> ticket={} )ι->sp<const V>;
		α Set( K key, V value, optional<steady_clock::duration> ttl={}, optional<uint> ticket={} )ι->sp<const V>{ return Set( move(key), sp<const V>{ms<const V>(move(value))}, ttl, ticket ); }
		α Clear( const K& key )ι->bool;//true when there was a value, expired or not.
		α Size()Ι->uint{ rl _{ _mutex }; return _entries.size(); }//expired entries and Clear's markers included, until a Set drops them.
	private:
		struct Entry{ sp<const V> Value; steady_clock::time_point Deadline; uint Ticket; };//Value is null in Clear's marker.
		Ω deadline( steady_clock::duration ttl, steady_clock::time_point now )ι->steady_clock::time_point{ return ttl==Forever ? steady_clock::time_point::max() : now+ttl; }
		steady_clock::duration _defaultTtl;
		std::atomic<uint> _sequence{};
		mutable absl::Mutex _mutex;
		flat_map<K,Entry> _entries ABSL_GUARDED_BY(_mutex);
		uint _dropAt ABSL_GUARDED_BY(_mutex) = 16;//the size at which Set next drops the expired entries.
	};

	ẗ Cache<K,V>::Get( const K& key )Ι->sp<const V>{
		{
			rl _{ _mutex };
			if( let p = _entries.find(key); p!=_entries.end() && p->second.Value && p->second.Deadline>steady_clock::now() )
				return p->second.Value;
		}
		TRACET( ELogTags::Cache, "Cache miss for {}", key );//outside the lock:  a waiting Set or Clear would hold every later Get behind the log call.
		return nullptr;
	}
	ẗ Cache<K,V>::Set( K key, sp<const V> value, optional<steady_clock::duration> ttl, optional<uint> ticket )ι->sp<const V>{
		let now = steady_clock::now();
		let until = deadline( ttl.value_or(_defaultTtl), now );
		TRACET( ELogTags::Cache, "Cache set for {}", key );
		bool superseded;
		{
			ul _{ _mutex };
			if( _entries.size()>=_dropAt ){
				std::erase_if( _entries, [&]( let& e ){ return e.second.Deadline<=now; } );//one pass:  an erase at a time shifts the rest each time.
				_dropAt = std::max<uint>( 16, _entries.size()*2 );
			}
			let issued = ticket ? *ticket : Ticket();
			let p = _entries.find( key );
			superseded = p!=_entries.end() && p->second.Deadline>now && p->second.Ticket>issued;
			if( !superseded )
				_entries.insert_or_assign( move(key), Entry{value, until, issued} );
		}
		if( superseded )
			TRACET( ELogTags::Cache, "Cache set for {} superseded:  a later read is cached.", key );
		return value;
	}
	ẗ Cache<K,V>::Clear( const K& key )ι->bool{
		let until = deadline( _defaultTtl, steady_clock::now() );
		ul _{ _mutex };
		let p = _entries.find( key );
		let had = p!=_entries.end() && p->second.Value;
		_entries.insert_or_assign( key, Entry{nullptr, until, Ticket()} );//ticketed under the lock, so no Set lands between the two.
		return had;
	}
}
#undef let