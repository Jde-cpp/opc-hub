
#include "jde/fwk/log/logTags.h"
#include "jde/fwk/str.h"
#include "jde/fwk/usings.h"
#include <string>
#define let const auto
namespace Jde{
	//Routes GoogleTest's own console output through the shared spdlog console sink instead of
	//writing straight to stdout, so it can't race/tear against log lines written from other
	//threads mid-test (spdlog's stdout_color_sink_mt only serializes writes made through it).
	struct SpdlogTestListener : ::testing::EmptyTestEventListener{
		Ω Config( ::testing::TestEventListeners& listeners ){
			delete listeners.Release( listeners.default_result_printer() );
			listeners.Append( new SpdlogTestListener{} );
		}
		α OnTestIterationStart( const ::testing::UnitTest& unit, int )->void override{
			INFOT( ELogTags::Test, "[==========] Running {} tests from {} test suites.", unit.test_to_run_count(), unit.test_suite_to_run_count() );
		}
		α OnEnvironmentsSetUpStart( const ::testing::UnitTest& )->void override{
			INFOT( ELogTags::Test, "[----------] Global test environment set-up." );
		}
		α OnTestSuiteStart( const ::testing::TestSuite& suite )->void override{
			INFOT( ELogTags::Test, "[----------] {} tests from {}", suite.test_to_run_count(), suite.name() );
		}
		Ω toEntry( const ::testing::TestInfo& info, ELogLevel level, sv format, vector<string>&& args )->Logging::Entry{
			return Logging::Entry{ level, ELogTags::Test, (uint32_t)info.line(), Clock::now(), {0}, string{format}, info.file(), info.name(), move(args) };
		}
		α OnTestStart( const ::testing::TestInfo& info )->void override{
			_currentTest = info.name();
			auto entry = toEntry( info, ELogLevel::Information, "[ RUN      ] {}.{}", {info.test_suite_name(), info.name()} );
			Logging::Log( entry );
		}
		//Called with UnitTest::mutex_ held - calling back into UnitTest (e.g. current_test_info()) self-deadlocks.
		α OnTestPartResult( const ::testing::TestPartResult& result )->void override{
			if( result.failed() ){
				Logging::Entry entry{ ELogLevel::Error, ELogTags::Test, (uint32_t)result.line_number(), Clock::now(), {0}, string{result.message()}, result.file_name() ? result.file_name() : "unknown file", _currentTest.empty() ? "unknown" : _currentTest, {} };
				Logging::Log( entry );
			}
		}
		α OnTestEnd( const ::testing::TestInfo& info )->void override{
			_currentTest.clear();
			let ms = std::to_string( info.result()->elapsed_time() );
			auto level = ELogLevel::Information;
			sv format = "[       OK ] {}.{} ({} ms)";
			if( info.result()->Skipped() ){
				level = ELogLevel::Warning;
				format = "[  SKIPPED  ] {}.{} ({} ms)";
			}	else if( info.result()->Failed() ){
				level = ELogLevel::Error;
				format = "[  FAILED  ] {}.{} ({} ms)";
			}
			Logging::Log( toEntry(info, level, format, {info.test_suite_name(), info.name(), ms}) );
		}
		α OnTestSuiteEnd( const ::testing::TestSuite& suite )->void override{
			INFOT( ELogTags::Test, "[----------] {} tests from {} ({} ms total)", suite.test_to_run_count(), suite.name(), suite.elapsed_time() );
		}
		α OnEnvironmentsTearDownStart( const ::testing::UnitTest& )->void override{
			INFOT( ELogTags::Test, "[----------] Global test environment tear-down" );
		}
		α OnTestIterationEnd( const ::testing::UnitTest& unit, int )->void override{
			INFOT( ELogTags::Test, "[==========] {} tests from {} test suites ran. ({} ms total)", unit.test_to_run_count(), unit.test_suite_to_run_count(), unit.elapsed_time() );
			INFOT( ELogTags::Test, "[  PASSED  ] {} tests.", unit.successful_test_count() );
			if( let failed = unit.failed_test_count() ){
				ERRT( ELogTags::Test, "[  FAILED  ] {} tests, listed below:", failed );
				for( auto i=0; i<unit.total_test_suite_count(); ++i ){
					let& suite = *unit.GetTestSuite( i );
					for( auto j=0; j<suite.total_test_count(); ++j ){
						let& test = *suite.GetTestInfo( j );
						if( test.result()->Failed() )
							ERRT( ELogTags::Test, "[  FAILED  ] {}.{}", suite.name(), test.name() );
					}
				}
			}
		}
	private:
		string _currentTest;//benign race if an assertion fires off the main thread - logging only.
	};
}
#undef let