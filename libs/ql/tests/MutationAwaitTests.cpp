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

	//Start/Stop are hook-implemented and resolve no table by design, so they must not be caught by the guard:  with no hook
	//registered the fan-out is empty and the mutation returns null instead of throwing.
	TEST( MutationAwaitTests, StartAndStopWithNullTableDoNotThrow ){
		auto ql = ms<NullQL>();
		EXPECT_TRUE( mutate("startStatus", jobject{}, ql).is_null() );
		EXPECT_TRUE( mutate("stopStatus", jobject{}, ql).is_null() );
	}

	//#33:  Execute is the third type the guard has to let past - not because a hook answers it, but because it has its own
	//message.  Nothing in any suite had ever sent one.
	TEST( MutationAwaitTests, ExecuteSaysItIsNotImplemented ){
		auto ql = ms<NullQL>();
		try{
			mutate( "executeStatus", jobject{}, ql );
			FAIL() << "expected a throw";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("Execute mutation not implemented"), string::npos ) << e.what(); //not "Table not found".
		}
	}
}
