#pragma once
#include <deque>
#include <jde/fwk/co/LockKey.h>
#include <jde/ql/types/TableQL.h>
#include <jde/app/log/ArchiveQuery.h>
#include <jde/app/proto/Log.pb.h>

namespace Jde::App{
		struct ArchiveAwait : VoidAwait{
			ArchiveAwait( fs::path dailyFile, fs::path path, const std::chrono::time_zone& tz, SRCE )ι:VoidAwait{ sl }, _dailyFile{ move(dailyFile) }, _path{ move(path) }, _tz{ tz } {}
			α Suspend()ι->void override{ Execute(); }
		private:
			α Execute()ι->TAwait<CoLockGuard>::Task;

			fs::path _dailyFile;
			fs::path _path;
			const std::chrono::time_zone& _tz;
		};
	}