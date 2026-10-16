#pragma once
#include <jde/fwk/exceptions/IOException.h>
DISABLE_WARNINGS
#include <jde/fwk/proto/Jde.IO.Records.pb.h>
ENABLE_WARNINGS

#define Φ Γ auto
//The record layer:  files of protobuf records with CRC-32C checkpoints, as the historian writes them (libs/historian/docs/spec.md
//*Record format*).  A record on disk is one member of the owner's record oneof, framed as its field - tag, length, body -
//so a file parses as one message of the owner's type.  Byte-level work is here and in Scan.h; what touches the owner's
//message type is a template over it, with Traits.h's specialization.
namespace Jde::IO::Records{
	using Time = int64_t;//the owner's ordering key:  UA ticks for the historian, ns since the epoch for the log.
	using State = int64_t;//the owner's decode state at a file position - the historian's delta chain; 0 for an owner with none.
	//An append to a live file, in time order and ending at a checkpoint.  A read merges the runs that overlap its range,
	//and the rewrite merges them all.
	struct Run final{
		uint Offset;
		uint End;//past its checkpoint.
		State Chain;//where the owner's state stands at Offset.
		Time First;//its times.
		Time Last;
	};
	struct Window final{ uint Offset; uint Size; };//bytes of a file a reader wants next:  the caller fetches them and feeds them back.
	enum class ENext : uint8{ Record, More, End };//More:  Wants() says what to Feed before the next call.  End:  Stop() says why.
	//Why reading stopped, as ToString says.
	enum class EStop : uint8{
		End,
		Length,//a frame the end of the bytes cuts short:  torn.
		BadLength,//a varint the layer never writes - padded, over 5 bytes, or a length past protobuf's limit:  not torn.
		Tag,//a field that isn't length-delimited, or field 0:  nothing the layer writes.
		Body,//a frame that doesn't parse as the owner's record.
		Empty,//a zero byte where a tag should be:  what a zero-filled tail reads as.
		Unknown,//a whole frame of a field this build doesn't know:  a newer build's, or damage that parses.
		Short,//the caller fed fewer bytes than were wanted:  the file no longer holds them.
		//The scan's alone.
		NoFileStart,
		Crc,
		SecondStart
	};
	Φ ToString( EStop stop )ι->sv;

	constexpr uint WindowBytes{ 1<<20 };//what a scan reads at a time.
	constexpr uint MergeWindowBytes{ 64<<10 };//what a read of a run reads at a time:  one window for a small run.
	constexpr uint PartBytes{ 1<<20 };//what a rewrite writes at a time.
	//The widest FileStart body, every field at its widest:  a first frame past it is no torn FileStart.
	constexpr uint32_t MaxStartBody{ 22 };
	constexpr uint MaxStartFrame{ 2+MaxStartBody };

	//The FileStart's crc:  the CRC-32C of ts and generation, each little-endian.
	Φ StartCrc( const Proto::FileStart& start )ι->uint32_t;
	//Onto out as field's frame, every field written and crc set, so the frame is MaxStartFrame bytes at most.
	Φ EncodeStart( string& out, uint32_t field, Proto::FileStart start )ι->void;
	//A FileStart from its body, none unless its crc matches.
	Φ DecodeStart( sv body )ι->optional<Proto::FileStart>;
	constexpr uint CheckpointBytes{ 7 };//a checkpoint's frame:  tag, length 5, the crc's tag, the crc.
	//Onto out as field's frame, CheckpointBytes long whatever crc is.
	Φ EncodeCheckpoint( string& out, uint32_t field, uint32_t crc )ι->void;
	//The crc a checkpoint's body holds, none for a body that isn't one.
	Φ DecodeCheckpoint( sv body )ι->optional<uint32_t>;
	//A checkpoint frame's first three bytes:  field's tag, 05, 0D.  A scan past damage finds one by them.
	Φ CheckpointMark( uint32_t field )ι->array<char,3>;

	//One frame as the file holds it.
	struct Raw final{
		uint32_t Field;
		sv Frame;//tag through body:  what the owner parses.
		sv Body;
		uint Offset;//the frame's, in the file.
	};
	//Frames over bytes, which start at file offset base.  Next is none at the end of the bytes, at a frame they cut short
	//(Stop Length, Needed how many bytes from Offset at least complete it) or at a stop.
	struct Γ Cursor final{
		Cursor( sv bytes, uint base )ι:_bytes{ bytes }, _base{ base }{}
		α Next()ι->optional<Raw>;
		α Stop()Ι->optional<EStop>{ return _stop; }
		α Offset()Ι->uint{ return _base+_pos; }//where the next frame starts, or where reading stopped.
		α Needed()Ι->uint{ return _needed; }
	private:
		sv _bytes;
		uint _base;
		uint _pos{};
		uint _needed{};
		optional<EStop> _stop;
	};
}
#undef Φ