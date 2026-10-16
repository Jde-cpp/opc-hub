#include <jde/fwk/io/records/Framing.h>
#include <jde/fwk/io/crc.h>
DISABLE_WARNINGS
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/wire_format_lite.h>
ENABLE_WARNINGS

#define let const auto
namespace Jde::IO{
	using google::protobuf::io::CodedInputStream;
	using google::protobuf::io::CodedOutputStream;
	using google::protobuf::internal::WireFormatLite;

	α Records::ToString( EStop stop )ι->sv{
		switch( stop ){
		case EStop::End: return "the end";
		case EStop::Length: return "a frame that runs past the end";
		case EStop::BadLength: return "a varint the record layer never writes";
		case EStop::Tag: return "a tag that isn't a length-delimited field";
		case EStop::Body: return "a frame that doesn't parse as a record";
		case EStop::Empty: return "a zero byte";
		case EStop::Unknown: return "a record this build doesn't know";
		case EStop::Short: return "a read that came back short";
		case EStop::NoFileStart: return "a first record that isn't a FileStart with a good CRC";
		case EStop::Crc: return "a checkpoint whose CRC doesn't match";
		case EStop::SecondStart: return "a second FileStart";
		}
		return "unknown";
	}

	namespace{
		//body onto out as field's frame:  the tag, the length, the body.
		Ω frame( string& out, uint32_t field, sv body )ι->void{
			let tag = WireFormatLite::MakeTag( (int)field, WireFormatLite::WIRETYPE_LENGTH_DELIMITED );
			let offset = out.size();
			out.resize( offset+CodedOutputStream::VarintSize32(tag)+CodedOutputStream::VarintSize32((uint32_t)body.size())+body.size() );
			auto p = reinterpret_cast<uint8_t*>( out.data()+offset );
			p = CodedOutputStream::WriteVarint32ToArray( tag, p );
			p = CodedOutputStream::WriteVarint32ToArray( (uint32_t)body.size(), p );
			memcpy( p, body.data(), body.size() );
		}
		constexpr uint8_t CrcTag{ 0x0D };//Checkpoint.crc, fixed32.
	}

	α Records::StartCrc( const Proto::FileStart& start )ι->uint32_t{
		uint8_t bytes[12];
		auto p = CodedOutputStream::WriteLittleEndian64ToArray( (uint64_t)start.ts(), bytes );
		(void)CodedOutputStream::WriteLittleEndian32ToArray( start.generation(), p );
		return Crc::Calc32c( sv{reinterpret_cast<const char*>(bytes), sizeof(bytes)} );
	}
	α Records::EncodeStart( string& out, uint32_t field, Proto::FileStart start )ι->void{
		start.set_crc( StartCrc(start) );
		uint8_t body[MaxStartBody];
		auto p = CodedOutputStream::WriteTagToArray( WireFormatLite::MakeTag(1, WireFormatLite::WIRETYPE_VARINT), body );
		p = CodedOutputStream::WriteVarint64ToArray( (uint64_t)start.ts(), p );
		p = CodedOutputStream::WriteTagToArray( WireFormatLite::MakeTag(2, WireFormatLite::WIRETYPE_VARINT), p );
		p = CodedOutputStream::WriteVarint32ToArray( start.generation(), p );
		p = CodedOutputStream::WriteTagToArray( WireFormatLite::MakeTag(3, WireFormatLite::WIRETYPE_FIXED32), p );
		p = CodedOutputStream::WriteLittleEndian32ToArray( start.crc(), p );
		frame( out, field, sv{reinterpret_cast<const char*>(body), (uint)(p-body)} );
	}
	α Records::DecodeStart( sv body )ι->optional<Proto::FileStart>{
		Proto::FileStart y;
		if( !y.ParseFromArray(body.data(), (int)body.size()) || y.crc()!=StartCrc(y) )
			return nullopt;
		return y;
	}
	α Records::EncodeCheckpoint( string& out, uint32_t field, uint32_t crc )ι->void{
		uint8_t body[5]{ CrcTag };
		(void)CodedOutputStream::WriteLittleEndian32ToArray( crc, body+1 );
		frame( out, field, sv{reinterpret_cast<const char*>(body), sizeof(body)} );
	}
	α Records::DecodeCheckpoint( sv body )ι->optional<uint32_t>{
		if( body.size()!=5 || (uint8_t)body[0]!=CrcTag )//only ever the 5 bytes EncodeCheckpoint writes.
			return nullopt;
		uint32_t crc;
		(void)CodedInputStream::ReadLittleEndian32FromArray( reinterpret_cast<const uint8_t*>(body.data()+1), &crc );
		return crc;
	}
	α Records::CheckpointMark( uint32_t field )ι->array<char,3>{
		return { (char)WireFormatLite::MakeTag((int)field, WireFormatLite::WIRETYPE_LENGTH_DELIMITED), 0x05, (char)CrcTag };
	}

	α Records::Cursor::Next()ι->optional<Raw>{
		if( _stop )
			return nullopt;
		let remaining = _bytes.size()-_pos;
		if( !remaining ){
			_stop = EStop::End;
			return nullopt;
		}
		if( !_bytes[_pos] ){
			_stop = EStop::Empty;
			return nullopt;
		}
		//A varint the bytes end inside is torn, needing at least one more; one they hold the widest form of is bad.
		let cut = [&]( uint width ){
			if( remaining>=width )
				_stop = EStop::BadLength;
			else{
				_stop = EStop::Length;
				_needed = remaining+1;
			}
		};
		CodedInputStream in{ reinterpret_cast<const uint8_t*>(_bytes.data()+_pos), (int)std::min<uint>(remaining, std::numeric_limits<int>::max()) };
		uint32_t tag;
		if( !in.ReadVarint32(&tag) ){
			cut( 5 );
			return nullopt;
		}
		let tagSize = (uint)in.CurrentPosition();
		if( tagSize!=CodedOutputStream::VarintSize32(tag) ){//padded:  nothing the layer writes.
			_stop = EStop::BadLength;
			return nullopt;
		}
		let field = WireFormatLite::GetTagFieldNumber( tag );
		if( !field || WireFormatLite::GetTagWireType(tag)!=WireFormatLite::WIRETYPE_LENGTH_DELIMITED ){
			_stop = EStop::Tag;
			return nullopt;
		}
		uint32_t size;
		if( !in.ReadVarint32(&size) ){
			cut( tagSize+5 );
			return nullopt;
		}
		let prefix = (uint)in.CurrentPosition();
		if( prefix-tagSize!=CodedOutputStream::VarintSize32(size) || size>(uint32_t)std::numeric_limits<int>::max() ){
			_stop = EStop::BadLength;
			return nullopt;
		}
		let frameSize = prefix+size;
		if( frameSize>remaining ){
			_stop = EStop::Length;
			_needed = frameSize;
			return nullopt;
		}
		Raw y{ (uint32_t)field, _bytes.substr(_pos, frameSize), _bytes.substr(_pos+prefix, size), _base+_pos };
		_pos += frameSize;
		return y;
	}
}