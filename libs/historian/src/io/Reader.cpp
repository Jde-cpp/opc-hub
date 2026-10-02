#include "Reader.h"
#include <fstream>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/crc.h>

#define let const auto

namespace Jde::Opc{
	constexpr ELogTags _tags{ ELogTags::IO };
	using google::protobuf::io::CodedInputStream;
	using google::protobuf::io::CodedOutputStream;

	α Hist::ToString( EStop stop )ι->sv{
		switch( stop ){
		case EStop::End: return "the end";
		case EStop::Length: return "a length that runs past the end";
		case EStop::Body: return "a body that doesn't parse";
		case EStop::Empty: return "an empty record";
		case EStop::NoFileStart: return "a first record that isn't a FileStart";
		case EStop::Crc: return "a checkpoint whose CRC doesn't match";
		}
		return "unknown";
	}
}
namespace Jde::Opc::Hist{
	Reader::Reader( google::protobuf::io::ZeroCopyInputStream& in, uint start, uint end, Ticks chain )ι:
		_in{ &in, (int64_t)(end-start) },
		_offset{ start },
		_end{ end },
		_chain{ chain }
	{}

	α Reader::Next( Proto::HistoryRecord& r )ι->bool{
		if( !_stop )
			_stop = Read( r );
		return !_stop;
	}

	//A CodedInputStream per record, as protobuf's own delimited reader does:  its position is an int, which a whole file
	//could overflow.  Its destructor backs _in up to the record's end.
	α Reader::Read( Proto::HistoryRecord& r )ι->optional<EStop>{
		if( _offset>=_end )
			return EStop::End;
		CodedInputStream coded{ &_in };
		uint32_t size;
		if( !coded.ReadVarint32(&size) )
			return EStop::Length;
		let prefix = (uint)coded.CurrentPosition();
		//a padded varint isn't one the historian wrote, and would make Bytes() differ from the file's.
		if( prefix!=CodedOutputStream::VarintSize32(size) || size>_end-_offset-prefix || size>(uint32_t)std::numeric_limits<int>::max() )
			return EStop::Length;
		_bytes.resize( prefix+size );
		(void)CodedOutputStream::WriteVarint32ToArray( size, reinterpret_cast<uint8_t*>(_bytes.data()) );
		if( !coded.ReadRaw(_bytes.data()+prefix, (int)size) )
			return EStop::Length;
		if( !r.ParseFromArray(_bytes.data()+prefix, (int)size) )
			return EStop::Body;
		if( r.record_case()==Proto::HistoryRecord::RECORD_NOT_SET )
			return EStop::Empty;
		ToMemory( r, _chain );
		_offset += prefix+size;
		return nullopt;
	}

	Ω scan( std::istream& file, uint size, Scanned& y )ι->uint{
		google::protobuf::io::IstreamInputStream in{ &file };
		Reader reader{ in, 0, size, 0 };
		Proto::HistoryRecord r;
		optional<Proto::FileStart> start;
		uint32_t crc{};
		Run run{};
		bool timed{};
		for( ;; ){
			let at = reader.Offset();
			if( !reader.Next(r) ){
				y.Stop = *reader.Stop();
				return at;
			}
			if( !start ){
				if( !r.has_file_start() ){
					y.Stop = EStop::NoFileStart;
					return at;
				}
				start = r.file_start();
				if( start->generation() ){
					y = { .Start=move(start), .Size=size };
					return size;
				}
			}
			if( r.has_checkpoint() ){
				if( r.checkpoint().crc()!=crc ){
					y.Stop = EStop::Crc;
					return at;
				}
				y.Start = start;
				y.Size = reader.Offset();
				y.Chain = reader.Chain();
				if( timed )
					y.Runs.push_back( {run.Offset, y.Size, run.Chain, run.First, run.Last} );
				run = { .Offset=y.Size, .Chain=y.Chain };
				timed = false;
				crc = 0;
				continue;
			}
			crc = IO::Crc::Extend32c( crc, reader.Bytes() );
			if( let t = PrimaryTime(r); t ){
				run.First = timed ? std::min( run.First, *t ) : *t;
				run.Last = timed ? std::max( run.Last, *t ) : *t;
				timed = true;
			}
		}
	}

	α Scan( const fs::path& path, SL sl )ε->Scanned{
		std::error_code ec;
		let size = fs::file_size( path, ec );
		if( ec )
			throw IO::IOException{ path, (uint32)ec.value(), ec.message(), sl };
		Scanned y;
		uint stoppedAt;
		{
			std::ifstream file{ path, std::ios::binary };
			if( !file )
				throw IO::IOException{ path, "could not be opened to scan", sl };
			stoppedAt = scan( file, size, y );
		}
		if( y.Size<size ){
			WARN( "Truncating '{}' from {} bytes to {}, its last good checkpoint:  {} at byte {}.", path.string(), size, y.Size, ToString(y.Stop), stoppedAt );
			fs::resize_file( path, y.Size, ec );
			if( ec )
				throw IO::IOException{ path, (uint32)ec.value(), ec.message(), sl };
		}
		return y;
	}
}