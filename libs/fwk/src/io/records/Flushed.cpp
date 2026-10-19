#include <jde/fwk/io/records/Flushed.h>
#include <jde/fwk/io/crc.h>
#include <jde/fwk/io/records/Scan.h>
DISABLE_WARNINGS
#include <google/protobuf/io/coded_stream.h>
ENABLE_WARNINGS

#define let const auto
namespace Jde::IO::Records{
	constexpr ELogTags _tags{ ELogTags::IO };
	using google::protobuf::io::CodedInputStream;
	using google::protobuf::io::CodedOutputStream;

	namespace{
		constexpr uint Covered{ 16 };//the time, the sequence and the day, which the crc is of.
		static_assert( Crc::Calc32c(sv{"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", Covered})!=0, "A zero-filled slot must not read as a flush." );

		struct Stored{ int64_t Time; uint32_t Sequence; Day Recover; };
		Ω parse( sv bytes )ι->optional<Stored>{
			if( bytes.size()<Flushed::SlotSize )
				return nullopt;
			auto p = reinterpret_cast<const uint8_t*>( bytes.data() );
			uint64_t time;
			uint32_t sequence, day, crc;
			p = CodedInputStream::ReadLittleEndian64FromArray( p, &time );
			p = CodedInputStream::ReadLittleEndian32FromArray( p, &sequence );
			p = CodedInputStream::ReadLittleEndian32FromArray( p, &day );
			(void)CodedInputStream::ReadLittleEndian32FromArray( p, &crc );
			if( crc!=Crc::Calc32c(bytes.substr(0, Covered)) )
				return nullopt;
			return Stored{ (int64_t)time, sequence, Day{std::chrono::sys_days{std::chrono::days{(int32_t)day}}} };
		}
	}

	Flushed::Flushed( fs::path file, optional<string> bytes )ι:
		_file{ move(file) }{
		if( !bytes )
			return;
		let read = sv{ *bytes }.substr( 0, 2*SlotSize );
		const array<optional<Stored>,2> slots{ parse(read), parse(read.substr(std::min<uint>(SlotSize, read.size()))) };
		//The sequence wraps, so the later of two is the one ahead by less than half its range.
		let second = slots[1] && ( !slots[0] || (int32_t)(slots[1]->Sequence-slots[0]->Sequence)>0 );
		if( let& slot = slots[second]; slot ){
			_time = slot->Time;
			_recover = slot->Recover;
			_sequence = slot->Sequence;
			_slot = second;
		}
		else
			WARN( "'{}' holds no valid slot, so its owner is taken as never flushed.", _file.string() );
	}
	α Flushed::Load( fs::path file, SL sl )ε->Flushed{
		string bytes;
		try{
			bytes = FetchNow( ms<OpenFile>(file, sl), Window{0, 2*SlotSize}, sl );
		}
		catch( IOException& e ){
			if( e.Error==EIOError::NotFound )
				return Flushed{ move(file), nullopt };
			e.SetLevel( ELogLevel::Error );//its open or its read.
			throw;
		}
		return Flushed{ move(file), move(bytes) };
	}

	α Flushed::Next( int64_t time, Day recover )Ι->Slot{
		if( _time )
			time = std::max( time, *_time );//a clock set back claims less than the last flush did, which still holds.
		Slot y{ .File=_file, .Index=(uint8)(1-_slot), .Sequence=_sequence+1, .Time=time, .Recover=recover, .Bytes=string(SlotSize, '\0') };
		auto p = CodedOutputStream::WriteLittleEndian64ToArray( (uint64_t)time, reinterpret_cast<uint8_t*>(y.Bytes.data()) );
		p = CodedOutputStream::WriteLittleEndian32ToArray( y.Sequence, p );
		p = CodedOutputStream::WriteLittleEndian32ToArray( (uint32_t)(int32_t)std::chrono::sys_days{recover}.time_since_epoch().count(), p );
		(void)CodedOutputStream::WriteLittleEndian32ToArray( Crc::Calc32c(sv{y.Bytes}.substr(0, Covered)), p );
		return y;
	}
	α Flushed::Slot::Write( SL sl )Ι->WriteAwait{
		return WriteAwait{ File, Bytes, WriteOptions{.Create=true, .Mode=EWriteMode::At, .Offset=Index*SlotSize, .Sync=true}, sl };
	}
	α Flushed::Wrote( const Slot& slot )ι->void{
		_time = slot.Time;
		_recover = slot.Recover;
		_sequence = slot.Sequence;
		_slot = slot.Index;
	}
}