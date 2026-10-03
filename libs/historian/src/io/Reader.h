#pragma once
#include "Records.h"
DISABLE_WARNINGS
#include <google/protobuf/io/zero_copy_stream_impl.h>
ENABLE_WARNINGS

namespace Jde::Opc::Hist{
	//Why reading stopped, as ToString says.
	enum class EStop : uint8{
		End,
		Length,
		BadLength,//whole but padded, or past protobuf's limits, or a first that no FileStart fills:  not torn.
		Body,		//also one that isn't a single field filling it, which is how a garbled length shows.
		Empty,	//a record of 0 bytes, which the historian never writes:  what a zero-filled tail reads as.
		Unknown,	//a whole one with no member this build knows:  a newer build's, or damage that parses.
		//The scan's alone.
		NoFileStart,
		Crc,
		SecondStart
	};
	α ToString( EStop stop )ι->sv;

	//A file's records from byte offset start to end, through a CodedInputStream.  in is positioned at start, where the
	//delta chain stands at chain.  Checkpoints are returned with the rest:  the scan checks them, and a read skips them.
	//A read parses each body straight from the stream; the scan, with keepBytes, copies it first, for Bytes().
	struct Reader final{
		Reader( google::protobuf::io::ZeroCopyInputStream& in, uint start, uint end, Ticks chain, bool keepBytes=false )ι;
		//The next record with its times absolute.  false at the end, or at a record that can't be read whole, and after.
		α Next( Proto::HistoryRecord& r )ι->bool;
		α Offset()Ι->uint{ return _offset; }//where the next record starts, or where reading stopped.
		α Chain()Ι->Ticks{ return _chain; }
		α Stop()Ι->optional<EStop>{ return _stop; }
		α Bytes()Ι->sv{ return _bytes; }//with keepBytes, the last record as it was read:  its length, then its body.
	private:
		α Read( Proto::HistoryRecord& r )ι->optional<EStop>;//none when it read one.
		google::protobuf::io::LimitingInputStream _in;
		uint _offset;
		const uint _end;
		Ticks _chain;
		const bool _keepBytes;
		string _bytes;
		optional<EStop> _stop;
	};

	//An append to a live file, sorted by primary time and ending at a checkpoint.  A read merges the runs that overlap its
	//range, and the midnight rewrite merges them all.
	struct Run final{
		uint Offset;
		uint End;//past its checkpoint.
		Ticks Chain;//where the delta chain stands at Offset.
		Ticks First;//its primary times.
		Ticks Last;
	};
	//What the first-open scan keeps of a file.
	struct Scanned final{
		//None when nothing was kept.  Only once Truncate succeeds is the file then empty, needing its preamble again:  one
		//that Keep() holds onto, foreign or with a newer build's records, takes no preamble.
		optional<Proto::FileStart> Start;
		uint Size{};//through the last good checkpoint.
		uint FileSize{};//what the file holds:  past Size, a tail that a read ignores and Truncate drops, unless Keep().
		Ticks Chain{};//where the delta chain resumes:  the last primary time kept.
		vector<Run> Runs;//each append holding a record with a time.
		EStop Stop{ EStop::End };
		uint StopOffset{};//where the scan stopped.
		//Past a stop short of the file's end, where an append a historian sealed ends.  A torn flush is only ever the last
		//append, so this makes the stop damage.
		optional<uint> SealedAfter;
		//Whether what lies past Size must be kept:  not a torn flush, but damage, a newer build's records or a second
		//FileStart before an append a historian sealed (SealedAfter), or the start of a file that isn't a live one.  Truncate
		//refuses it, and so must any writer that would drop it, such as the midnight rewrite from Runs.
		α Keep()Ι->bool;
	};
	//The first time the process opens a live or modifications file, to read it or to append:  scans it from the start and
	//keeps it through the last checkpoint whose CRC matches.  It never changes the file.  An archive, a FileStart
	//generation past 0, is only ever replaced whole and carries no checkpoints, so all of it is kept, with no runs or
	//chain.  Throws when the file can't be read through, which is no torn append.  Logs once when it keeps less than the
	//whole file:  at Warning for a torn flush, at Error for what must be kept.  The size is the open file's, so a
	//rename over file between its open and the scan can't pair one file's size with another's bytes.
	α Scan( const fs::path& file, SRCE )ε->Scanned;
	//Before the first append to a scanned file:  truncates it to y.Size, so a torn append is dropped whole.  A first
	//record the end of the file cuts short, or a zero byte, is a torn preamble and is dropped too.  Throws, leaving the
	//file as it is, when y.Keep(), or when there is a tail to cut and the file no longer holds y.FileSize bytes:  y is
	//then another file's scan, or this one's from before it grew, and the cut would drop bytes it never read.
	α Truncate( const fs::path& file, Scanned& y, SRCE )ε->void;
}