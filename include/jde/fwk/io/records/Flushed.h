#pragma once
#include <jde/fwk/io/FileAwait.h>
#include "Days.h"

namespace Jde::IO::Records{
	//An owner's last flush, a file of its own:  the moment the flush took its buffer, which every record it wrote arrived
	//before, and the oldest day whose file a start looks at for one still live.  Two 20-byte slots written alternately in
	//place, each the time as the owner's int64, a sequence number, the day as days since 1970-01-01, and the CRC-32C of
	//the three, all little-endian.  A reader takes the valid slot with the higher sequence, so a torn write loses one flush,
	//not the file.
	struct Γ Flushed final{
		static constexpr uint SlotSize{ 20 };
		//One write of the file, into the slot the last one isn't in.
		struct Slot final{
			fs::path File;
			uint8 Index{};
			uint32_t Sequence{};
			int64_t Time{};
			Day Recover;
			string Bytes;
			//In place, and fsynced.  The first makes the file, and fsyncs its directory.
			α Write( SRCE )Ι->WriteAwait;
		};
		//From the file's bytes:  none for a file that isn't there, an owner that has never flushed; one with no valid slot is
		//warned of, and read as that.
		Flushed( fs::path file, optional<string> bytes )ι;
		//Reads the file now.  Throws, at Error, when it is there and can't be read.
		Ω Load( fs::path file, SRCE )ε->Flushed;
		α Time()Ι->optional<int64_t>{ return _time; }
		α Recover()Ι->optional<Day>{ return _recover; }//with Time.
		α Next( int64_t time, Day recover )Ι->Slot;//time never before the last, so a clock set back doesn't move it back.
		α Wrote( const Slot& slot )ι->void;//once its Write has returned.
	private:
		fs::path _file;
		optional<int64_t> _time;
		optional<Day> _recover;
		uint32_t _sequence{};
		uint8 _slot{ 1 };//the valid one, so the first write takes slot 0.
	};
}