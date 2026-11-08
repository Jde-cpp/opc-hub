#ifndef _WIN32
#include <thread>
#include <unistd.h>
#include <jde/fwk/process/process.h>

namespace Jde::Tests{
	//A stop can't be undone, so each of these runs in gtest's forked child; *DeathTest suites run first, while the parent's
	//threads are idle.  The alarm is the failure:  its default action kills a Pause that never wakes, where the signal handler would end it.
	[[noreturn]] Ω exitOnPause()ι->void{
		::signal( SIGALRM, SIG_DFL );
		::alarm( 10 );
		std::_Exit( Process::Pause() );
	}

	TEST( ProcessDeathTest, PauseSeesASignalFromBeforeIt ){
		EXPECT_EXIT( { ::raise( SIGTERM ); exitOnPause(); }, testing::ExitedWithCode(EXIT_SUCCESS), "" );
	}
	TEST( ProcessDeathTest, PauseWakesOnASignalWhileWaiting ){
		EXPECT_EXIT( {
			std::thread{ []{ std::this_thread::sleep_for( 50ms ); ::kill( ::getpid(), SIGTERM ); } }.detach();
			exitOnPause();
		}, testing::ExitedWithCode(EXIT_SUCCESS), "" );
	}
	TEST( ProcessDeathTest, PauseReportsAnythingButAStopAsFailure ){
		EXPECT_EXIT( { ::raise( SIGUSR1 ); exitOnPause(); }, testing::ExitedWithCode(255), "" );
	}
	TEST( ProcessDeathTest, FirstExitCauseWins ){
		EXPECT_EXIT( { ::raise( SIGUSR1 ); ::raise( SIGTERM ); exitOnPause(); }, testing::ExitedWithCode(255), "" );
	}
}
#endif