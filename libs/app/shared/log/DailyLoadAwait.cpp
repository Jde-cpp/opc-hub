#include <jde/app/log/DailyLoadAwait.h>
#include <jde/fwk/co/LockKey.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/app/log/ProtoLog.h>

#define let const auto
namespace Jde::App{
	constexpr ELogTags _tags = ELogTags::ExternalLogger;
	α DailyLoadAwait::Execute()ι->TAwait<CoLockGuard>::Task{
		try{
			auto lock = co_await LockKeyAwait{ _log.DailyFile().string() };//held to the end of the read.
			auto y = _log.Entries();
			TRACE( "Memory item count: {}", y.size() );
			auto& file = _log._file;
			if( !file.Opened )
				file = co_await Log::DailyOpenAwait{ _log.DailyFile() };
			std::ranges::copy( file.Salvaged, std::back_inserter(y) );
			let start = _bounds.Start ? Log::Ns( *_bounds.Start ) : std::numeric_limits<int64_t>::min();
			let end = _bounds.End ? Log::Ns( *_bounds.End ) : std::numeric_limits<int64_t>::max();
			vector<Log::Run> runs;
			for( let& run : file.Runs ){
				if( run.Last>=start && run.First<=end )
					runs.push_back( run );
			}
			if( runs.size() ){
				sp<IO::OpenFile> open;
				try{
					open = ms<IO::OpenFile>( file.Path );
				}
				catch( IO::IOException& e ){
					if( e.Error!=IO::EIOError::NotFound )
						throw;
					e.SetLevel( ELogLevel::Debug );//removed since the process last wrote it:  the next flush finds out.
				}
				if( open ){
					auto records = co_await Log::RunsReadAwait{ move(open), move(runs) };
					TRACE( "DailyFile item count: {}", records.size() );
					std::ranges::move( records, std::back_inserter(y) );
				}
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}