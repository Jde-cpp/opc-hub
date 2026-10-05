#include "Reader.h"
#include "File.h"
#include <fstream>
#include <jde/fwk/exceptions/IOException.h>
#include <jde/fwk/io/crc.h>
DISABLE_WARNINGS
#include <google/protobuf/wire_format_lite.h>
ENABLE_WARNINGS

#define let const auto

namespace Jde::Opc{
	constexpr ELogTags _tags{ ELogTags::IO };
	using google::protobuf::io::CodedInputStream;
	using google::protobuf::io::CodedOutputStream;

	α Hist::ToString( EStop stop )ι->sv{
		switch( stop ){
		case EStop::End: return "the end";
		case EStop::Length: return "a length that runs past the end";
		case EStop::BadLength: return "a length the historian never writes";
		case EStop::Body: return "a body that doesn't parse as a record";
		case EStop::Empty: return "a zero byte";
		case EStop::Unknown: return "a record this build doesn't know";
		case EStop::NoFileStart: return "a first record that isn't a FileStart with a good CRC";
		case EStop::Crc: return "a checkpoint whose CRC doesn't match";
		case EStop::SecondStart: return "a second FileStart";
		}
		return "unknown";
	}
}
namespace Jde::Opc::Hist{
	Reader::Reader( google::protobuf::io::ZeroCopyInputStream& in, uint start, uint end, Ticks chain, bool keepBytes )ι:
		_in{ &in, (int64_t)(end-start) },
		_offset{ start },
		_end{ end },
		_chain{ chain },
		_keepBytes{ keepBytes }
	{}

	α Reader::Next( Proto::HistoryRecord& r )ι->bool{
		if( !_stop )
			_stop = Read( r );
		return !_stop;
	}

	//Whether head, the first bytes of a body of size, starts one field that fills it:  the record's oneof member.  A garbled
	//length almost never agrees with it.
	Ω fills( sv head, uint32_t size )ι->bool{
		using google::protobuf::internal::WireFormatLite;
		CodedInputStream in{ reinterpret_cast<const uint8_t*>(head.data()), (int)head.size() };
		let tag = in.ReadTag();
		if( !WireFormatLite::GetTagFieldNumber(tag) )
			return false;
		uint64_t rest{};//past what in reads.
		switch( WireFormatLite::GetTagWireType(tag) ){
		case WireFormatLite::WIRETYPE_VARINT:{
			uint64_t value;
			if( !in.ReadVarint64(&value) )
				return false;
			break;}
		case WireFormatLite::WIRETYPE_FIXED64: rest = 8; break;
		case WireFormatLite::WIRETYPE_FIXED32: rest = 4; break;
		case WireFormatLite::WIRETYPE_LENGTH_DELIMITED:{
			uint32_t length;
			if( !in.ReadVarint32(&length) )
				return false;
			rest = length;
			break;}
		default:
			return false;
		}
		return (uint64_t)in.CurrentPosition()+rest==size;
	}

	//A CodedInputStream per record, as protobuf's own delimited reader does:  its position is an int, which a whole file
	//could overflow.  Its destructor backs _in up to the record's end.
	α Reader::Read( Proto::HistoryRecord& r )ι->optional<EStop>{
		if( _offset>=_end )
			return EStop::End;
		CodedInputStream coded{ &_in };
		uint32_t size;
		//Only a length the end of the range cuts short can be torn:  one whole but longer than protobuf's 10 byte varints
		//isn't, and neither is a file's first that a FileStart, at most MaxFileStartBody, can't fill.
		if( !coded.ReadVarint32(&size) )
			return _end-_offset>=10 || !_offset ? EStop::BadLength : EStop::Length;
		let prefix = (uint)coded.CurrentPosition();
		//a padded varint isn't one the historian wrote, and would make Bytes() differ from the file's.
		if( prefix!=CodedOutputStream::VarintSize32(size) || size>(uint32_t)std::numeric_limits<int>::max() )
			return EStop::BadLength;
		if( size>_end-_offset-prefix )
			return !_offset && size>MaxFileStartBody ? EStop::BadLength : EStop::Length;
		//The body's first bytes are checked before the rest is read, so a garbled length that fits in the file can't make
		//the reader buffer the rest of it.  16 bytes hold a tag and a length or a varint.
		char head[16];
		let headSize = std::min<uint>( size, sizeof(head) );
		const void* buffered;
		int available;
		//A read parses in place, once the head is in the stream's buffer:  one that straddles two of its blocks is copied.
		if( !_keepBytes && coded.GetDirectBufferPointer(&buffered, &available) && (uint)available>=headSize ){
			if( size && !fills({(const char*)buffered, headSize}, size) )
				return EStop::Body;
			let limit = coded.PushLimit( (int)size );
			let parsed = r.ParseFromCodedStream( &coded );
			coded.PopLimit( limit );
			if( !parsed )
				return EStop::Body;
		}
		else{
			if( !coded.ReadRaw(head, (int)headSize) )
				return EStop::Length;
			if( size && !fills({head, headSize}, size) )
				return EStop::Body;
			_bytes.resize( prefix+size );
			let p = CodedOutputStream::WriteVarint32ToArray( size, reinterpret_cast<uint8_t*>(_bytes.data()) );
			memcpy( p, head, headSize );
			if( !coded.ReadRaw(p+headSize, (int)(size-headSize)) )
				return EStop::Length;
			if( !r.ParseFromArray(_bytes.data()+prefix, (int)size) )
				return EStop::Body;
		}
		if( r.record_case()==Proto::HistoryRecord::RECORD_NOT_SET )
			return size ? EStop::Unknown : EStop::Empty;
		ToMemory( r, _chain );
		_offset += prefix+size;
		return nullopt;
	}

	Ω scan( std::istream& file, uint size, Scanned& y, const std::function<void( Proto::HistoryRecord& )>& sealed )ι->void{
		google::protobuf::io::IstreamInputStream in{ &file };
		Reader reader{ in, 0, size, 0, true };
		Proto::HistoryRecord r;
		optional<Proto::FileStart> start;
		uint32_t crc{};
		optional<Run> run;//the append's, once it holds a record with a time.
		vector<Proto::HistoryRecord> pending;//the append's records, for sealed once its checkpoint matches.
		//Every stop is just where the kept prefix ends:  whether what follows is torn or must be kept is Scan's
		//sealedAfter's to judge, and Keep's.
		for( ;; ){
			y.StopOffset = reader.Offset();
			if( !reader.Next(r) ){
				y.Stop = *reader.Stop();
				return;
			}
			if( !start ){
				if( !r.has_file_start() || r.file_start().crc()!=StartCrc(r.file_start()) ){
					y.Stop = EStop::NoFileStart;
					return;
				}
				start = r.file_start();
				if( start->generation() ){
					y = { .Start=move(start), .Size=size, .FileSize=size, .StopOffset=size };
					if( sealed ){//an archive has no checkpoints:  all of it is kept, so all of it is sealed's.
						for( sealed(r); reader.Next(r); )
							sealed( r );
						y.Stop = *reader.Stop();
						y.StopOffset = reader.Offset();
					}
					return;
				}
			}
			else if( r.has_file_start() ){//the historian writes one, so no append that holds another is one to build on.
				y.Stop = EStop::SecondStart;
				return;
			}
			if( r.has_checkpoint() ){
				if( r.checkpoint().crc()!=crc ){
					y.Stop = EStop::Crc;
					return;
				}
				if( !y.Start )
					y.Start = start;
				for( auto& record : pending )
					sealed( record );
				pending.clear();
				y.Size = reader.Offset();
				y.Chain = reader.Chain();
				if( run ){
					run->End = y.Size;
					y.Runs.push_back( *run );
					run.reset();
				}
				crc = 0;
				continue;
			}
			crc = IO::Crc::Extend32c( crc, reader.Bytes() );
			if( let t = PrimaryTime(r); t ){
				if( !run )
					run = Run{ .Offset=y.Size, .Chain=y.Chain, .First=*t, .Last=*t };//y holds where the append starts.
				run->First = std::min( run->First, *t );
				run->Last = std::max( run->Last, *t );
			}
			if( sealed )
				pending.push_back( move(r) );
		}
	}

	//Where the first append past from that a historian sealed ends:  a checkpoint, found by its bytes, whose CRC matches the
	//bytes since the checkpoint before it, or since from.  Found by bytes, not records, so damage that broke the framing
	//can't hide it, though one whose own checkpoint was damaged too can't be proved.  An empty span proves nothing.
	Ω sealedAfter( std::istream& file, uint from, uint size, const fs::path& path, SL sl )ε->optional<uint>{
		constexpr char mark[]{ 0x07, 0x32, 0x05, 0x0D };//a Checkpoint record's length, oneof tag, length and crc tag.
		constexpr uint checkpointSize{ sizeof(mark)+4 };
		file.clear();
		file.seekg( (std::streamoff)from );
		string window;
		uint base{ from };//the file offset of window's first byte.
		uint done{};//how far into window crc reaches.
		uint32_t crc{};
		bool spanned{};//whether the span crc covers holds a byte.
		let extend = [&]( uint end ){
			crc = IO::Crc::Extend32c( crc, sv{window}.substr(done, end-done) );
			spanned = spanned || end>done;
			done = end;
		};
		for( ;; ){
			let wanted = std::min<uint>( 64*1024, size-(base+window.size()) );
			if( wanted ){
				let had = window.size();
				window.resize( had+wanted );
				file.read( window.data()+had, (std::streamsize)wanted );
				window.resize( had+(uint)file.gcount() );
				if( window.size()<had+wanted )
					throw IO::IOException{ sl, path, ELogLevel::Error, "could not be read through to scan" };
			}
			for( auto p = window.find(sv{mark, sizeof(mark)}, done); p!=string::npos && p+checkpointSize<=window.size(); p = window.find(sv{mark, sizeof(mark)}, done) ){
				extend( (uint)p );
				uint32_t stored;
				(void)CodedInputStream::ReadLittleEndian32FromArray( reinterpret_cast<const uint8_t*>(window.data()+p+sizeof(mark)), &stored );
				if( spanned && stored==crc )
					return base+(uint)p+checkpointSize;
				crc = 0;
				spanned = false;
				done = (uint)p+checkpointSize;
			}
			if( !wanted )
				return nullopt;
			extend( std::max<uint>(done, window.size()>=checkpointSize ? (uint)window.size()-(checkpointSize-1) : 0) );//the rest could start a mark.
			base += done;
			window.erase( 0, done );
			done = 0;
		}
	}

	//What a scan that stopped short of the file's end found there, for the log and Truncate's refusal.
	Ω stopped( const Scanned& y )ι->string{
		let sealed = y.SealedAfter ? Ƒ( " since an append a historian sealed follows it, ending at byte {}", *y.SealedAfter ) : string{};
		return Ƒ( "{} at byte {}, which is {}{}", ToString(y.Stop), y.StopOffset, y.Keep() ? "no torn flush" : "a torn flush", sealed );
	}

	α Scan( const fs::path& path, const std::function<void( Proto::HistoryRecord& )>& sealed, SL sl )ε->Scanned{
		std::ifstream file{ path, std::ios::binary | std::ios::ate };
		if( !file )
			throw IO::IOException{ path, "could not be opened to scan", sl };
		let end = file.tellg();//the open file's size, which a rename over path can't pair with another file's bytes.
		if( end<0 || !file.seekg(0) )
			throw IO::IOException{ sl, path, ELogLevel::Error, "could not be sized to scan" };
		let size = (uint)end;
		Scanned y{ .FileSize=size };
		scan( file, size, y, sealed );
		//libc++ reports a failed read as the end of the file, which then comes short of the file's size.
		bool failed = file.bad();
		if( !failed && file.eof() ){
			file.clear();
			failed = (std::streamoff)file.tellg()<(std::streamoff)size;
		}
		if( failed )
			throw IO::IOException{ sl, path, ELogLevel::Error, "could not be read through to scan" };
		if( y.Start && y.Start->generation() && y.Stop!=EStop::End )//an archive is whole, so one sealed can't read through is damage.
			throw IO::IOException{ sl, path, ELogLevel::Error, "read {} at byte {}, short of the {} bytes its scan kept", ToString(y.Stop), y.StopOffset, y.Size };
		if( y.Size<y.FileSize )
			y.SealedAfter = sealedAfter( file, y.Size, size, path, sl );
		if( y.Size<y.FileSize )//once, here, so a file that is only read says what its reads leave out.
			LOG( y.Keep() ? ELogLevel::Error : ELogLevel::Warning, _tags, "'{}' holds {}:  reads serve only its first {} of {} bytes, and {}.", path.string(), stopped(y), y.Size, y.FileSize, y.Keep() ? "the historian won't append to it" : "its first append drops the rest" );
		return y;
	}

	α Scanned::Keep()Ι->bool{
		let foreign = !StopOffset && Stop!=EStop::Length && Stop!=EStop::Empty;//a first record no torn preamble leaves.
		return Size<FileSize && (SealedAfter || foreign);
	}

	α Truncate( const fs::path& path, Scanned& y, SL sl )ε->void{
		if( y.Keep() )
			throw IO::IOException{ sl, path, ELogLevel::Error, "holds {}:  it is left as it is", stopped(y) };
		if( y.Size==y.FileSize )
			return;
		std::error_code ec;
		let now = fs::file_size( path, ec );
		if( ec )
			throw Failed( path, ec, sl );
		if( now!=y.FileSize )
			throw IO::IOException{ sl, path, ELogLevel::Error, "holds {} bytes, not the {} it was scanned at, and is left as it is", now, y.FileSize };
		WARN( "Truncating '{}' from {} bytes to {}, its last good checkpoint:  {} at byte {}.", path.string(), y.FileSize, y.Size, ToString(y.Stop), y.StopOffset );
		fs::resize_file( path, y.Size, ec );
		if( ec )
			throw Failed( path, ec, sl );
		y.FileSize = y.Size;
	}
}