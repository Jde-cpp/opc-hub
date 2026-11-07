#include <spdlog/logger.h>
#include <spdlog/sinks/ostream_sink.h>
#include <jde/tests/Capture.h>

#define let const auto
namespace Jde::Tests{
	struct LogEntryTests : public ::testing::Test{
	protected:
		α SetUp()->void override{ Logging::ClearMemory(); }
	};

	//An entry off the wire is rebuilt from Text + Arguments and formatted lazily here, so a mismatched pair is data,
	//not a programming error - it must not escape as an exception into whatever is draining the socket.  The catch is
	//also the only thing that surfaces the offending template, because Message() memoizes the fallback and every later
	//caller sees the result rather than the failure.
	TEST_F( LogEntryTests, MessageSurvivesABadFormat ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, Jde::UserPK{7}, string{"a {} b {}"}, vector<Logging::Argument>{"1"} };
		string message;
		ASSERT_NO_THROW( message = e.Message() ) << "one argument for two placeholders is wire data, not a fatal error";
		EXPECT_NE( message.find("a {} b {}"), string::npos ) << "the unformattable template has to survive into the message: " << message;
		EXPECT_NE( message.find('1'), string::npos ) << "...and so do the arguments that did arrive: " << message;
		EXPECT_EQ( e.UserPK, Jde::UserPK{7} );
		EXPECT_EQ( e.Message(), message ) << "memoized - the fallback is computed once";
		EXPECT_FALSE( Logging::Find([](let& x){ return x.Text().starts_with("Bad Format"); }).empty() ) << "nothing logged the template that could not be formatted";
	}

	TEST_F( LogEntryTests, ArgumentsMatchTheConsole ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "{} {} {}", true, (uint8_t)5, 1234567.0 };
		EXPECT_EQ( e.Arguments, (vector<Logging::Argument>{true, uint64_t{5}, 1234567.0}) );
		EXPECT_EQ( e.Message(), fmt::format("{} {} {}", true, (uint8_t)5, 1234567.0) ) << "the stored form renders as spdlog prints the line";
	}
	//reviews-issue/log-pass2.md #4:  fmt writes a float in exponent form from 1e7, a double not until 1e16, so a float there
	//is kept as its text.  Below 1e7 and from 1e16 it is still the double that renders as it did.
	TEST_F( LogEntryTests, AFloatRendersAsTheConsoleDid ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "{} {} {} {}", 12345678.0f, -1e10f, 0.1f, 1e20f };
		EXPECT_EQ( e.Message(), fmt::format("{} {} {} {}", 12345678.0f, -1e10f, 0.1f, 1e20f) );
		EXPECT_EQ( e.Arguments, (vector<Logging::Argument>{string{"1.2345678e+07"}, string{"-1e+10"}, 0.1, 1e20}) );
	}

	TEST_F( LogEntryTests, MessageFormatsAWellFormedEntry ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, Jde::UserPK{}, string{"a {} b {}"}, vector<Logging::Argument>{"1","2"} };
		EXPECT_EQ( e.Message(), "a 1 b 2" );
		Logging::Entry noArgs{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{"{ not a placeholder }"} };
		EXPECT_EQ( noArgs.Message(), "{ not a placeholder }" ) << "no arguments short-circuits vformat, so braces are never parsed";
	}

	//Id/FileId/FunctionId are the proto-log's join keys: the wire carries the md5 and the receiver looks the text up by
	//it, so the id has to be the md5 of the *template* rather than of the formatted message - otherwise two entries
	//from one log line key differently and the text is never resolved.
	TEST_F( LogEntryTests, IdsAreMemoizedMd5sOfTheirText ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{"id {}"}, vector<Logging::Argument>{"1"} };
		EXPECT_EQ( e.Id(), Logging::Entry::GenerateId("id {}") ) << "the template, not the formatted message";
		EXPECT_NE( e.Id(), Logging::Entry::GenerateId(e.Message()) );
		EXPECT_EQ( e.Id(), e.Id() ) << "memoized";
		EXPECT_EQ( e.FileId(), Logging::Entry::GenerateId(e.File()) );
		EXPECT_EQ( e.FunctionId(), Logging::Entry::GenerateId(e.Function()) );
		EXPECT_NE( e.FileId(), e.FunctionId() ) << "separate memos, not one shared slot";
	}

	//A template's id is the compiler's (Logging::Format), and every other id CalcMd5's at run time:  the two have to agree,
	//or a line is stored under an id its template's String record doesn't carry.
	TEST_F( LogEntryTests, AFormatsIdIsItsTemplatesMd5 ){
		constexpr Logging::Format<int> format{ "format id {}" };
		EXPECT_EQ( format.Id, Logging::GenerateId("format id {}") );
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "format id {}", 1 };
		EXPECT_EQ( e.Id(), format.Id ) << "an entry built from a format";
		INFOT( ELogTags::Test, "format id {}", 2 );
		let logged = Logging::Find( "format id 2" );
		ASSERT_EQ( logged.size(), 1u );
		EXPECT_EQ( logged[0].Id(), format.Id ) << "a logged line";
	}

	//A spec is applied when the argument is stored, and the template keeps a plain {} in its place, so a stored entry
	//renders as the console line - fmt over the real arguments - instead of "Bad Format".
	TEST_F( LogEntryTests, ASpecIsAppliedWhenTheArgumentIsStored ){
		constexpr Logging::Format<int, double, const char*, int> format{ "{:x} {:.1f} {{{:>4}}} {}" };
		static_assert( format.Specs==0b0111 );
		EXPECT_EQ( format.Template(), "{} {} {{{}}} {}" ) << "escaped braces stay escaped";
		EXPECT_EQ( format.Id, Logging::GenerateId(format.Template()) );

		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "{:x} {:.1f} {{{:>4}}} {}", 255, 3.14159, "ab", 7 };
		EXPECT_EQ( e.Text(), "{} {} {{{}}} {}" );
		EXPECT_EQ( e.Arguments, (vector<Logging::Argument>{"ff", "3.1", "  ab", int64_t{7}}) );
		EXPECT_EQ( e.Message(), fmt::format("{:x} {:.1f} {{{:>4}}} {}", 255, 3.14159, "ab", 7) );
		EXPECT_EQ( e.Id(), format.Id );

		INFOT( ELogTags::Test, "logged with a spec {:x}", 255 );
		EXPECT_EQ( Logging::Find("logged with a spec ff").size(), 1u ) << "the capture renders it as the console does";
		EXPECT_TRUE( Logging::Find([](let& x){ return x.Text().starts_with("Bad Format"); }).empty() );
	}

	//A template is a view of its Format's literal, and a copy of the entry views it too, so copying costs nothing until a
	//logger keeps one past the line and calls OwnNames.  One with a spec is stripped into a copy.
	TEST_F( LogEntryTests, TheTemplateIsAViewOfTheLiteral ){
		static constexpr char literal[]{ "template view {}" };
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, literal, 1 };
		EXPECT_EQ( e.Text().data(), literal ) << "the template was copied";
		let copy = e;
		EXPECT_EQ( copy.Text().data(), literal ) << "a copy of the entry copied it";
		EXPECT_EQ( copy.Id(), Logging::GenerateId(literal) );
		Logging::Entry spec{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "template view {:x}", 1 };
		EXPECT_EQ( spec.Text(), "template view {}" );
	}

	//Each plain {} argument as fmt saw it, the value a log file stores typed (log.md #11):  int8_t arrives as an int, a float
	//is the double that renders as it did, and a field with a spec, a char and text are their text.
	TEST_F( LogEntryTests, ArgumentsAreTypedAsFmtSawThem ){
		const int8_t small{ -3 };
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "{} {} {} {} {} {} {} {:x} {} {}", small, -4, 5u, uint64_t{6}, 0.5, 0.1f, true, 255, 'c', "text" };
		EXPECT_EQ( e.Arguments, (vector<Logging::Argument>{int64_t{-3}, int64_t{-4}, uint64_t{5}, uint64_t{6}, 0.5, 0.1, true, "ff", "c", "text"}) ) << "0.1f as a double is 0.10000000149011612";
		EXPECT_EQ( e.Message(), "-3 -4 5 6 0.5 0.1 true ff c text" );
	}

	//fmt has no formatter for boost::json::string, so a log line couldn't take one;  fwk's formats it as its text, specs and all.
	TEST_F( LogEntryTests, AJsonStringIsItsText ){
		const boost::json::string text{ "text" }, spec{ "spec" };
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "json {} {:>6}", text, spec };
		EXPECT_EQ( e.Arguments, (vector<Logging::Argument>{"text", "  spec"}) );
		EXPECT_EQ( e.Message(), "json text   spec" );
	}

	//Every logger gets the line as one Record.  Emit's builds its Entry on first ask, once, for every logger after;  an Entry's
	//wraps it, with a forwarded entry's origin.
	TEST_F( LogEntryTests, ARecordBuildsItsEntryOnce ){
		constexpr Logging::Format<int> format{ "record entry {}" };
		let view = format.View();
		const int arg{ 5 };
		let args = fmt::make_format_args( arg );
		Logging::Record emitted{ ELogLevel::Information, ELogTags::Test, SRCE_CUR, view, args };
		ASSERT_TRUE( emitted.Format() ) << "no format for SpdLog to format from";
		let& e = emitted.Entry();
		EXPECT_EQ( &emitted.Entry(), &e ) << "the entry was built again";
		EXPECT_EQ( e.Message(), "record entry 5" );
		EXPECT_EQ( e.Id(), format.Id );

		Logging::Entry built{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "record wrapped {}", 1 };
		Logging::Record forwarded{ built, 7, 8 };
		EXPECT_EQ( &forwarded.Entry(), &built );
		EXPECT_FALSE( forwarded.Format() );
		EXPECT_EQ( forwarded.AppPK, 7u );
		EXPECT_EQ( forwarded.InstancePK, 8u );
	}

	//fmt refuses some values only at run time:  a null char pointer.  It is stored as the error, not thrown out of Entry's
	//noexcept constructor.  A spec is dropped first, in case only the spec was refused.
	TEST_F( LogEntryTests, AnArgumentFmtRefusesIsStoredAsTheError ){
		const char* null{};
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, "refused {} {:>4} {}", null, null, 1 };
		const string nullError{ "<format error: string pointer is null>" };
		ASSERT_EQ( e.Arguments.size(), 3u );
		EXPECT_EQ( e.Arguments[0], Logging::Argument{nullError} );
		EXPECT_EQ( e.Arguments[1], Logging::Argument{nullError} ) << "without its spec, it still fails";
		EXPECT_EQ( e.Arguments[2], Logging::Argument{int64_t{1}} );
		EXPECT_EQ( e.Message(), Ƒ("refused {} {} 1", nullError, nullError) );
	}

	//A source_location's file and function are hashed once per call site, cached by the name's address.  The address is the
	//key, not the text, so an unloaded library's names have to be forgotten (Process::FreeLibrary).
	TEST_F( LogEntryTests, SourceIdsAreCachedByAddress ){
		char name[]{ "first name" };
		let first = Logging::SourceId( name );
		EXPECT_EQ( first, Logging::GenerateId("first name") );
		std::ranges::copy( sv{"other name"}, name );
		EXPECT_EQ( Logging::SourceId(name), first ) << "cached by address, so the new text is not hashed";
		Logging::ForgetSourceIds();
		EXPECT_EQ( Logging::SourceId(name), Logging::GenerateId("other name") ) << "forgotten, then hashed again";
		Logging::ForgetSourceIds();//a stack address, which later frames reuse.
	}
	//reviews-issue/log-pass2.md #2:  an entry off the wire with a line but no file or function gives spdlog empty names, not the
	//null pointers its source flags would strlen.
	TEST_F( LogEntryTests, ASourcelessEntryGivesSpdlogEmptyNames ){
		Logging::Entry e{ ELogLevel::Information, ELogTags::Test, 12, TimePoint{}, Jde::UserPK{}, Logging::SourceName{string{"no source"}}, {}, {}, {} };
		let source = e.SourceLocation();
		ASSERT_NE( source.filename, nullptr );
		ASSERT_NE( source.funcname, nullptr );
		std::ostringstream os;
		spdlog::logger spd{ "sourceless", std::make_shared<spdlog::sinks::ostream_sink_st>(os) };
		spd.set_pattern( "[%g][%s][%!][%@] %v" );
		spd.log( source, spdlog::level::info, e.Message() );
		EXPECT_EQ( os.str(), "[][][][:12] no source\n" );
	}

	//the overload the wire path uses: entries received from another instance are already built, so they arrive through
	//Log(const Entry&) rather than the variadic Log, and it is this one that has to route them to every logger - and
	//to apply each logger's own level, which the cumulative check before it does not.
	TEST_F( LogEntryTests, LogEntryReachesTheLoggers ){
		Logging::Entry e{ SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{"entry to the loggers {}"}, vector<Logging::Argument>{"1"} };
		Logging::Log( e );
		let found = Logging::Find( e.Id() );
		ASSERT_EQ( found.size(), 1u );
		EXPECT_EQ( found[0].Message(), "entry to the loggers 1" );
		EXPECT_EQ( found[0].Level, ELogLevel::Information );
		EXPECT_EQ( found[0].Tags, ELogTags::Test );

		//the suite's capture is configured Debug while the spd logger takes test at Trace, so the cumulative admits this
		//and the per-logger check is the only thing that can keep it out.
		Logging::ClearMemory();
		Logging::Entry belowThisLogger{ SRCE_CUR, ELogLevel::Trace, ELogTags::Test, string{"entry below the capture"} };
		ASSERT_TRUE( Logging::ShouldLog(ELogLevel::Trace, ELogTags::Test) ) << "the cumulative must admit it, or the per-logger pin proves nothing";
		ASSERT_FALSE( Logging::Captures(ELogLevel::Trace, ELogTags::Test) ) << "the capture must refuse it, or a failure here is the config, not the library";
		Logging::Log( belowThisLogger );
		EXPECT_TRUE( Logging::Find(belowThisLogger.Id()).empty() ) << "Log(Entry) skipped the per-logger level and wrote a Trace into a Debug logger";
	}

	//reviews-issue/MemoryLog.md #2:  Find ran its predicate under the capture's lock, so a predicate that logged waited on
	//itself, and Message() logs a template its arguments do not fit.  The capture renders an entry as it keeps it now.
	TEST_F( LogEntryTests, AFindPredicateMayLog ){
		Logging::Log( Logging::Entry{SRCE_CUR, ELogLevel::Information, ELogTags::Test, string{"predicate target {} {}"}, vector<Logging::Argument>{"1"}} );
		let badFormats = []{ return Logging::Find( [](let& e){ return e.Text().starts_with("Bad Format"); } ).size(); };
		let rendered = badFormats();
		EXPECT_GT( rendered, 0u ) << "the capture did not render the entry as it kept it";
		let found = Logging::Find( [](let& e){
			if( e.Text()=="predicate target {} {}" )
				INFOT( ELogTags::Test, "logged from a predicate" );
			return e.Message().starts_with( "predicate target" );
		});
		EXPECT_EQ( found.size(), 1u );
		EXPECT_EQ( Logging::Find([](let& e){ return e.Text()=="logged from a predicate"; }).size(), 1u ) << "the predicate's line was not kept";
		EXPECT_EQ( badFormats(), rendered ) << "a predicate's Message() rendered a kept entry again";
	}

	//LogOnce is `if( gate && MarkLogged(format.Id) ) Emit(...)`, the template's md5, and _loggedEntries is process-global with no clear - so
	//a --gtest_repeat pass cannot re-arm a literal, and an "exactly one entry" assertion would hold only on the first.
	//Split accordingly: this pins the predicate with an id made unique per pass, LogOnceRespectsAnAlreadyLoggedId pins
	//that LogOnce honours a false answer, and LogOnceLogsAtMostOnce that it logs on the pass that claims the id.
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
		EXPECT_TRUE( Logging::Find(string{text}).empty() ) << "LogOnce logged an id that was already marked";
	}

	//LogOnce gates before it claims the id, so a line below every logger's level stays unclaimed and logs the first time it
	//would be seen.  The id is claimed here on the first pass, which is all a repeat pass can then check.
	TEST_F( LogEntryTests, LogOnceClaimsOnlyALoggedLine ){
		static uint32_t pass{};
		++pass;
		ASSERT_FALSE( Logging::ShouldLog(ELogLevel::Trace, ELogTags::Locks) ) << "a logger takes locks at Trace, so the line would log";
		Logging::LogOnce( SRCE_CUR, ELogLevel::Trace, ELogTags::Locks, "LogEntryTests LogOnce below every level" );
		EXPECT_EQ( Logging::MarkLogged(Logging::GenerateId("LogEntryTests LogOnce below every level")), pass==1 ) << "LogOnce claimed a line nothing logged";
	}

	//One break rule for every path, by bit:  the framework's tags break, a library's never, whatever the magnitude.  The two
	//rules it replaced disagreed on the first two.
	static_assert( BreaksFor(ELogTags::SocketServerWrite) );//past Write by magnitude, which Log(Entry) didn't break for.
	static_assert( !BreaksFor((ELogTags)(1ull << LibraryTagBase)) );//a library's tag alone, which the variadic Log broke for.
	static_assert( !BreaksFor(ELogTags::App | (ELogTags)(1ull << (LibraryTagBase+3))) );
	static_assert( BreaksFor(ELogTags::None) );

	//the finding's own case.  One entry on the pass that claims the id, none on a repeat pass, where it is claimed already -
	//an EXPECT_LE accepted a LogOnce that never logged at all.  The MarkLogged check fails unless LogOnce reached the
	//de-dup set.
	TEST_F( LogEntryTests, LogOnceLogsAtMostOnce ){
		static uint32_t pass{};
		++pass;
		constexpr sv text{ "LogEntryTests LogOnce emitted" };
		Logging::LogOnce( SRCE_CUR, ELogLevel::Information, ELogTags::Test, "LogEntryTests LogOnce emitted" );
		Logging::LogOnce( SRCE_CUR, ELogLevel::Information, ELogTags::Test, "LogEntryTests LogOnce emitted" );
		EXPECT_EQ( Logging::Find(string{text}).size(), pass==1 ? 1u : 0u ) << "one entry on the pass that claims the id, none after";
		EXPECT_FALSE( Logging::MarkLogged(Logging::Entry::GenerateId(text)) ) << "LogOnce did not mark the id it logged";
	}
}