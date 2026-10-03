#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/co/Timer.h>
#include <jde/db/db.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/ql/ql.h>
#include <jde/access/Authorize.h> //!
#include <jde/access/server/accessServer.h>
#include <jde/access/AccessListener.h>
#include <jde/tests/testMain.h>
#include "globals.h"
#include "AccessQL.h"
#include <atomic>
#include <iostream>
#include <thread>

#define let const auto

namespace Jde{
	sp<Access::AccessListener> _listener;
	α configureAndProbe( VoidAwait&& a, sp<BlockAwaitState<std::monostate>> s )ι->VoidAwait::Task; //below Startup, where it is explained.
#ifndef _WIN32
	α Process::ProductName()ι->sv{ return "Tests.Access"; }
#endif
 	α Startup( int argc, char **argv )ε->void{
#ifdef _MSC_VER
		ASSERT( Settings::FindNumber<uint>("/workers/drive/threadSize").value_or(5)>0 )
#endif
		Process::Startup( argc, argv, Process::ProductName(), "Access tests", true );
		Logging::Init();
		let metaDataName{ "access" };
		sp<Access::Authorize> authorizer = Access::Tests::Authorizer();
		auto schema = DB::GetAppSchema( metaDataName, authorizer );
		auto ql = ms<Access::Tests::AccessQL>( vector<sp<DB::AppSchema>>{schema}, authorizer );
		_listener = ms<Access::AccessListener>( ql );
		if( Settings::FindBool("/testing/recreateDB").value_or(false) )
			DB::NonProd::Recreate( *schema, ql );
		else if( Settings::FindBool("/dbServers/sync").value_or(false) || schema->DS()->RequiresSync() )
			DB::SyncSchema( *schema, ql );
		auto await = Access::Server::Configure( {schema}, ql, UserPK{UserPK::System}, authorizer, _listener );
		let sl = await.Source();
		auto state = ms<BlockAwaitState<std::monostate>>();
		configureAndProbe( move(await), state );
		state->Wait( sl );
		if( state->Error )
			state->Error->Throw();
		Access::Tests::SetQL( ql );
	}
	//access-review3 #20:  BlockVoidAwait's glue, plus a probe.  On the local-QL path every awaitable in Configure's Subscribe stage
	//completes in await_ready, so this continuation runs inline on the thread that finished it - the thread Loader::Acl used to
	//leave holding the authorizer's unique lock across Subscribe.  An Authorize read here then self-deadlocked (shared_mutex is
	//not recursive); the watchdog turns that into an abort with a message rather than a silent hang of the whole suite.
	α configureAndProbe( VoidAwait&& a, sp<BlockAwaitState<std::monostate>> s )ι->VoidAwait::Task{
		try{
			co_await a;
			std::atomic<bool> done{ false };
			std::jthread watchdog{ [&done]{
				for( int i=0; i<50 && !done; ++i )
					std::this_thread::sleep_for( std::chrono::milliseconds{100} );
				if( !done ){
					std::cerr << "access-review3 #20: Configure's continuation cannot read Authorize - Configure is holding the mutex across Subscribe." << std::endl;
					std::abort();
				}
			} };
			(void)Access::Tests::Authorizer()->Rights( "access", "users", UserPK{1} );
			done = true;
		}
		catch( Exception& e ){
			s->Error = e.Move();
		}
		s->Signal();
	}
}

α main( int argc, char **argv )->int{
	using namespace Jde;
	let filterSet = Process::Args().find( "--gtest_filter" )!=Process::Args().end();
	::testing::InitGoogleTest( &argc, argv );
	int exitCode{ EXIT_FAILURE };
	try{
		Startup( argc, argv );
		if( !filterSet )
			::testing::GTEST_FLAG( filter ) = Settings::FindSV( "/testing/tests" ).value_or( "*" );
		exitCode = CheckTestsRan( RUN_ALL_TESTS() );
	}
	catch( runtime_error& e ){
		exitCode = StartupFailed( e );//never the exception's code:  main's status keeps only its low 8 bits.
	}
	INFOT( ELogTags::App, "Shutting down with exit code {}.", exitCode );
	Process::Shutdown( exitCode );
	std::cout << "Exited with code: " << exitCode << std::endl;
	return exitCode;
}