#pragma once
#include <jde/fwk/co/Await.h>
#include <jde/app/log/ArchiveQuery.h>
#include <jde/app/proto/Log.pb.h>

namespace Jde::App{
	struct ProtoLog;
	//Today's log, as a query reads it under ProtoLog::_fileLock:  ProtoLog's buffer and what is in flight to the
	//file, so a query sees entries not yet on disk, and all of the file, since a run's entries can name Strings an
	//earlier run holds (Log.proto's FileEntry).
	struct DailyLoadAwait : TAwaitEx<vector<App::Log::Proto::FileEntry>,VoidAwait::Task>{
		using base=TAwaitEx<vector<App::Log::Proto::FileEntry>,VoidAwait::Task>;
		DailyLoadAwait( ProtoLog& log, SRCE )ι:base{sl},_log{ log }{}
		α Execute()ι->VoidAwait::Task;
	private:
		ProtoLog& _log;
	};
}