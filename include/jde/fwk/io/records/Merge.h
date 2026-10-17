#pragma once
#include <algorithm>
#include <numeric>
#include <jde/fwk/io/FileAwait.h>
#include "Reader.h"

namespace Jde::IO::Records{
	//A day's records in time order:  the runs of its file, each in order, merged with one more in memory, late, in order
	//too.  Ties go to the earlier run, and to the file's before late's, so the merge is as stable as a sort.  A run is
	//opened only when the merge reaches its first time, so memory follows how many runs overlap, not how many there are:  a
	//window per open run, which the caller fetches (Wants/Feed).  FileStart and checkpoint records are left out.  A record
	//with no time is filed under its run's last, so it stays where its run put it.
	template<Record T> struct Merge final : noncopyable{
		//late is in order too.  file, which the runs are of, is open when there are runs; it is let go at the end, before a
		//rewrite renames over it, which Windows may refuse while it is open.
		Merge( sp<OpenFile> file, vector<Run> runs, vector<T> late, SRCE )ι;
		//Record:  r.  More:  Wants/Feed first.  End:  the file is let go.  Throws, at Error, when a run can't be read
		//through:  a run its scan kept no longer reads as it did, so the file changed (Unreadable).
		α Next( T& r )ε->ENext;
		α Wants()Ι->optional<Window>{ return _filling && _filling->In ? _filling->In->Wants() : nullopt; }
		α Feed( string&& bytes )ι->void{ if( _filling && _filling->In ) _filling->In->Feed( move(bytes) ); }
		α File()Ι->const sp<OpenFile>&{ return _file; }
		α Unreadable()Ι->bool{ return _unreadable; }
		//Where the last record Next returned lies in the file:  none for one of late's.
		struct Position final{ uint Start; uint End; };
		α Where()Ι->optional<Position>{ return _where; }
	private:
		static constexpr Time Earliest{ std::numeric_limits<Time>::min() };
		struct Source final{
			uint Index;
			up<Reader<T>> In;//none for late's.
			T Record;
			Time At{ Earliest };
			Position Where{};//Record's, in the file.
		};
		//What orders the heap:  whether a comes out after b.
		static constexpr auto later = []( const up<Source>& a, const up<Source>& b )ι{ return a->At>b->At || (a->At==b->At && a->Index>b->Index); };
		α First( uint index )Ι->Time;
		α Advance( Source& source )ε->ENext;//its next record:  End at its end.

		sp<OpenFile> _file;
		const vector<Run> _runs;
		vector<T> _late;
		uint _lateNext{};
		vector<uint> _order;//each run's index, and late's after them, by first time.
		uint _next{};//into _order:  the first not yet open.
		vector<up<Source>> _open;//a heap, the earliest record first.
		up<Source> _filling;//the source reading its next record, out of the heap until it has it.
		optional<Position> _where;
		bool _unreadable{};
		SL _sl;
	};

	template<Record T> Merge<T>::Merge( sp<OpenFile> file, vector<Run> runs, vector<T> late, SL sl )ι:
		_file{ move(file) },
		_runs{ move(runs) },
		_late{ move(late) },
		_sl{ sl }{
		_order.resize( _runs.size()+(_late.empty() ? 0 : 1) );
		std::iota( _order.begin(), _order.end(), 0u );
		std::ranges::stable_sort( _order, {}, [this]( uint index ){ return First(index); } );
	}
	template<Record T> α Merge<T>::First( uint index )Ι->Time{
		return index<_runs.size() ? _runs[index].First : Traits<T>::Time( _late.front() ).value_or( Earliest );
	}
	template<Record T> α Merge<T>::Advance( Source& source )ε->ENext{
		auto& r = source.Record;
		if( !source.In ){
			if( _lateNext==_late.size() )
				return ENext::End;
			r = move( _late[_lateNext++] );
		}
		else{
			auto& in = *source.In;
			for( ;; ){
				source.Where.Start = in.Offset();
				const auto next = in.Next( r );
				if( next==ENext::More )
					return ENext::More;
				if( next==ENext::End ){
					if( const auto stop = *in.Stop(); stop!=EStop::End ){
						_unreadable = true;
						throw IOException{ _sl, _file->Path(), ELogLevel::Error, "reads {} at byte {}, in what its scan kept, so it can't be read through", ToString(stop), in.Offset() };
					}
					return ENext::End;
				}
				source.Where.End = in.Offset();
				if( in.Field()!=Traits<T>::StartField && in.Field()!=Traits<T>::CheckpointField )
					break;
			}
		}
		source.At = std::max( source.At, Traits<T>::Time(r).value_or(source.At) );//never back, so the heap holds whatever a run does.
		return ENext::Record;
	}
	template<Record T> α Merge<T>::Next( T& r )ε->ENext{
		for( ;; ){
			if( _filling ){
				const auto next = Advance( *_filling );
				if( next==ENext::More )
					return ENext::More;
				if( next==ENext::Record ){
					_open.push_back( move(_filling) );
					std::ranges::push_heap( _open, later );
				}
				_filling.reset();
			}
			if( _next<_order.size() && (_open.empty() || First(_order[_next])<=_open.front()->At) ){
				const auto index = _order[_next++];
				_filling = mu<Source>( index, index<_runs.size() ? mu<Reader<T>>(_runs[index]) : nullptr );
				continue;
			}
			if( _open.empty() ){
				_file.reset();
				return ENext::End;
			}
			std::ranges::pop_heap( _open, later );
			auto source = move( _open.back() );
			_open.pop_back();
			r = move( source->Record );
			_where = source->In ? optional<Position>{ source->Where } : nullopt;
			_filling = move( source );//its next record, read before the next call takes from the heap.
			return ENext::Record;
		}
	}
}