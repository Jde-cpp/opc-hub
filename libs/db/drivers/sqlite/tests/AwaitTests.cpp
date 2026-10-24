#include <jde/db/Row.h>
#include <jde/db/DBException.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Table.h> //complete type: SelectEnum takes const Table& and GetTablePtr yields sp<Table>.
#include <jde/db/meta/Cluster.h>
#include <jde/access/Authorize.h>
#include <jde/fwk/co/BlockAwait.h>

#define let const auto

//#62: the async awaitables had no coverage in libs/db's own tree.  They were not *unexercised* - Schema::Create runs
//SyncData -> LocalQL::Upsert -> the QL awaits -> QueryAwait/SqliteQueryAwait, so a crash on the happy path would fail
//every test - but nothing asserted a result, a NULL, or the type of a failure.  This suite drives each awaitable
//through BlockAwait and asserts what comes back, including the cases earlier findings turned on.
namespace Jde::DB::Sqlite::Tests{
	struct AwaitTests : BackendTests{};
	INSTANTIATE_BACKENDS( AwaitTests );

	α select( sp<IDataSource> ds, string sql )ε->vector<Row>{ return BlockAwait<SelectAwait,vector<Row>>( ds->SelectAsync(DB::Sql{move(sql)}) ); }

	TEST_P( AwaitTests, SelectAsyncReturnsRows ){
		let rows = select( _ds, "select 1 union all select 2 union all select 3" );
		ASSERT_EQ( rows.size(), 3u );
		EXPECT_EQ( rows[0].Get<uint>(0), 1u );
		EXPECT_EQ( rows[2].Get<uint>(0), 3u );

		EXPECT_TRUE( select(_ds, "select 1 where 1=0").empty() ); //no rows is not an error.
	}

	//#11: the relay must hand the caller the driver's exception, not a flattened Jde::Exception.
	TEST_P( AwaitTests, SelectAsyncFailureKeepsTheDriverException ){
		try{
			select( _ds, "select id from zz_62_no_such_table" );
			ADD_FAILURE() << "expected a throw";
		}
		catch( const DBException& e ){ EXPECT_EQ( (uint)e.Error, (uint)EDbError::Syntax ); } //sqlite: no such table -> Syntax.
	}

	TEST_P( AwaitTests, ExecuteAwaitReportsRowsAffected ){
		let exec = [&]( string sql ){ return BlockAwait<ExecuteAwait,uint32_t>( _ds->Execute(DB::Sql{move(sql)}) ); };
		exec( "drop table if exists zz_62" );
		exec( "create table zz_62( c integer )" );
		EXPECT_EQ( exec("insert into zz_62 values(1),(2)"), 2u );
		EXPECT_EQ( exec("update zz_62 set c=c+1"), 2u );
		EXPECT_EQ( exec("delete from zz_62 where c=99"), 0u ); //nothing matched is 0, not an error.
		exec( "drop table zz_62" );
	}

	//#53: a NULL cell resumes nullopt, and the non-Opt form reports the missing value rather than answering 0.
	TEST_P( AwaitTests, ScalerAndScalerOpt ){
		let opt = [&]( string sql ){ return BlockAwait<ScalerAwaitOpt<uint>,optional<uint>>( _ds->ScalerOpt<uint>(DB::Sql{move(sql)}) ); };
		let one = [&]( string sql ){ return BlockAwait<ScalerAwait<uint>,uint>( _ds->Scaler<uint>(DB::Sql{move(sql)}) ); };

		EXPECT_EQ( one("select 5"), 5u );
		EXPECT_EQ( opt("select 5").value_or(0), 5u );
		EXPECT_FALSE( opt("select null").has_value() );
		EXPECT_FALSE( opt("select 1 where 1=0").has_value() );
		EXPECT_THROW( one("select null"), Exception );
		EXPECT_THROW( one("select 1 where 1=0"), Exception );

		//C4/B5: ScalerAwait<uint32> once was an explicit specialisation - it exists so IAwait<uint32,...> is exported once -
		//that carried a verbatim copy of the template's Execute body; it is now the same template on fwk's UInt32Await base
		//(ScalerBase<T>).  Same body, same answers - and this keeps the uint32 instantiation exercised.
		let one32 = [&]( string sql ){ return BlockAwait<ScalerAwait<uint32>,uint32>( _ds->Scaler<uint32>(Sql{move(sql)}) ); };
		EXPECT_EQ( one32("select 5"), 5u );
		EXPECT_THROW( one32("select null"), Exception );        //the shared "No value returned" mapping.
		EXPECT_THROW( one32("select 1 where 1=0"), Exception );
	}

	//InsertSeq dispatches to the native twin, whose OUT row is the only source of the new pk (see InsertSeqSyncThroughProcTwin).
	TEST_P( AwaitTests, InsertSeqThroughProcTwinAndDuplicate ){
		let insert = []( string slug ){
			return DB::InsertClause{ "access_identity_insert",
				vector<Value>{Value{string{"zz_62"}}, Value{}, Value{move(slug)}, Value{}, Value{}, Value{false}, Value{}} };
		};
		let seq = [&]( string slug ){ return BlockAwait<ScalerAwait<uint>,uint>( _ds->InsertSeq<uint>(insert(move(slug))) ); };

		let id1 = seq( "zz_62_a" );
		let id2 = seq( "zz_62_b" );
		EXPECT_GT( id1, 0u );
		EXPECT_EQ( id2, id1+1 ); //each call gets its own pk - not the shared rows-affected of 1.

		try{ //the unique natural key on slug: a duplicate has to arrive classified, not as a bare Exception.
			seq( "zz_62_a" );
			ADD_FAILURE() << "expected a duplicate";
		}
		catch( const DBException& e ){
			EXPECT_EQ( (uint)e.Error, (uint)EDbError::Duplicate );
			EXPECT_EQ( e.Code(), 2067u ); //SQLITE_CONSTRAINT_UNIQUE - the extended code, which needs extended_result_codes on.
		}
		_ds->ExecuteSync( DB::Sql{"delete from access_identities where name='zz_62'"} );
	}

	//SelectEnum -> CacheAwait: a hit, a miss that populates, and a failure that is catchable rather than a terminate (#4).
	TEST_P( AwaitTests, SelectEnumHitMissAndFailure ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto table = schema->GetTablePtr( "provider_types" ); //Names::FromJson converts the meta key, so it is not "providerTypes" here.
		ASSERT_TRUE( table );

		let load = [&]{ return BlockAwait<CacheAwait<flat_map<uint,string>>,flat_map<uint,string>>( _ds->SelectEnum<uint,string>(*table) ); };
		let first = load();                       //miss: reads the table and caches it.
		EXPECT_GT( first.size(), 0u );            //access.mutation seeds seven provider types.
		EXPECT_EQ( load().size(), first.size() ); //hit: the same answer from cache.

		//a table that does not exist must surface as an exception at the BlockAwait, not end the process.
		EXPECT_THROW( (BlockAwait<CacheAwait<flat_map<uint,string>>,flat_map<uint,string>>(
			_ds->SelectMap<uint,string>(DB::Sql{"select id, name from zz_62_no_such_table"}, "zz_62_no_such_table") )), Exception );
	}
}
