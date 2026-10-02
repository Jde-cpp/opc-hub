#pragma once
#include "Records.h"
DISABLE_WARNINGS
#include <google/protobuf/io/zero_copy_stream_impl.h>
ENABLE_WARNINGS

namespace Jde::Opc::Hist{
	//Why reading stopped.
	enum class EStop : uint8{
		End,		//the end, after a whole record.
		Length,	//a length that can't be read or runs past the end.
		Body,		//a body that doesn't parse.
		Empty,	//a record with no member set, which the historian never writes:  what a zero byte reads as.
		NoFileStart,//the scan's:  a first record that isn't a FileStart.
		Crc			//the scan's:  a checkpoint whose CRC doesn't match the bytes before it.
	};
	α ToString( EStop stop )ι->sv;

	//A file's records from byte offset start to end, through a CodedInputStream.  in is positioned at start, where the
	//delta chain stands at chain.  Checkpoints are returned with the rest:  the scan checks them, and a read skips them.
	struct Reader final{
		Reader( google::protobuf::io::ZeroCopyInputStream& in, uint start, uint end, Ticks chain )ι;
		//The next record with its times absolute.  false at the end, or at a record that can't be read whole, and after.
		α Next( Proto::HistoryRecord& r )ι->bool;
		α Offset()Ι->uint{ return _offset; }//where the next record starts, or where reading stopped.
		α Chain()Ι->Ticks{ return _chain; }
		α Stop()Ι->optional<EStop>{ return _stop; }
		α Bytes()Ι->sv{ return _bytes; }//the last record as it was read:  its length, then its body.
	private:
		α Read( Proto::HistoryRecord& r )ι->optional<EStop>;//none when it read one.
		google::protobuf::io::LimitingInputStream _in;
		uint _offset;
		const uint _end;
		Ticks _chain;
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
		optional<Proto::FileStart> Start;//none when nothing was kept, so the file needs its preamble again.
		uint Size{};//through the last good checkpoint.
		Ticks Chain{};//where the delta chain resumes:  the last primary time kept.
		vector<Run> Runs;//each append holding a record with a time.
		EStop Stop{ EStop::End };
	};
	//The first time the process opens a live or modifications file:  scans it from the start and truncates it after the
	//last checkpoint whose CRC matches, so a torn append is dropped whole.  An archive, a FileStart generation past 0, is
	//only ever replaced whole and carries no checkpoints, so it is returned as it is, with no runs or chain.
	α Scan( const fs::path& file, SRCE )ε->Scanned;
}