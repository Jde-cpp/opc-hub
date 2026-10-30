//review3 #1:  MutationQL resolves no DBTable for a system-shaped or empty name ("createStatus", "create"), and MutationAwait
//handed that null straight to InsertAwait/AddRemoveAwait/PurgeAwait, each of which authorizes through it - Table::Authorize reads
//`this` - so an unauthenticated `/graphql` mutation segfaulted the app server.  Only UpdateAwait guarded.  NullQL claims no
//mutation, which is what lets these reach the stock crud ops; no data source is involved, and Resume/ResumeExp are synchronous,
//so BlockAwait drives the coroutine on this thread.
#include <gtest/gtest.h>
#include <jde/fwk/co/BlockAwait.h>
#include <jde/ql/ops/MutationAwait.h>
#include "NullQL.h"

#define let const auto

namespace Jde::QL::Tests{
	Ω mutate( sv command, jobject args, sp<NullQL> ql )ε->jvalue{
		const vector<sp<DB::AppSchema>> noSchemas;
		MutationQL mutation{ string{command}, move(args), ms<jobject>(), {}, false, noSchemas, true }; //system -> DBTable is null.
		return BlockAwait<MutationAwait,jvalue>( MutationAwait{move(mutation), Creds{UserPK{UserPK::System}}, sp<IQL>{ql}, SRCE_CUR} );
	}
	Ω expectNoTable( sv command, jobject args )ε->void{
		auto ql = ms<NullQL>();
		try{
			mutate( command, move(args), ql );
			FAIL() << Ƒ( "expected a throw from '{}'", command );
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("Table not found"), string::npos ) << e.what();
			EXPECT_NE( string{e.what()}.find(command), string::npos ) << e.what(); //the mutation is named, so the log says which one.
		}
		EXPECT_EQ( ql->CustomMutationCount, 1u ); //the custom hook still gets its chance before the guard.
	}

	TEST( MutationAwaitTests, CreateWithNullTableThrows ){ expectNoTable( "createStatus", jobject{{"name","x"}} ); }
	TEST( MutationAwaitTests, PurgeWithNullTableThrows ){ expectNoTable( "purgeStatus", jobject{{"id",1}} ); }
	TEST( MutationAwaitTests, AddWithNullTableThrows ){ expectNoTable( "addLogs", jobject{{"id",1},{"memberId",2}} ); }
	TEST( MutationAwaitTests, RemoveWithNullTableThrows ){ expectNoTable( "removeLogs", jobject{{"id",1},{"memberId",2}} ); }
	TEST( MutationAwaitTests, EmptyTableNameThrows ){ expectNoTable( "create", jobject{} ); }
	//the pre-existing UpdateAwait guard, unchanged:  the same message, from the other side of the switch.
	TEST( MutationAwaitTests, UpdateWithNullTableStillThrows ){ expectNoTable( "updateStatus", jobject{{"id",1},{"name","x"}} ); }

	//Start, Stop and Execute resolve no table by design and have nothing stock behind them:  only an IQL's CustomMutation answers
	//one (stopApplicationInstance, App::Server).  So the guard lets them past, and the refusal names the verb, not a table (#33).
	TEST( MutationAwaitTests, StartStopExecuteSayTheyHaveNoHandler ){
		auto ql = ms<NullQL>();
		for( let command : {"startStatus", "stopStatus", "executeStatus"} ){
			try{
				mutate( command, jobject{}, ql );
				FAIL() << command << ":  expected a throw";
			}
			catch( const Exception& e ){
				EXPECT_NE( string{e.what()}.find(Ƒ("'{}' has no handler", command)), string::npos ) << e.what(); //not "Table not found".
			}
		}
	}
}