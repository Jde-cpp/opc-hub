#pragma once
#include <jde/historian/Historian.h>
#include "io/File.h"

namespace Jde::Opc::Hist{
	//What a historian's groups share:  its settings and clock, its lock on hist.path, and the cap on what they buffer
	//together.
	struct Store final : noncopyable, std::enable_shared_from_this<Store>{
		//Takes the lock.  When it can't, the historian is disabled, with a Critical log saying why.
		Store( Settings settings, sp<IClock> clock )ι;
		const Settings Config;
		const sp<IClock> Time;
		optional<PathLock> Lock;//until the historian is destroyed.
		string Disabled;//why there was no lock to take.

		//The order records arrived in, across every group, which is the order a full buffer drops them in.
		α Sequence()ι->uint{ return _sequence.fetch_add( 1 )+1; }
		α Buffered()Ι->uint{ return _buffered.load(); }
		//True once the buffers are past maxBuffer, for the caller to flush, or when it can't, to RequestTrim once it holds
		//no lock of a group's.
		α Add( uint bytes )ι->bool{ return _buffered.fetch_add( bytes )+bytes>Config.MaxBuffer; }
		//Never below 0, which an unsigned count would wrap past into every Enqueue trimming:  that is a miscount, asserted.
		α Subtract( uint bytes )ι->void{
			auto had = _buffered.load();
			while( !_buffered.compare_exchange_weak(had, had-std::min(had, bytes)) )
			{}
			ASSERT( had>=bytes );
		}
		//Once the buffers are past maxBuffer, drops the oldest values across every group until they are an eighth below
		//it, so the next value doesn't trim again.  A group whose drops begin a streak is warned of once.  Each node keeps
		//what a flush needs to mark its gap.
		α Trim()ι->void;
		//Trim on the clock's zero-delay hop, off the collection path, which takes only a group's own lock and never logs.
		//One at a time:  those asked for meanwhile are the same trim.
		α RequestTrim()ι->void;
		α Register( Group& group )ι->void;
		α Unregister( Group& group )ι->void;
	private:
		std::atomic<uint> _sequence{};
		std::atomic<uint> _buffered{};
		std::atomic<bool> _trimRequested{};
		absl::Mutex _mutex;//before any group's.
		vector<Group*> _groups ABSL_GUARDED_BY(_mutex);
	};
}
