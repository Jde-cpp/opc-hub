#pragma once
#include <jde/fwk/io/FileAwait.h>

namespace Jde::Opc::Hist{
	using Day = std::chrono::year_month_day;

	//A group's last flush, <hist.path>/<name>.flushed:  the moment the flush took its buffer, which every record it wrote
	//arrived before, and the oldest day whose file a start looks at for one still live.  Two 20-byte slots written
	//alternately in place, each the time in absolute UA ticks, a sequence number, the day as days since 1970-01-01, and
	//the CRC-32C of the three, all little-endian.  A reader takes the valid slot with the higher sequence, so a torn write
	//loses one flush, not the file.
	struct Flushed final{
		static constexpr uint SlotSize{ 20 };
		//One write of the file, into the slot the last one isn't in.
		struct Slot final{
			fs::path File;
			uint8 Index{};
			uint32_t Sequence{};
			TimePoint Time;
			Day Recover;
			string Bytes;
			//In place, and fsynced.  The first makes the file, and fsyncs its directory.
			α Write( SRCE )Ι->IO::WriteAwait;
		};
		//Reads the file.  No file is a group that has never flushed; one with no valid slot is warned of, and read as that.
		Flushed( fs::path file, SRCE )ε;
		α Time()Ι->optional<TimePoint>{ return _time; }
		α Recover()Ι->optional<Day>{ return _recover; }//with Time.
		α Next( TimePoint time, Day recover )Ι->Slot;//time never before the last, so a clock set back doesn't move it back.
		α Wrote( const Slot& slot )ι->void;//once its Write has returned.
	private:
		fs::path _file;
		optional<TimePoint> _time;
		optional<Day> _recover;
		uint32_t _sequence{};
		uint8 _slot{ 1 };//the valid one, so the first write takes slot 0.
	};
}
