#include <jde/fwk/log/MemoryLog.h>

#define let const auto
namespace Jde::Tests{
	struct LogEntryTests : public ::testing::Test{
	protected:
		α SetUp()->void override{ Logging::ClearMemory(); }
		Ω logger()ι->Logging::MemoryLog&{ return Logging::GetLogger<Logging::MemoryLog>(); }
	};

	//An entry off the wire is rebuilt from Text + Arguments and formatted lazily here, so a mismatched pair is data,
	//not a programming error - it must not escape as an exception into whatever is draining the socket.  The catch is
	//also the only thing that surfaces the offending template, because Message() memoizes the fallback and every later
	//caller sees the result rather than the failure.
	TEST_F( LogEntryTests, MessageSurvivesABadFormat ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, Jde::UserPK{7}, string{"a {} b {}"}, vector<string>{"1"} };
		string message;
		ASSERT_NO_THROW( message = e.Message() ) << "one argument for two placeholders is wire data, not a fatal error";
		EXPECT_NE( message.find("a {} b {}"), string::npos ) << "the unformattable template has to survive into the message: " << message;
		EXPECT_NE( message.find('1'), string::npos ) << "...and so do the arguments that did arrive: " << message;
		EXPECT_EQ( e.UserPK, Jde::UserPK{7} );
		EXPECT_EQ( e.Message(), message ) << "memoized - the fallback is computed once";
		EXPECT_FALSE( logger().Find([](let& x){ return x.Text.starts_with("Bad Format"); }).empty() ) << "nothing logged the template that could not be formatted";
	}

	TEST_F( LogEntryTests, MessageFormatsAWellFormedEntry ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, Jde::UserPK{}, string{"a {} b {}"}, vector<string>{"1","2"} };
		EXPECT_EQ( e.Message(), "a 1 b 2" );
		Logging::Entry noArgs{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{"{ not a placeholder }"} };
		EXPECT_EQ( noArgs.Message(), "{ not a placeholder }" ) << "no arguments short-circuits vformat, so braces are never parsed";
	}

	//Id/FileId/FunctionId are the proto-log's join keys: the wire carries the md5 and the receiver looks the text up by
	//it, so the id has to be the md5 of the *template* rather than of the formatted message - otherwise two entries
	//from one log line key differently and the text is never resolved.
	TEST_F( LogEntryTests, IdsAreMemoizedMd5sOfTheirText ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{"id {}"}, vector<string>{"1"} };
		EXPECT_EQ( e.Id(), Logging::Entry::GenerateId("id {}") ) << "the template, not the formatted message";
		EXPECT_NE( e.Id(), Logging::Entry::GenerateId(e.Message()) );
		EXPECT_EQ( e.Id(), e.Id() ) << "memoized";
		EXPECT_EQ( e.FileId(), Logging::Entry::GenerateId(e.File()) );
		EXPECT_EQ( e.FunctionId(), Logging::Entry::GenerateId(e.Function()) );
		EXPECT_NE( e.FileId(), e.FunctionId() ) << "separate memos, not one shared slot";
	}

	//the overload the wire path uses: entries received from another instance are already built, so they arrive through
	//Log(const Entry&) rather than the variadic Log, and it is this one that has to route them to every logger - and
	//to apply each logger's own level, which the cumulative check before it does not.
	TEST_F( LogEntryTests, LogEntryReachesTheLoggers ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{"entry to the loggers {}"}, vector<string>{"1"} };
		Logging::Log( e );
		let found = Logging::Find( e.Id() );
		ASSERT_EQ( found.size(), 1u );
		EXPECT_EQ( found[0].Message(), "entry to the loggers 1" );
		EXPECT_EQ( found[0].Level, ELogLevel::Information );
		EXPECT_EQ( found[0].Tags, ELogTags::Test );

		//the memory logger is configured Debug while the spd logger takes test at Trace, so the cumulative admits this
		//and the per-logger check is the only thing that can keep it out.
		Logging::ClearMemory();
		Logging::Entry belowThisLogger{ SRCE_CUR, ELogLevel::Trace, ELogTags::Test, string{"entry below the memory logger"} };
		Logging::Log( belowThisLogger );
		EXPECT_TRUE( Logging::Find(belowThisLogger.Id()).empty() ) << "Log(Entry) skipped the per-logger level and wrote a Trace into a Debug logger";
	}

	//LogOnce is `if( MarkLogged(GenerateId(text)) ) Log(...)`, and _loggedEntries is process-global with no clear - so
	//a --gtest_repeat pass cannot re-arm a literal, and an "exactly one entry" assertion would hold only on the first.
	//Split accordingly: this pins the predicate with an id made unique per pass, LogOnceRespectsAnAlreadyLoggedId pins
	//that LogOnce honours a false answer, and the Log() call between them is the overload asserted above.
	TEST_F( LogEntryTests, MarkLoggedIsOncePerId ){
		static uint32_t pass{};
		++pass;
		let id = Logging::Entry::GenerateId( Ƒ("LogEntryTests.MarkLogged pass {}", pass) );
		EXPECT_TRUE( Logging::MarkLogged(id) ) << "a fresh id must report itself new - this is what gates LogOnce";
		EXPECT_FALSE( Logging::MarkLogged(id) ) << "...and never again";
		EXPECT_TRUE( Logging::MarkLogged(Logging::Entry::GenerateId(Ƒ("LogEntryTests.MarkLogged other {}", pass))) ) << "a different id is unaffected";
	}

	TEST_F( LogEntryTests, LogOnceRespectsAnAlreadyLoggedId ){
		constexpr sv text{ "LogEntryTests LogOnce suppressed" };
		Logging::MarkLogged( Logging::Entry::GenerateId(text) );//claim it here, so the call below has to stay silent on every pass.
		Logging::ClearMemory();
		Logging::LogOnce( SRCE_CUR, ELogLevel::Information, ELogTags::Test, "LogEntryTests LogOnce suppressed" );
		EXPECT_TRUE( logger().Find(string{text}).empty() ) << "LogOnce logged an id that was already marked";
	}

	//the finding's own case.  EXPECT_LE rather than EXPECT_EQ(...,1) for the reason above: on a repeat pass the id is
	//already claimed and the count is legitimately 0.  The MarkLogged check is what gives it teeth on every pass - it
	//fails unless LogOnce actually reached the de-dup set.
	TEST_F( LogEntryTests, LogOnceLogsAtMostOnce ){
		constexpr sv text{ "LogEntryTests LogOnce emitted" };
		Logging::LogOnce( SRCE_CUR, ELogLevel::Information, ELogTags::Test, "LogEntryTests LogOnce emitted" );
		Logging::LogOnce( SRCE_CUR, ELogLevel::Information, ELogTags::Test, "LogEntryTests LogOnce emitted" );
		EXPECT_LE( logger().Find(string{text}).size(), 1u ) << "two identical LogOnce calls produced more than one entry";
		EXPECT_FALSE( Logging::MarkLogged(Logging::Entry::GenerateId(text)) ) << "LogOnce did not mark the id it logged";
	}
}
