#pragma once
#ifndef JDE_TEST_MAIN_H
#define JDE_TEST_MAIN_H

#include <cstdlib>
#include <iostream>
#include "jde/fwk/exceptions/Exception.h"
#include "jde/fwk/log/log.h"
#include "jde/fwk/log/logTags.h"
#include "jde/fwk/process/cpu.h" // IWYU pragma: keep - its linux start-up check, for every test exe:  only a test's main.cpp includes this header.
#include "jde/fwk/process/process.h"
#include "jde/fwk/settings.h"
#include "jde/tests/SpdlogTestListener.h"
#define let const auto

namespace Jde{
	//'/testing/tests' is the suite's default filter, so it yields to one chosen any other way - --gtest_filter, GTEST_FILTER
	//or a --gtest_flagfile all set the flag before this runs, off gtest's own "*".  The argv check keeps an explicit
	//--gtest_filter=* from losing to the setting.  Call after InitGoogleTest, once Settings are loaded.
	Ξ ApplyTestsFilter()ι->void{
		let& args = Process::Args();
		if( ::testing::GTEST_FLAG(filter)=="*" && args.find("--gtest_filter")==args.end() )
			::testing::GTEST_FLAG( filter ) = Settings::FindSV( "/testing/tests" ).value_or( "*" );
	}

	//A gtest filter that matches nothing is not an error to gtest - RUN_ALL_TESTS() runs no test and returns 0,
	//so a suite that was renamed out from under its '/testing/tests' setting (or never written) reports green
	//having tested nothing.  Wrap the RUN_ALL_TESTS() call:  result = CheckTestsRan( RUN_ALL_TESTS() );
	//Only meaningful afterwards - test_to_run_count() is populated when gtest applies the filter, inside Run().
	Ξ CheckTestsRan( int result )ι->int{
		let& unit = *::testing::UnitTest::GetInstance();
		if( unit.test_to_run_count() )
			return result;
		CRITICALT( ELogTags::Test, "gtest filter '{}' matched none of the {} tests - nothing ran.", ::testing::GTEST_FLAG(filter), unit.total_test_count() );
		return EXIT_FAILURE;
	}

	//A startup exception is a failed run, whatever code it carries:  `exitCode = (int)e.Code()` reads like it reports the
	//status, but the OS keeps only the low 8 bits of what main returns.  Every UA status code is 0xXXXX0000 and every
	//tag-only Exception has code 0, so a bad settings file, certificate or log-tag parse exited **0** - and addJdeTest
	//sets no PASS_REGULAR_EXPRESSION, so ctest reported the suite PASSED with zero tests run.  That is the same hole
	//CheckTestsRan closes on the other side of RUN_ALL_TESTS.  The code is worth logging, not returning.
	//Use as:  catch( runtime_error& e ){ exitCode = StartupFailed( e ); }
	Ξ StartupFailed( const std::exception& e )ι->int{
		if( auto p = dynamic_cast<const Exception*>(&e); p ){
			p->Log();
			CRITICALT( ELogTags::Test, "Startup failed{} - exiting {}.", p->HasCode() ? Ƒ(" with code 0x{:x}", p->Code()) : string{}, EXIT_FAILURE );
		}
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}
namespace Jde::Tests{
	//Every suite's main:  gtest's flags, `startup`, the '/testing/tests' filter and the run, a failed startup a failed run.
	//`listener` sends gtest's console output through spdlog (SpdlogTestListener).
	Ŧ Main( int argc, char** argv, T&& startup, bool listener=true )ι->int{
		::testing::InitGoogleTest( &argc, argv );
		int exitCode{ EXIT_FAILURE };
		try{
			startup( argc, argv );
			ApplyTestsFilter();
			if( listener )
				SpdlogTestListener::Config( ::testing::UnitTest::GetInstance()->listeners() );
			exitCode = CheckTestsRan( RUN_ALL_TESTS() );
		}
		catch( const std::exception& e ){
			exitCode = StartupFailed( e );
		}
		Process::Shutdown( exitCode );
		return exitCode;
	}
}
#undef let
#endif
