#pragma once
#include <jde/fwk/co/LockKey.h>
#include <jde/app/log/ArchiveQuery.h>
#include <jde/app/proto/Log.pb.h>

namespace Jde::App{
	struct ProtoLog;
	//Today's log, as a query reads it under the daily file's LockKey:  ProtoLog's buffer and what is in flight to the
	//file, so a query sees entries not yet on disk, and of the file only the runs whose times overlap bounds, each run
	//carrying the strings its entries name (Log.proto's FileEntry).
	struct DailyLoadAwait : TAwaitEx<vector<App::Log::Proto::FileEntry>,TAwait<CoLockGuard>::Task>{
		using base=TAwaitEx<vector<App::Log::Proto::FileEntry>,TAwait<CoLockGuard>::Task>;
		DailyLoadAwait( ProtoLog& log, Bounds bounds={}, SRCE )ι:base{sl},_log{ log },_bounds{ bounds }{}
		α Execute()ι->TAwait<CoLockGuard>::Task;
	private:
		ProtoLog& _log;
		Bounds _bounds;
	};
}