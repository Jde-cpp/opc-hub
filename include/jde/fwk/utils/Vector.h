#pragma once
#ifndef TO_VEC_H //gcc pragma once is not supported
#define TO_VEC_H
#include <sstream>
#include <absl/synchronization/mutex.h>

namespace Jde{
	template<class T>
	struct Vector{
		Vector()ι{}
		Vector( uint size )ι:_items( size ){}
		α copy()Ι->vector<T>{ rl _{Mutex}; return _items; }

		α clear()ι{ ul _{Mutex}; _items.clear(); }
		α find( const T& x )ι->optional<T>{ rl _{Mutex}; auto p = std::ranges::find(_items, x); return p==_items.end() ? nullopt : optional<T>{*p}; }
		α erase( const T& x )ι->bool{ ul _{Mutex}; auto p = std::ranges::find(_items, x); bool found = p!=_items.end(); if( found ) _items.erase(p); return found; }
		α	erase( function<void(const T& p)> before )ι->void;
		α	rerase( function<void(const T& p)> before )ι->void;
		α	erase_if( function<bool(const T& p)> test )ι->void;

		α push_back( const T& val )ι{ ul _{Mutex}; _items.push_back(val); }
		α push_back( T&& val )ι{ ul _{Mutex}; _items.push_back(move(val)); }
		ABSL_EXCLUSIVE_LOCKS_REQUIRED(Mutex) α push_back_locked( T&& val )ι{ _items.push_back(move(val)); }//for a batch under one hold of Mutex.
		ψ emplace_back( Args&&... args )ι->T&{ ul _{Mutex}; return _items.emplace_back(std::forward<Args>(args)...); }
		α reserve( uint size )ι->void{ ul _{Mutex}; _items.reserve(size); }
		α empty()Ι->bool{ rl _{Mutex}; return _items.empty(); }
		α size()Ι->uint{ rl _{Mutex}; return _items.size(); }
		α visit( function<void(const T& p)> f )ι->void;

		mutable absl::Mutex Mutex;
	private:
		α drain( bool reverse, function<void(const T& p)> before )ι->void;
		vector<T> _items ABSL_GUARDED_BY(Mutex);
	};

	//erase/rerase drain the whole container, calling `before` on each element. The callback is invoked
	//*after* releasing the lock (on a moved-out snapshot) so a callback that re-enters this same Vector -
	//e.g. an IShutdown::Shutdown calling RemoveShutdown - doesn't self-deadlock on the non-recursive mutex.
	Ŧ	Vector<T>::erase( function<void(const T& p)> before )ι->void{ drain( false, move(before) ); }
	Ŧ	Vector<T>::rerase( function<void(const T& p)> before )ι->void{ drain( true, move(before) ); }
	Ŧ	Vector<T>::drain( bool reverse, function<void(const T& p)> before )ι->void{
		vector<T> snapshot;
		{
			ul _{ Mutex };
			snapshot.reserve( _items.size() );
			if( reverse )
				std::move( _items.rbegin(), _items.rend(), std::back_inserter(snapshot) );//rerase: last registered is called first.
			else
				std::move( _items.begin(), _items.end(), std::back_inserter(snapshot) );
			_items.clear();
		}
		for( auto& p : snapshot )
			before( p );
	}
	Ŧ	Vector<T>::erase_if( function<bool(const T& p)> test )ι->void{
		ul _{ Mutex };
		std::erase_if( _items, test );
	}
	Ŧ	Vector<T>::visit( function<void(const T& p)> f )ι->void{
		ul _{ Mutex };
		for( const auto& item : _items )
			f( item );
	}
}
#endif