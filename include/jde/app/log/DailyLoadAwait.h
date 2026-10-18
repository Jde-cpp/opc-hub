#pragma once
#include <jde/fwk/co/LockKey.h>
#include <jde/app/log/ArchiveQuery.h>
#include <jde/app/proto/Log.pb.h>

namespace Jde::App{
	struct ProtoLog;
	//Today's log, as a query reads it under the daily file's LockKey:  ProtoLog's buffer and what is in flight to the
	//file, so a query sees entries not yet on disk, and all of the file, since a run's entries can name Strings an
	//earlier run holds (Log.proto's FileEntry).
	struct DailyLoadAwait : TAwaitEx<vector<App::Log::Proto::FileEntry>,TAwait<CoLockGuard>::Task>{
		using base=TAwaitEx<vector<App::Log::Proto::FileEntry>,TAwait<CoLockGuard>::Task>;
		DailyLoadAwait( ProtoLog& log, SRCE )ι:base{sl},_log{ log }{}
		α Execute()ι->TAwait<CoLockGuard>::Task;
	private:
		ProtoLog& _log;
	};
}