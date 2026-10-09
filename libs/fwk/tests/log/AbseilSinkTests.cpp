#include <absl/log/absl_log.h>//ABSL_LOG only - absl/log/log.h's LOG collides with log.h's.
#include <absl/log/globals.h>
#include <absl/log/log_sink_registry.h>
#include <jde/fwk/log/MemoryLog.h>

#define let const auto

namespace Jde::Tests{
	//Stands in for ProtoLog, which holds its absl::Mutex while it serializes - and protobuf ABSL_LOG(ERROR)s an argument holding
	//invalid UTF-8.  A sink that called the loggers from Send re-locked that mutex on the same thread, and taking it under
	//abseil's sink-set lock anywhere else is the reverse order; either way abseil's debug deadlock detector aborts the process.
	struct LockingLogger final : Logging::ILogger{
		LockingLogger()ι:ILogger{ jobject{{"tags", jobject{{"default", "Information"}}}} }{}
		α Write( const Logging::Entry& e )ι->void override{
			ul _{ _mutex };
			if( e.Text==Trigger )
				ABSL_LOG(ERROR) << "absl under a logger's lock";
		}
		α Write( const Logging::Entry& e, uint32, uint32 )ι->void override{ Write( e ); }
		α Shutdown( bool, SL )ι->void override{}
		α Name()Ι->sv override{ return "LockingLogger"; }
		static constexpr sv Trigger{ "abseil sink: log under a logger's lock" };
	private:
		absl::Mutex _mutex;
	};
	//After Logging::Init, before any test - where ProtoLog::Init adds itself in a service.
	struct LockingLoggerEnvironment final : ::testing::Environment{
		α SetUp()->void override{ Logging::AddLogger( mu<LockingLogger>() ); }
	};
	[[maybe_unused]] const auto* _lockingLogger = ::testing::AddGlobalTestEnvironment( new LockingLoggerEnvironment );

	//#274: abseil's own logging, and protobuf's through it, went to abseil's stderr sink and never reached the loggers.
	struct AbseilSinkTests : ::testing::Test{
	protected:
		α SetUp()->void override{ Logging::ClearMemory(); }
		Ω logger()ι->Logging::MemoryLog&{ return Logging::GetLogger<Logging::MemoryLog>(); }
		//the sink queues and a thread of its own logs - FlushLogSinks waits for it.
		Ω find( sv text )ι->vector<Logging::Entry>{
			absl::FlushLogSinks();
			return Logging::Find( [text](let& e){ return e.Text==text; } );
		}
	};

	TEST_F( AbseilSinkTests, SeverityMapsToLevel ){
		let line = __LINE__+1;
		ABSL_LOG(INFO) << "absl info";
		ABSL_LOG(WARNING) << "absl warning";
		ABSL_LOG(ERROR) << "absl error { not a placeholder }";
		for( let& [text, level] : vector<std::pair<sv,ELogLevel>>{ {"absl info", ELogLevel::Information}, {"absl warning", ELogLevel::Warning}, {"absl error { not a placeholder }", ELogLevel::Error} } ){
			let found = find( text );
			ASSERT_EQ( found.size(), 1u ) << text;
			EXPECT_EQ( found[0].Level, level ) << text;
			EXPECT_EQ( found[0].Tags, ELogTags::App ) << text;
			EXPECT_EQ( found[0].Message(), text ) << "the text arrives formatted - braces in it are not placeholders";
		}
		let info = find( "absl info" ).front();
		EXPECT_EQ( info.File(), sv{SRCE_CUR.file_name()} );
		EXPECT_EQ( info.Line, line );
		EXPECT_TRUE( info.Function().empty() ) << "absl::LogEntry carries no function name";
		EXPECT_LT( Clock::now()-info.Time, 1min ) << "the entry's time is abseil's timestamp, converted";
	}

	TEST_F( AbseilSinkTests, VerbosityMapsToDebugAndTrace ){
		logger().SetLevel( ELogTags::App, ELogLevel::Trace );//memory's default is Debug.
		let previous = absl::SetGlobalVLogLevel( 2 );
		ABSL_VLOG(1) << "absl vlog 1";
		ABSL_VLOG(2) << "absl vlog 2";
		ABSL_LOG(WARNING).WithVerbosity( 2 ) << "absl verbose warning";
		absl::SetGlobalVLogLevel( previous );
		absl::FlushLogSinks();//before ClearLevel: the drain thread filters again, in Logging::Log.
		logger().ClearLevel( ELogTags::App );

		let levelOf = []( sv text )->optional<ELogLevel>{ let found = find( text ); return found.size()==1 ? optional{found[0].Level} : optional<ELogLevel>{}; };
		EXPECT_EQ( levelOf("absl vlog 1"), ELogLevel::Debug );
		EXPECT_EQ( levelOf("absl vlog 2"), ELogLevel::Trace );
		EXPECT_EQ( levelOf("absl verbose warning"), ELogLevel::Warning ) << "verbosity only refines kInfo";
	}

	//The first line takes LockingLogger's mutex under abseil's lock if the sink logs from Send; the second, logged while
	//LockingLogger holds its mutex, then re-locks it and closes the cycle.  A regression aborts the suite here.
	TEST_F( AbseilSinkTests, LoggingUnderALoggersLockDoesNotReenter ){
		ABSL_LOG(WARNING) << "absl before the lock";
		INFOT( ELogTags::Test, "abseil sink: log under a logger's lock" );//LockingLogger::Trigger - a format string has to be a literal.
		EXPECT_EQ( find("absl before the lock").size(), 1u );
		EXPECT_EQ( find("absl under a logger's lock").size(), 1u ) << "logged from inside a logger, it still reaches the loggers - afterwards";
	}
}
