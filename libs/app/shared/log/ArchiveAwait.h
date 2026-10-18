#pragma once
#include <jde/fwk/co/LockKey.h>

namespace Jde::App{
	struct ProtoLog;
	//The archive round:  under the daily file's LockKey, each entry of the file's runs goes to its local day's archive
	//with the strings it names, and the file is removed.  A day's archive is rewritten whole by the record layer, the
	//entries it holds merged in time order with the day's new ones, under a FileStart one generation on and a Merged
	//naming the daily file and how far into it the round read (Log.proto's FileEntry):  a round that failed at a later
	//day, or a crash before the removal, leaves the archive in place and the next round adds none of its entries again.
	//The file is written to a temp beside it and renamed over, so the name only ever refers to a complete archive.  An
	//archive that isn't the layer's, an earlier build's included, or that can't be read through, is left as it is and the
	//day's entries are dropped, with an error, rather than held for a file that will never take them.
	//The lock covers the daily *file* only - ProtoLog::Write never takes it, it only takes ProtoLog::_mutex.  So the round
	//archives what is durably in the file and nothing else; ProtoLog::Save flushes its buffer into the file before starting
	//a round, and entries written after that stay buffered for the next one.
	struct ArchiveAwait final : VoidAwait{
		ArchiveAwait( ProtoLog& log, SRCE )ι:VoidAwait{ sl }, _log{ log }{}
		α Suspend()ι->void override{ Execute(); }
	private:
		α Execute()ι->TAwait<CoLockGuard>::Task;
		ProtoLog& _log;
	};
}