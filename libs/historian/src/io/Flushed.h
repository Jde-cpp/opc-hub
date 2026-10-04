#pragma once
#include <jde/fwk/io/FileAwait.h>

namespace Jde::Opc::Hist{
	//A group's last flush, <hist.path>/<name>.flushed:  the moment the flush took its buffer, which every record it wrote
	//arrived before.  Two 16-byte slots written alternately in place, each the time in absolute UA ticks, a sequence
	//number and the CRC-32C of the two, all little-endian.  A reader takes the valid slot with the higher sequence, so a
	//torn write loses one flush time, not the file.
	struct Flushed final{
		static constexpr uint SlotSize{ 16 };
		//One write of the file, into the slot the last one isn't in.
		struct Slot final{
			fs::path File;
			uint8 Index{};
			uint32_t Sequence{};
			TimePoint Time;
			string Bytes;
			//In place, and fsynced.  The first makes the file, and fsyncs its directory.
			α Write( SRCE )Ι->IO::WriteAwait;
		};
		//Reads the file.  No file is a group that has never flushed; one with no valid slot is warned of, and read as that.
		Flushed( fs::path file, SRCE )ε;
		α Time()Ι->optional<TimePoint>{ return _time; }
		α Next( TimePoint time )Ι->Slot;
		α Wrote( const Slot& slot )ι->void;//once its Write has returned.
	private:
		fs::path _file;
		optional<TimePoint> _time;
		uint32_t _sequence{};
		uint8 _slot{ 1 };//the valid one, so the first write takes slot 0.
	};
}
