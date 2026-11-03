#pragma once

#include "../usings.h"
#include "../exports.h"
#include <jde/fwk/io/Cache.h>
#include <jde/db/Row.h>
#include <jde/db/awaits/SelectAwait.h>
#define let const auto
namespace Jde::DB{
	struct IDataSource;
	using RowΛ=function<void( Row&& )ε>;
	Τ using CoRowΛ=function<void( T& pResult, Row&& r )ε>;

	//A select folded into one T - the shape behind SelectMap/SelectEnum, where every row is a map entry.  `fold` is
	//applied to each row in order and the T is what the caller is owed; Suspend hands that to RunQuery as the
	//projection, so this is the same coroutine every other awaitable over IDataSource::Query runs, and a fold that
	//throws reaches the caller through the promise like a driver error would.
	Τ struct TSelectAwait : TAwait<T>{
		using base=TAwait<T>;
		TSelectAwait( sp<IDataSource> ds, Sql&& sql, CoRowΛ<T> fold, SL sl )ι:
			base{ sl }, _ds{ move(ds) }, _sql{ move(sql) }, _fold{ move(fold) }
		{}
		α Suspend()ι->void override{
			RunQuery( *this, *_ds, move(_sql), false, base::_sl, [&]( Result&& r ){
				T y{};
				for( auto&& row : r.Rows )
					_fold( y, move(row) );
				return y;
			});
		}
	private:
		sp<IDataSource> _ds;
		Sql _sql;
		CoRowΛ<T> _fold;
	};

	//A TSelectAwait answered from `cache` when `key` is there, and stored under it when the select has to run.  `reload`
	//skips the lookup, so the select always runs, and the entry it replaces keeps answering everyone else until then.
	Τ struct CacheAwait final: TSelectAwait<T>{
		CacheAwait( sp<IDataSource> ds, Sql&& sql, CoRowΛ<T> fold, Cache<string,T>& cache, string key, optional<steady_clock::duration> ttl, bool reload, SL sl ):
			TSelectAwait<T>{ move(ds), move(sql), move(fold), sl },
			_cache{ cache },
			_key{ move(key) },
			_ttl{ ttl },
			_reload{ reload }
		{}
		α await_ready()ι->bool override;
		α await_resume()ε->T override;
	private:
		Cache<string,T>& _cache;
		sp<const T> _cached;
		string _key;
		optional<steady_clock::duration> _ttl;
		bool _reload;
		uint _ticket{};
	};

	Ŧ CacheAwait<T>::await_ready()ι->bool{
		if( !_reload )
			_cached = _cache.Get( _key );
		if( !_cached )
			_ticket = _cache.Ticket();//before the select reads, so a slower read that started earlier can't replace this one.
		return _cached!=nullptr;
	}

	Ŧ CacheAwait<T>::await_resume()ε->T{
		if( _cached )
			return *_cached;
		auto y = TSelectAwait<T>::await_resume();
		return *_cache.Set( move(_key), move(y), _ttl, _ticket );//move into the cache, copy out once - `Set(key, y); return y;` copies twice.
	}
}
#undef let