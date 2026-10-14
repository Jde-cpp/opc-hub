#include "gtest/gtest.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/settings.h>
#include <jde/db/db.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/tests/testMain.h>
#include "globals.h"

#define let const auto

namespace Jde{
#ifndef _MSC_VER
	α Process::ProductName()ι->sv{ return "Tests.QL"; }
#endif
	Ω startup( int argc, char **argv )ε->void{
		Process::Startup( argc, argv, Process::ProductName(), "QL tests", true );
		Logging::Init();
		//#238: the qlTests schema, on the data source config/args/sqlite mounts - `:memory:` under ctest, which RequiresSync says is
		//created on every start.  A null authorizer:  Table::Authorize is a no-op on one, so the ops run unguarded (globals.h).
		auto schema = DB::GetAppSchema( "qlTests", nullptr );
		auto ql = ms<QL::Tests::DataSourceQL>( vector<sp<DB::AppSchema>>{schema} );
		if( Settings::FindBool("/dbServers/sync").value_or(false) || schema->DS()->RequiresSync() )
			DB::SyncSchema( *schema, ql );
		QL::Tests::SetQL( ql );
		QL::Tests::EnsureItemTypes();
	}
}

α main( int argc, char **argv )->int{
	using namespace Jde;
	::testing::InitGoogleTest( &argc, argv );
	int exitCode{ EXIT_FAILURE };
	try{
		startup( argc, argv );
		ApplyTestsFilter();
		exitCode = CheckTestsRan( RUN_ALL_TESTS() );
	}
	catch( runtime_error& e ){
		exitCode = StartupFailed( e );//never the exception's code:  main's status keeps only its low 8 bits.
	}
	Process::Shutdown( exitCode );
	return exitCode;
}