#include "Flushed.h"
#include <fstream>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/crc.h>
#include "File.h"
DISABLE_WARNINGS
#include <google/protobuf/io/coded_stream.h>
ENABLE_WARNINGS

#define let const auto

namespace Jde::Opc::Hist{
	constexpr ELogTags _tags{ ELogTags::IO };
	using google::protobuf::io::CodedInputStream;
	using google::protobuf::io::CodedOutputStream;

	namespace{
		constexpr uint Covered{ 12 };//the time and the sequence, which the crc is of.
		static_assert( IO::Crc::Calc32c(sv{"\0\0\0\0\0\0\0\0\0\0\0\0", Covered})!=0, "A zero-filled slot must not read as a flush." );

		struct Slot{ UA_DateTime Time; uint32_t Sequence; };
		Ω parse( sv bytes )ι->optional<Slot>{
			if( bytes.size()<Flushed::SlotSize )
				return nullopt;
			auto p = reinterpret_cast<const uint8_t*>( bytes.data() );
			uint64_t time;
			uint32_t sequence, crc;
			p = CodedInputStream::ReadLittleEndian64FromArray( p, &time );
			p = CodedInputStream::ReadLittleEndian32FromArray( p, &sequence );
			(void)CodedInputStream::ReadLittleEndian32FromArray( p, &crc );
			return crc==IO::Crc::Calc32c( bytes.substr(0, Covered) ) ? optional<Slot>{ Slot{(UA_DateTime)time, sequence} } : nullopt;
		}
	}

	Flushed::Flushed( fs::path file, SL sl )ε:
		_file{ move(file) }{
		std::ifstream in{ _file, std::ios::binary };
		if( !in ){
			std::error_code ec;
			if( fs::exists(_file, ec) || ec )
				throw IO::IOException{ sl, _file, ELogLevel::Error, "could not be opened to read the last flush" };
			return;
		}
		char bytes[2*SlotSize];
		in.read( bytes, sizeof(bytes) );
		if( in.bad() )
			throw IO::IOException{ sl, _file, ELogLevel::Error, "could not be read for the last flush" };
		let read = sv{ bytes, (uint)in.gcount() };
		const array<optional<Slot>,2> slots{ parse(read), parse(read.substr(std::min<uint>(SlotSize, read.size()))) };
		//The sequence wraps, so the later of two is the one ahead by less than half its range.
		let second = slots[1] && ( !slots[0] || (int32_t)(slots[1]->Sequence-slots[0]->Sequence)>0 );
		if( let& slot = slots[second]; slot ){
			_time = UADateTime{ slot->Time }.Time();
			_sequence = slot->Sequence;
			_slot = second;
		}
		else
			WARN( "'{}' holds no valid slot, so its group is taken as never flushed.", _file.string() );
	}

	α Flushed::Write( TimePoint time, SL sl )ε->void{
		let sequence = _sequence+1;
		const uint8 slot = 1-_slot;
		char bytes[SlotSize];
		auto p = CodedOutputStream::WriteLittleEndian64ToArray( (uint64_t)UADateTime{time}.UA(), reinterpret_cast<uint8_t*>(bytes) );
		p = CodedOutputStream::WriteLittleEndian32ToArray( sequence, p );
		(void)CodedOutputStream::WriteLittleEndian32ToArray( IO::Crc::Calc32c(sv{bytes, Covered}), p );
		auto file = File::Open( _file, sl );
		file.Write( slot*SlotSize, sv{bytes, sizeof(bytes)}, sl );
		file.Sync( sl );
		if( file.Created() )
			SyncDirectories( _file.parent_path(), {}, sl );
		_time = time;
		_sequence = sequence;
		_slot = slot;
	}
}
