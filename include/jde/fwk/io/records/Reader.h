#pragma once
#include <expected>
#include <functional>
#include "Traits.h"

namespace Jde::IO::Records{
	//r from raw, or why it isn't a record:  a field this build doesn't know, a frame that doesn't parse, or one whose length
	//isn't its record's.  A length garbled to claim the frames after it parses, as a record with fields repeated, and gives
	//itself away re-measured:  what the layer writes is the record's own serialization.  The layer's two frames are measured
	//by their own decoders.
	template<Record T> α Parse( const Raw& raw, T& r )ι->optional<EStop>{
		if( !Traits<T>::Known(raw.Field) )
			return EStop::Unknown;
		if( !r.ParseFromArray(raw.Frame.data(), (int)raw.Frame.size()) )
			return EStop::Body;
		if( raw.Field!=Traits<T>::StartField && raw.Field!=Traits<T>::CheckpointField && r.ByteSizeLong()!=raw.Frame.size() )
			return EStop::Body;
		return nullopt;
	}

	//A run's records, each whole and with its times absolute, from bytes the caller fetches a window at a time:  Next is
	//More while Wants() names the window to Feed.  A frame longer than a window is read whole, the window grown to it.
	template<Record T> struct Reader final : noncopyable{
		Reader( Run run, uint windowBytes=MergeWindowBytes )ι;
		//Record:  r.  End:  Stop() is End at the run's end, else why it stopped short.
		α Next( T& r )ι->ENext;
		α Wants()Ι->optional<Window>{ return _wants; }
		α Feed( string&& bytes )ι->void;//fewer than wanted is Short:  the file no longer holds them.
		α Offset()Ι->uint{ return _offset; }//where the next frame starts, or where reading stopped.
		α Field()Ι->uint32_t{ return _field; }//the last record's member:  a merge passes over StartField's and CheckpointField's.
		α Chain()Ι->State{ return _state; }
		α Stop()Ι->optional<EStop>{ return _stop; }
	private:
		Run _run;
		uint _windowBytes;
		string _buffer;//the bytes from _base.
		uint _base;
		uint _offset;
		State _state;
		optional<Window> _wants;
		optional<EStop> _stop;
		uint32_t _field{};
	};

	//The scan's hook into the owner's records:  each frame in file order, the FileStart's included, and when an append's
	//checkpoint proves the records since the last, or the end of the file disproves them.
	struct IDecoder{
		virtual ~IDecoder()=default;
		//A frame's time, none for a record with none; Body or Unknown for one that isn't a record.
		β Decode( const Raw& raw )ι->std::expected<optional<Time>,EStop> = 0;
		β Chain()Ι->State = 0;//where the owner's state stands:  a run's Chain, and Scanned's.
		β Sealed()ι->void = 0;//the records since the last Sealed or Dropped are proved.
		β Dropped()ι->void = 0;//and aren't.
		β Consumes()Ι->bool = 0;//whether it wants the records at all:  an archive is read through only for one that does.
	};
	//The owner's records parsed once, held until their checkpoint proves them, then handed to sealed in file order.
	template<Record T> struct Decoder final : IDecoder{
		Decoder( std::function<void( T& )> sealed={} )ι:_sealed{ move(sealed) }{}
		α Decode( const Raw& raw )ι->std::expected<optional<Time>,EStop> override{
			T r;
			if( const auto stop = Parse(raw, r); stop )
				return std::unexpected{ *stop };
			Traits<T>::ToMemory( r, _state );
			auto t = Traits<T>::Time( r );
			if( _sealed )
				_pending.push_back( move(r) );
			return t;
		}
		α Chain()Ι->State override{ return _state; }
		α Sealed()ι->void override{
			for( auto& r : _pending )
				_sealed( r );
			_pending.clear();
		}
		α Dropped()ι->void override{ _pending.clear(); }
		α Consumes()Ι->bool override{ return _sealed!=nullptr; }
	private:
		std::function<void( T& )> _sealed;
		vector<T> _pending;
		State _state{};
	};

	template<Record T> Reader<T>::Reader( Run run, uint windowBytes )ι:
		_run{ run },
		_windowBytes{ windowBytes },
		_base{ run.Offset },
		_offset{ run.Offset },
		_state{ run.Chain }{
		if( run.End>run.Offset )
			_wants = Window{ run.Offset, std::min<uint>(windowBytes, run.End-run.Offset) };
	}
	template<Record T> α Reader<T>::Feed( string&& bytes )ι->void{
		if( !_wants )
			return;
		if( bytes.size()<_wants->Size )
			_stop = EStop::Short;
		else
			_buffer.append( bytes.data(), _wants->Size );
		_wants.reset();
	}
	template<Record T> α Reader<T>::Next( T& r )ι->ENext{
		if( _stop )
			return ENext::End;
		if( _wants )
			return ENext::More;
		if( _offset>=_run.End ){
			_stop = EStop::End;
			return ENext::End;
		}
		Cursor cursor{ sv{_buffer}.substr(_offset-_base), _offset };
		if( auto raw = cursor.Next(); raw ){
			_field = raw->Field;
			if( const auto stop = Parse(*raw, r); stop ){
				_stop = *stop;
				return ENext::End;
			}
			Traits<T>::ToMemory( r, _state );
			_offset += raw->Frame.size();
			if( _offset-_base>_windowBytes ){//what is read is let go, a window at a time.
				_buffer.erase( 0, _offset-_base );
				_base = _offset;
			}
			return ENext::Record;
		}
		const auto stop = *cursor.Stop();
		const auto have = _base+_buffer.size();//through where the bytes reach.
		//More of the run to read:  the next window, or what completes a frame cut short, when the run can hold it.
		if( have<_run.End && (stop==EStop::End || (stop==EStop::Length && _offset+cursor.Needed()<=_run.End)) ){
			const auto needed = stop==EStop::End ? 1u : _offset+cursor.Needed()-have;
			_wants = Window{ have, std::min<uint>(std::max(_windowBytes, needed), _run.End-have) };
			return ENext::More;
		}
		_stop = stop;
		return ENext::End;
	}
}