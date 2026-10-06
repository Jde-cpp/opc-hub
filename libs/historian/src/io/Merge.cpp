#include "Merge.h"
#include <numeric>
#include <jde/fwk/exceptions/IOException.h>

#define let const auto

namespace Jde::Opc::Hist{
	using Proto::HistoryRecord;
	namespace{
		constexpr Ticks Earliest{ std::numeric_limits<Ticks>::min() };

		//A run's bytes through the merge's one handle, which each open run reads at its own position.
		struct Slice final : google::protobuf::io::CopyingInputStream{
			Slice( const ReadHandle& file, uint position, uint end )ι:_file{ file }, _position{ position }, _end{ end }{}
			α Read( void* buffer, int size )ι->int override{
				let wanted = std::min<uint>( (uint)size, _end-_position );
				if( !wanted )
					return 0;
				let read = _file.Read( _position, buffer, wanted ).value_or( 0 );
				_position += read;
				return read ? (int)read : -1;//short of the run's end is a failed read.
			}
		private:
			const ReadHandle& _file;
			uint _position;
			const uint _end;
		};
		//What orders the heap:  whether a comes out after b.
		constexpr auto later = []( let& a, let& b )ι{ return a->Time>b->Time || (a->Time==b->Time && a->Index>b->Index); };
	}

	struct Merge::Source final{
		struct Stream final{
			Stream( const ReadHandle& file, const Run& run )ι:
				Bytes{ file, run.Offset, run.End },
				In{ &Bytes, (int)std::clamp<uint>(run.End-run.Offset, 1, 8192) },
				Records{ In, run.Offset, run.End, run.Chain }
			{}
			Slice Bytes;
			google::protobuf::io::CopyingInputStreamAdaptor In;
			Reader Records;
		};
		uint Index;
		up<Stream> File;//none for the flush's records.
		HistoryRecord Record;
		Ticks Time{ Earliest };
		Merge::Position Where{};//Record's, in the file.
	};

	Merge::Merge( sp<ReadHandle> file, vector<Run> runs, vector<HistoryRecord> late, SL sl )ι:
		_file{ move(file) },
		_runs{ move(runs) },
		_late{ move(late) },
		_sl{ sl }{
		_order.resize( _runs.size()+(_late.empty() ? 0 : 1) );
		std::iota( _order.begin(), _order.end(), 0u );
		std::ranges::stable_sort( _order, {}, [this]( uint index ){ return First(index); } );
	}
	Merge::~Merge()=default;

	α Merge::First( uint index )Ι->Ticks{
		return index<_runs.size() ? _runs[index].First : PrimaryTime( _late.front() ).value_or( Earliest );
	}

	α Merge::Advance( Source& source )ε->bool{
		auto& r = source.Record;
		if( !source.File ){
			if( _lateNext==_late.size() )
				return false;
			r = move( _late[_lateNext++] );
		}
		else{
			auto& records = source.File->Records;
			do{
				source.Where.Start = records.Offset();
				if( records.Next(r) ){
					source.Where.End = records.Offset();
					continue;
				}
				if( let stop = *records.Stop(); stop!=EStop::End ){
					_unreadable = true;
					throw IO::IOException{ _sl, _file->Path, ELogLevel::Error, "reads {} at byte {}, in what its scan kept, so it can't be read through", ToString(stop), records.Offset() };
				}
				return false;
			}while( r.has_file_start() || r.has_checkpoint() );
		}
		source.Time = std::max( source.Time, PrimaryTime(r).value_or(source.Time) );//never back, so the heap holds whatever a run does.
		return true;
	}

	α Merge::Next( HistoryRecord& r )ε->bool{
		while( _next<_order.size() && (_open.empty() || First(_order[_next])<=_open.front()->Time) ){
			let index = _order[_next++];
			auto source = mu<Source>( index, index<_runs.size() ? mu<Source::Stream>(*_file, _runs[index]) : nullptr );
			if( Advance(*source) ){
				_open.push_back( move(source) );
				std::ranges::push_heap( _open, later );
			}
		}
		if( _open.empty() ){
			_file.reset();//before the rewrite renames over it, which Windows may refuse while it is open.
			return false;
		}
		std::ranges::pop_heap( _open, later );
		auto& source = *_open.back();
		r = move( source.Record );
		_where = source.File ? optional<Position>{ source.Where } : nullopt;
		if( Advance(source) )
			std::ranges::push_heap( _open, later );
		else
			_open.pop_back();
		return true;
	}
}
