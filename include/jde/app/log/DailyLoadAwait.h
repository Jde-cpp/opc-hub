#pragma once
#include <jde/fwk/co/LockKey.h>
#include <jde/app/proto/Log.pb.h>

namespace Jde::App{
	//Query - takes the file's LockKey for the read, and merges ProtoLog's unflushed buffer with the file so a query sees entries not yet on disk.
	//Archive - the caller already holds the LockKey and keeps holding it past the read (ArchiveAwait must, to cover the read→archive→remove window;
	//  LockKeyAwait is not reentrant, so re-taking it here would deadlock), and *only the file* is read: the archive deletes the file when it is done,
	//  but nothing trims ProtoLog's buffer, so an entry archived out of memory would be archived a second time once the next flush wrote it to disk.
	enum class EDailyLoad : uint8{ Query, Archive };
	struct DailyLoadAwait : TAwaitEx<vector<App::Log::Proto::FileEntry>,TAwait<CoLockGuard>::Task>{
		using base=TAwaitEx<vector<App::Log::Proto::FileEntry>,TAwait<CoLockGuard>::Task>;
		DailyLoadAwait( fs::path file, EDailyLoad mode=EDailyLoad::Query, SRCE )ι:base{sl},_file{ move(file) },_mode{mode}{}
		α Execute()ι->TAwait<CoLockGuard>::Task;
	private:
		fs::path _file;
		EDailyLoad _mode;
	};
}