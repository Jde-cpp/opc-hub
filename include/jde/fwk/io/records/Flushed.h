#pragma once
#include <jde/fwk/io/FileAwait.h>
#include "Days.h"

namespace Jde::IO::Records{
	//An owner's last flush, a file of its own.
	struct Γ Flushed final{
		//Each of the file's two slots, written alternately in place:  the time as ns since 1970-01-01, a sequence number, the
		//day as days since then, and the CRC-32C of the three, all little-endian.
		static constexpr uint SlotSize{ 20 };
		//One write of the file, into the slot the last one isn't in.
		struct Γ Slot final{
			//In place, and fsynced.  The first makes the file, and fsyncs its directory.
			α Write( SRCE )Ι->WriteAwait;

			fs::path File;
			uint8 Index{};//0 or 1, the slot at Index*SlotSize.
			uint32_t Sequence{};//one past the last flush's, so a reader takes this slot over the other.
			TimePoint Time;//as Flushed::Time():  when this flush took its buffer, never before the last one's.
			Day Recover;//as Flushed::Recover():  where a start looks back to.
			string Bytes;
		};
		//From the file's bytes:  none for a file that isn't there, an owner that has never flushed; one with no valid slot is
		//warned of, and read as that.  Of two valid slots it takes the one with the higher sequence, so a torn write loses one
		//flush, not the file.
		Flushed( fs::path file, optional<string> bytes )ι;
		//Reads the file now.  Throws, at Error, when it is there and can't be read.
		Ω Load( fs::path file, SRCE )ε->Flushed;
		//The moment the flush took its buffer, which every record it wrote arrived before; none for an owner that has never flushed.
		α Time()Ι->optional<TimePoint>{ return _time; }
		//The oldest day whose file a start looks at for a record still live; with Time.
		α Recover()Ι->optional<Day>{ return _recover; }
		α Next( TimePoint time, Day recover )Ι->Slot;//time never before the last, so a clock set back doesn't move it back.
		α Wrote( const Slot& slot )ι->void;//once its Write has returned.
	private:
		fs::path _file;
		optional<TimePoint> _time;
		optional<Day> _recover;
		uint32_t _sequence{};//the valid slot's, which Next goes one past, so the first write is 1.
		uint8 _slot{ 1 };//the valid one, so the first write takes slot 0.
	};
}