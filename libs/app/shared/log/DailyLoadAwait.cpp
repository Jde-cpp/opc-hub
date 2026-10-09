#include <jde/fwk/io/file.h>
#include <jde/app/log/DailyLoadAwait.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/co/LockKey.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/app/log/ProtoLog.h>

#define let const auto
namespace Jde::App{
	constexpr ELogTags _tags = ELogTags::ExternalLogger;
	α DailyLoadAwait::Execute()ι->TAwait<CoLockGuard>::Task{
		try{
			optional<CoLockGuard> lock;//held to the end of the read;  Archive mode reads under the caller's - see EDailyLoad.
			if( _mode==EDailyLoad::Query )
				lock = co_await LockKeyAwait{ _file.string() };
			vector<App::Log::Proto::FileEntry> y;
			if( _mode==EDailyLoad::Query ){//Archive reads the file only - see EDailyLoad.
				auto log = Logging::FindLogger<App::ProtoLog>();
				y = log ? log->Entries() : vector<App::Log::Proto::FileEntry>{};
				TRACE( "Memory item count: {}", y.size() );
			}
			if( fs::exists(_file) ){
				IO::ReadAwait read{ _file };//by reference: a ReadAwait does not move.
				auto content = co_await Any( read );
				auto fileContent = Protobuf::DeserializeVector<App::Log::Proto::FileEntry>( content );
				TRACE( "DailyFile item count: {}", fileContent.size() );
				y.insert( y.end(), make_move_iterator(fileContent.begin()), make_move_iterator(fileContent.end()) );
			}
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}