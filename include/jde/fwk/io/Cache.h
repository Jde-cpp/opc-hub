#pragma once
#include <jde/fwk/settings.h>

#define let const auto
namespace Jde{
	//A ttl map.  Get answers an entry until its deadline and Set replaces one.  Expiry is checked by the read, so nothing runs
	//between calls; Set drops the expired entries once the map holds twice what the last drop left, so they can't accumulate.
	template<class K, class V> struct Cache final{
		static constexpr steady_clock::duration Forever = steady_clock::duration::max();
		explicit Cache( steady_clock::duration defaultTtl )ι:_defaultTtl{ defaultTtl }{}
		Cache()ι:Cache{ Settings::FindDuration("/cache/default/duration").value_or(1h) }{}//reads settings, so a static instance goes behind a function.
		α DefaultTtl()Ι->steady_clock::duration{ return _defaultTtl; }
		α Get( const K& key )Ι->sp<const V>;//null past the deadline, as for a key never set.
		//ttl unset is the default, Forever sets no deadline.  Zero-copy - the cache shares the caller's instance.
		α Set( K key, sp<const V> value, optional<steady_clock::duration> ttl={} )ι->sp<const V>;
		α Set( K key, V value, optional<steady_clock::duration> ttl={} )ι->sp<const V>{ return Set( move(key), sp<const V>{ms<const V>(move(value))}, ttl ); }
		α Clear( const K& key )ι->bool;//true when there was an entry, expired or not.
		α Size()Ι->size_t{ rl _{ _mutex }; return _entries.size(); }//expired entries included, until a Set drops them.
	private:
		struct Entry{ sp<const V> Value; steady_clock::time_point Deadline; };
		steady_clock::duration _defaultTtl;
		mutable absl::Mutex _mutex;
		flat_map<K,Entry> _entries ABSL_GUARDED_BY(_mutex);
		size_t _dropAt ABSL_GUARDED_BY(_mutex) = 16;//the size at which Set next drops the expired entries.
	};

	ẗ Cache<K,V>::Get( const K& key )Ι->sp<const V>{
		rl _{ _mutex };
		if( let p = _entries.find(key); p!=_entries.end() && p->second.Deadline>steady_clock::now() )
			return p->second.Value;
		DBGT( ELogTags::Cache, "Cache miss for {}", key );
		return nullptr;
	}
	ẗ Cache<K,V>::Set( K key, sp<const V> value, optional<steady_clock::duration> ttl )ι->sp<const V>{
		let now = steady_clock::now();
		let live = ttl.value_or( _defaultTtl );
		let deadline = live==Forever ? steady_clock::time_point::max() : now+live;
		DBGT( ELogTags::Cache, "Cache set for {}", key );
		ul _{ _mutex };
		if( _entries.size()>=_dropAt ){
			for( auto p = _entries.begin(); p!=_entries.end(); )
				p = p->second.Deadline<=now ? _entries.erase( p ) : std::next( p );
			_dropAt = std::max<size_t>( 16, _entries.size()*2 );
		}
		_entries.insert_or_assign( move(key), Entry{value, deadline} );
		return value;
	}
	ẗ Cache<K,V>::Clear( const K& key )ι->bool{
		ul _{ _mutex };
		return _entries.erase( key )>0;
	}
}
#undef let