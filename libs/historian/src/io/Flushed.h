#pragma once

namespace Jde::Opc::Hist{
	//A group's last flush, <hist.path>/<name>.flushed:  the moment the flush took its buffer, which every record it wrote
	//arrived before.  Two 16-byte slots written alternately in place, each the time in absolute UA ticks, a sequence
	//number and the CRC-32C of the two, all little-endian.  A reader takes the valid slot with the higher sequence, so a
	//torn write loses one flush time, not the file.
	struct Flushed final{
		static constexpr uint SlotSize{ 16 };
		//Reads the file.  No file is a group that has never flushed; one with no valid slot is warned of, and read as that.
		Flushed( fs::path file, SRCE )ε;
		α Time()Ι->optional<TimePoint>{ return _time; }
		//Into the slot the last one isn't in, then fsynced.  The first creates the file, and fsyncs its directory.
		α Write( TimePoint time, SRCE )ε->void;
	private:
		fs::path _file;
		optional<TimePoint> _time;
		uint32_t _sequence{};
		uint8 _slot{ 1 };//the valid one, so the first write takes slot 0.
	};
}
