#include <jde/app/log/LogQLAwait.h>
#include <jde/fwk/chrono.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/io/FileAwait.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/fwk/log/log.h>
#include <jde/app/log/DailyLoadAwait.h>
#include <jde/app/log/ProtoLog.h>

#define let const auto

namespace Jde::App{
	constexpr ELogTags _tags{ ELogTags::ExternalLogger };
	using namespace std::chrono;
	//Newest first reads today's log, then the archives back from the end bound;  oldest first reads the archives forward
	//from the start bound, then today's log.  Either way the read stops once the page is full.  Today's log is skipped when
	//the end bound falls before the daily file starts.
	α LogQLAwait::Execute()ι->TAwait<jvalue>::Task{
		try{
			auto& log = Logging::GetLogger<ProtoLog>();
			let bounds = TimeBounds( _ql.Filter() );
			let& [start, end] = bounds;
			bool readLocal = !end || *end > log.DailyFileStart();
			let& ob = _ql.OrderByJson();
			let timeAsc = ob.size() && ob[0].first=="time" && ob[0].second;
			ArchiveQuery archive{ move(_hidden) };
			if( readLocal && !timeAsc ){
				archive.Append( _ql, co_await Any(DailyLoadAwait{log}) );
				readLocal = false;
				TRACE( "Daily entries: {}, complete: {}", archive.EntrySize(), archive.IsComplete(_ql) );
			}
			if( let files = archive.IsComplete(_ql) ? flat_map<year_month_day,fs::path>{} : ArchiveDayFiles(log.Root()); files.size() ){
				let& tz = log.TimeZone();
				let first = start ? year_month_day{ floor<days>(tz.to_local(*start)) } : files.begin()->first;
				let last = end ? year_month_day{ ceil<days>(tz.to_local(*end)) } : files.rbegin()->first;
				vector<fs::path> days;
				for( let& [ymd,file] : files ){
					if( ymd>=first && ymd<=last )
						days.push_back( file );
				}
				if( !timeAsc )
					std::ranges::reverse( days );
				for( let& file : days ){
					IO::ReadAwait read{ file };//by reference: a ReadAwait does not move.
					auto bytes = co_await Any( read );
					if( !Log::HeadOf(bytes) )
						throw IO::IOException{ _sl, file, ELogLevel::Error, "isn't an archive the record layer wrote" };
					archive.Append( _ql, Log::ParseRecords(bytes, file) );
					if( archive.IsComplete(_ql) )
						break;
				}
			}
			if( readLocal && !archive.IsComplete(_ql) )
				archive.Append( _ql, co_await Any(DailyLoadAwait{log}) );
			Resume( archive.ToJson(_ql) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}
}