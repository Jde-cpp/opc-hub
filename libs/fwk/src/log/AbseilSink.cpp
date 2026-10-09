#include "AbseilSink.h"
#include <condition_variable>
#include <cstdio>
#include <absl/base/no_destructor.h>
#include <absl/log/globals.h>
#include <absl/log/initialize.h>
#include <absl/log/log_entry.h>
#include <absl/log/log_sink.h>
#include <absl/log/log_sink_registry.h>
//Never absl/log/log.h or absl/log/check.h: their LOG and CHECK collide with log.h's LOG and Exception.h's CHECK.
#include <jde/fwk/process/thread.h>

#define let const auto

namespace Jde::Logging{
	constexpr ELogTags _tags{ ELogTags::App };
	constexpr Duration FlushTimeout{ 5s };//a fatal entry's ceiling: the dying thread may hold a lock the drain thread needs.

	Ω toLevel( const absl::LogEntry& e )ι->ELogLevel{
		switch( e.log_severity() ){
			using enum absl::LogSeverity;
			case kInfo:
				if( e.verbosity()==absl::LogEntry::kNoVerbosityLevel )
					return ELogLevel::Information;
				return e.verbosity()<=1 ? ELogLevel::Debug : ELogLevel::Trace;
			case kWarning: return ELogLevel::Warning;
			case kError: return ELogLevel::Error;
			case kFatal: return ELogLevel::Critical;
		}
		return ELogLevel::Critical;
	}

	//Send runs under abseil's sink-set lock, on whatever thread logged - including one inside a logger's Write: ProtoLog
	//serializes under its mutex, and protobuf ABSL_LOG(ERROR)s a string field holding invalid UTF-8.  Calling the loggers
	//from Send would re-lock that mutex on the same thread, and elsewhere take it while holding abseil's lock - the reverse
	//order.  So Send only queues, and a thread of the sink's own hands the entries to the loggers.
	struct AbseilSink final : absl::LogSink{
		α Send( const absl::LogEntry& e )->void override;
		α Flush()->void override;
		α Start()ι->void{ _thread = std::jthread{ [this]( std::stop_token stop ){ Drain(stop); } }; }
		α Stop()ι->void{ _thread.request_stop(); _thread.join(); }//drains what is queued first.
	private:
		struct Item{ optional<Entry> Value; string Line; };//no Value: shut the loggers down, the process is dying.
		α Push( Item&& item )ι->void;
		α Drain( std::stop_token stop )ι->void;
		α OnDrainThread()Ι->bool{ return std::this_thread::get_id()==_thread.get_id(); }

		std::mutex _mutex;
		std::condition_variable_any _cv;
		vector<Item> _queue;
		uint _pushed{};
		uint _drained{};
		std::atomic<bool> _dying;
		std::jthread _thread;
	};

	//A fatal entry arrives twice: LogMessage::PrepareToDie sends it, then sends it again carrying the stack trace.  The second
	//pass forwards only the trace, as abseil's own StderrLogSink does.
	α AbseilSink::Send( const absl::LogEntry& e )->void{
		if( e.log_severity()==absl::LogSeverity::kFatal )
			_dying = true;
		let trace = !e.stacktrace().empty();
		string line{ trace ? e.stacktrace() : e.text_message_with_prefix_and_newline() };
		if( OnDrainThread() || Process::Finalizing() ){//OnDrainThread: a logger the drain thread called logged through abseil - queueing it could feed itself.
			std::fwrite( line.data(), 1, line.size(), stderr );
			return;
		}
		let level = toLevel( e );
		if( ShouldLog(level, _tags) )
			Push( {Entry{level, _tags, (uint32_t)e.source_line(), absl::ToChronoTime(e.timestamp()), {}, string{trace ? e.stacktrace() : e.text_message()}, string{e.source_filename()}, string{}, {}}, move(line)} );
	}

	//Waits for the queue to drain.  LogMessage::Die calls it between a fatal entry and the abort, and then it also has the
	//loggers flushed - by Shutdown(false), the call that writes ProtoLog's buffer; not DestroyLoggers, since other threads
	//may be mid-fan-out over Loggers().
	α AbseilSink::Flush()->void{
		if( OnDrainThread() )
			return;
		std::unique_lock l{ _mutex };
		if( _dying ){
			_queue.push_back( {} );
			++_pushed;
			_cv.notify_all();
		}
		_cv.wait_for( l, FlushTimeout, [this, target=_pushed]{ return _drained>=target; } );
	}

	α AbseilSink::Push( Item&& item )ι->void{
		{
			std::lock_guard _{ _mutex };
			_queue.push_back( move(item) );
			++_pushed;
		}
		_cv.notify_all();
	}

	α AbseilSink::Drain( std::stop_token stop )ι->void{
		Thread::SetName( "AbseilLog" );
		vector<Item> items;
		while( true ){
			{
				std::unique_lock l{ _mutex };
				if( !_cv.wait(l, stop, [this]{ return !_queue.empty(); }) )
					return;//stopped, and nothing left.
				items.swap( _queue );
			}
			for( auto& item : items ){
				if( !item.Value ){
					for( let& logger : Loggers() )
						logger->Shutdown( false );
				}
				else if( Process::Finalizing() )//Log would drop it.
					std::fwrite( item.Line.data(), 1, item.Line.size(), stderr );
				else
					Log( *item.Value );
			}
			{
				std::lock_guard _{ _mutex };
				_drained += items.size();
			}
			_cv.notify_all();
			items.clear();
		}
	}

	//Never destroyed: a process that exits without DestroyLoggers leaves it registered, and abseil may still log during static destruction.
	absl::NoDestructor<AbseilSink> _sink;
	bool _added{};
}
namespace Jde{
	α Logging::AddAbseilSink()ι->void{
		absl::InitializeLog();//until then StderrLogSink prints everything, whatever the threshold.
		absl::SetStderrThreshold( absl::LogSeverityAtLeast::kFatal );//the rest reach the console through spdlog; a fatal prints both ways in case its tag is filtered out.
		_sink->Start();
		absl::AddLogSink( _sink.get() );
		_added = true;
	}
	α Logging::RemoveAbseilSink()ι->void{
		if( !std::exchange(_added, false) )//RemoveLogSink is FATAL for a sink that was never added.
			return;
		absl::RemoveLogSink( _sink.get() );//waits out a Send in progress, so nothing is pushed after the drain thread stops.
		_sink->Stop();
	}
}
