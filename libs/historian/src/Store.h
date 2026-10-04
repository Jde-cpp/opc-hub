#pragma once
#include <jde/historian/Historian.h>
#include "io/File.h"

namespace Jde::Opc::Hist{
	//What a historian's groups share:  its settings and clock, its lock on hist.path, and the cap on what they buffer
	//together.
	struct Store final : noncopyable{
		//Takes the lock.  When it can't, the historian is disabled, with a Critical log saying why.
		Store( Settings settings, sp<IClock> clock )ι;
		const Settings Config;
		const sp<IClock> Time;
		optional<PathLock> Lock;//until the historian is destroyed.
		string Disabled;//why there was no lock to take.

		//The order records arrived in, across every group, which is the order a full buffer drops them in.
		α Sequence()ι->uint{ return _sequence.fetch_add( 1 )+1; }
		α Buffered()Ι->uint{ return _buffered.load(); }
		//True once the buffers are past maxBuffer, for the caller to Trim when it holds no lock of a group's.
		α Add( uint bytes )ι->bool{ return _buffered.fetch_add( bytes )+bytes>Config.MaxBuffer; }
		α Subtract( uint bytes )ι->void{ _buffered.fetch_sub( bytes ); }
		//Drops the oldest values across every group until the buffers are back within maxBuffer, with a warning for the
		//first of a streak.  Each node keeps what a flush needs to mark its gap.
		α Trim()ι->void;
		α Wrote()ι->void{ _dropping = false; }//a flush wrote all it took, so the next drop starts a streak.
		α Register( Group& group )ι->void;
		α Unregister( Group& group )ι->void;
	private:
		std::atomic<uint> _sequence{};
		std::atomic<uint> _buffered{};
		std::atomic<bool> _dropping{};
		absl::Mutex _mutex;//before any group's.
		vector<Group*> _groups ABSL_GUARDED_BY(_mutex);
	};
}
