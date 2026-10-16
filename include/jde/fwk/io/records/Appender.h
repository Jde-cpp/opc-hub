#pragma once
#include <jde/fwk/io/crc.h>
#include "Traits.h"

namespace Jde::IO::Records{
	//One append to a file, a run:  each record serialized straight onto the end of out, with no copy of its own, then a
	//Checkpoint holding the CRC-32C of the run's bytes.
	template<Record T> struct Appender final{
		//chain is where the owner's state stands, from the file's first-open scan; a FileStart resets it.
		Appender( string& out, State chain )ι:_out{ out }, _start{ out.size() }, _chain{ chain }{}
		//A new file's FileStart, crc set and every field written, so its frame is MaxStartFrame bytes at most.
		α Start( Proto::FileStart start )ε->void{
			T r;
			Traits<T>::SetStart( r, start );
			auto chain = _chain;
			Traits<T>::ToDisk( r, chain );
			EncodeStart( _out, Traits<T>::StartField, move(start) );
			_chain = chain;
		}
		//Throws for a record over protobuf's limit, or with no member, or when out can't grow, leaving out and the chain as
		//they were, so the run can go on without r.
		α Add( T&& r )ε->void{
			auto chain = _chain;
			Traits<T>::ToDisk( r, chain );
			Write( r );
			_chain = chain;
		}
		//Ends the run, and returns where the owner's state stands for the file's next.  Throws only when out can't grow,
		//changing nothing.
		α Seal()ε->State{
			EncodeCheckpoint( _out, Traits<T>::CheckpointField, Crc::Calc32c(sv{_out}.substr(_start)) );
			_start = _out.size();
			return _chain;
		}
	private:
		α Write( const T& r )ε->void{
			const auto size = r.ByteSizeLong();
			THROW_IF( size>(size_t)std::numeric_limits<int>::max(), "A record of {} bytes is past protobuf's limit.", size );
			THROW_IF( !size, "A record with no member has no frame." );
			const auto offset = _out.size();
			_out.resize( offset+size );
			(void)r.SerializeWithCachedSizesToArray( reinterpret_cast<uint8_t*>(_out.data()+offset) );
		}
		string& _out;
		uint _start;//where the run begins in _out.
		State _chain;
	};
}