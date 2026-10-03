#pragma once
#ifndef JDE_TEST_MAIN_H
#define JDE_TEST_MAIN_H

#include <cstdlib>
#include <iostream>
#include "jde/fwk/exceptions/Exception.h"
#include "jde/fwk/log/log.h"
#include "jde/fwk/log/logTags.h"
#include "jde/fwk/process/cpu.h" // IWYU pragma: keep - its linux start-up check, for every test exe:  only a test's main.cpp includes this header.
#define let const auto

namespace Jde{
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
#undef let
#endif
