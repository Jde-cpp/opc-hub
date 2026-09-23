#include <sqlite3.h>
#include "../src/SqliteProcs.h" //compiled into this slug - see CMakeLists.txt.
#include <jde/db/Row.h>
#include <jde/db/DBException.h>
#include <jde/db/IDataSource.h>
#include "jde/fwk/settings.h"
#include <jde/access/Authorize.h>

#define let const auto

namespace Jde::DB::Sqlite::Tests{
	struct OpTests : BackendTests{};
	INSTANTIATE_BACKENDS( OpTests );
		//TODO Test Data, Test Procs, Test FKs, Test Indexes, Test Triggers.

	//#12: sqlite3_open_v2 allocates a handle even when it fails, so a failed open used to leave _db set - every later
	//call skipped the `if( !_db )` reopen and reused the dead handle, wedging the data source until process restart
	//even once the cause was gone. Not backend-parameterized: it owns a data source pointed at an unopenable path.
	TEST( ConnectionTests, ReopensAfterFailedOpen ){
		auto ds = DS( "wedge" ); //configured at a path whose directory does not exist.
		EXPECT_ANY_THROW( ds->ExecuteSync(Sql{"select 1"}, SRCE_CUR) ) << "open must fail";

		//cause removed: the next call has to open afresh. Before the fix _db stayed set, so this reused the dead handle and threw.
		ds->SetConfig( jobject{ {"catalogs", jobject{ {"testDb", jobject{ {"path", ":memory:"}, {"schemas", jobject{}}}} }} } );
		EXPECT_NO_THROW( ds->ExecuteSync(Sql{"select 1"}, SRCE_CUR) );
	}

	//#45: sqlite installs no busy handler by default, so the moment a second connection holds the write lock the next
	//statement fails outright with SQLITE_BUSY -> EDbError::Timeout, which nothing above retries.  busyTimeoutMs (catalog
	//config, 5s by default) makes it wait for the lock instead.  busyHolder/busyWaiter share their own file, so the write
	//lock taken here is invisible to the rest of the suite.
	TEST( ConnectionTests, BusyTimeoutWaitsForAConcurrentWriter ){
		auto holder = DS( "busyHolder" );      //default timeout; only takes the lock.
		auto waiter = DS( "busyWaiter" );      //busyTimeoutMs: 500 - short enough to assert on.
		holder->ExecuteSync( Sql{"create table if not exists zz_45(c)"}, SRCE_CUR );

		holder->ExecuteSync( Sql{"begin immediate"}, SRCE_CUR ); //held until the commit below, whatever happens between.
		let start = std::chrono::steady_clock::now();
		string error;
		try{ waiter->ExecuteSync( Sql{"insert into zz_45 values(1)"}, SRCE_CUR ); }
		catch( const Exception& e ){ error = e.what(); }
		let waited = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()-start ).count();
		holder->ExecuteSync( Sql{"commit"}, SRCE_CUR );

		EXPECT_NE( error.find("locked"), string::npos ) << error; //sqlite spells SQLITE_BUSY "database is locked".
		EXPECT_GE( waited, 400 ) << waited; //it waited out the 500ms rather than failing at once - without the handler this is ~0.
		EXPECT_LT( waited, 2000 ) << waited; //bounded well under the 5s default, so this also proves busyTimeoutMs was read.
	}

	//#46: RegisterProc replaced a same-named twin silently and UnregisterProcs erased by name alone - so when two
	//registrars held one name, whichever was destroyed *first* stripped the entry the survivor was still dispatching,
	//and its next call failed with "No native proc registered".  Entries carry their owner now, and only that owner can
	//remove them.  This runs against the copy of the registry compiled into the test binary, so the names below are
	//invisible to the driver's own.
	TEST( ProcRegistryTests, UnregisterOnlyRemovesTheOwnersEntries ){
		int ownerA{}, ownerB{}; //addresses only - the registry treats an owner as an opaque token.
		let body = []( uint result ){ return [result]( sqlite3&, const vector<Value>&, RowΛ*, SL )->uint{ return result; }; };
		sqlite3* db{};
		ASSERT_EQ( sqlite3_open(":memory:", &db), SQLITE_OK ); //a real handle: the bodies ignore it, but forming *nullptr would not be.
		let call = [&]( sv name ){ let p = FindProc(name); return p ? (*p)( *db, {}, nullptr, SRCE_CUR ) : 0u; };

		RegisterProc( "zz_46", body(1), 0, &ownerA );
		RegisterProc( "zz_46", body(2), 0, &ownerB ); //takes the name over; logs CRITICAL, which is the point of noticing.
		EXPECT_EQ( call("zz_46"), 2u );

		UnregisterProcs( {"zz_46"}, &ownerA ); //A's teardown: no longer A's entry, so it must be left alone.
		ASSERT_TRUE( FindProc("zz_46") );      //was erased here - the survivor lost a proc its dll was still serving.
		EXPECT_EQ( call("zz_46"), 2u );

		UnregisterProcs( {"zz_46"}, &ownerB ); //its actual owner does remove it.
		EXPECT_FALSE( FindProc("zz_46") );

		//an anonymous registration (ProcRegistry's default) owns itself: a tagged unregister must not take it either.
		RegisterProc( "zz_46b", body(3) );
		UnregisterProcs( {"zz_46b"}, &ownerA );
		EXPECT_TRUE( FindProc("zz_46b") );
		UnregisterProcs( {"zz_46b"} );
		EXPECT_FALSE( FindProc("zz_46b") );
		sqlite3_close_v2( db );
	}

	//#47: a non-Null outValue declares the *trailing placeholder* an OUT param, which is a proc convention.  MySQL keyed
	//it off outValue alone, so a plain statement had its last param silently replaced by 0 - and with no params the loop
	//bound `0 + -1` wrapped to SIZE_MAX and indexed an empty vector.  sqlite always bound every param for a non-proc;
	//this pins the contract both drivers now share, so the two cannot drift apart again.
	TEST_P( OpTests, ScalerSyncOnPlainSqlBindsEveryParam ){
		EXPECT_EQ( _ds->ExecuteScalerSync({"select ? + ?", {Value{40}, Value{2}}}, EValue::UInt64).get_number<uint>(), 42u ); //42, not 40: the last param is a value, not an OUT slot.
		EXPECT_EQ( _ds->ExecuteScalerSync({"select 7"}, EValue::UInt64).get_number<uint>(), 7u );                            //and no params at all is legitimate on plain SQL.
	}

	TEST_P( OpTests, InsertSelectRoundTrip ){
		let now = DBTimePoint{ std::chrono::floor<std::chrono::seconds>(DBClock::now()) };
		//provider_id left null - it has an fk to access_providers, and no provider row is seeded here.
		_ds->ExecuteSync( {"insert into access_identities( name, slug, is_group, attributes, description, created ) values( ?, ?, ?, ?, ?, ? )",
			{Value{"alice"}, Value{"alice@example.com"}, Value{true}, Value{(uint)42}, Value{}, Value{now}}} );

		let rows = _ds->Select( {"select name, slug, is_group, attributes, description, created from access_identities where name=?", {Value{"alice"}}} );
		ASSERT_EQ( rows.size(), 1u );
		let& r = rows[0];
		EXPECT_EQ( r.GetString(0), "alice" );
		EXPECT_EQ( r.GetString(1), "alice@example.com" );
		EXPECT_TRUE( r.GetBit(2) ); //declared 'bit' comes back as Bool, not int.
		EXPECT_EQ( r.Get<uint>(3), 42u );
		EXPECT_TRUE( r.IsNull(4) ); //description not supplied.
		EXPECT_EQ( r.Get<DBTimePoint>(5), now ); //declared 'datetime' comes back as Time from the stored epoch int.
	}

	TEST_P( OpTests, IdentityAndReturning ){
		let id1 = _ds->ExecuteScalerSync( {"insert into access_identities( name, slug ) values( ?, ? ) returning identity_id", {Value{"bob"}, Value{"bob@example.com"}}}, EValue::UInt64 ).get_number<uint>();
		let id2 = _ds->ExecuteScalerSync( {"insert into access_identities( name, slug ) values( ?, ? ) returning identity_id", {Value{"carol"}, Value{"carol@example.com"}}}, EValue::UInt64 ).get_number<uint>();
		EXPECT_EQ( id2, id1+1 ); //rowid alias auto-assigns - identity_id omitted from the insert.
		let last = _ds->ExecuteScalerSync( {"select last_insert_rowid()"}, EValue::UInt64 ).get_number<uint>();
		EXPECT_EQ( last, id2 );
	}

	TEST_P( OpTests, DefaultNowApplied ){
		_ds->ExecuteSync( {"insert into access_identities( name, slug ) values( ?, ? )", {Value{"dave"}, Value{"dave@example.com"}}} );
		let rows = _ds->Select( {"select created from access_identities where name=?", {Value{"dave"}}} );
		ASSERT_EQ( rows.size(), 1u );
		let created = rows[0].Get<DBTimePoint>( 0 ); //default (unixepoch()) - SqliteSyntax::NowDefault.
		let diff = std::chrono::abs( DBClock::now()-created );
		EXPECT_LT( diff, std::chrono::minutes{1} );
	}

	TEST_P( OpTests, NativeProcDispatch ){
		//access_permissions/access_roles created by Schema::Create. access_role_insert's twin inserts a permission row then the role,
		//returning role_id as the out param - callers are unaware there's no server proc (same Sql shape InsertClause::Proc generates).
		Sql call1{ "access_role_insert( ?, ?, ?, ?, ? )", {Value{"admin"}, Value{"admin"}, Value{}, Value{}, Value{}}, true };
		let role1 = _ds->ExecuteScalerSync( move(call1), EValue::UInt64 ).get_number<uint>();
		Sql call2{ "access_role_insert( ?, ?, ?, ?, ? )", {Value{"user"}, Value{"user"}, Value{}, Value{}, Value{}}, true };
		let role2 = _ds->ExecuteScalerSync( move(call2), EValue::UInt64 ).get_number<uint>();

		EXPECT_EQ( role2, role1+1 ); //permission_id rowid alias auto-assigns, reused as role_id.
		let roles = _ds->ExecuteScalerSync( {"select count(*) from access_roles"}, EValue::UInt64 ).get_number<uint>();
		EXPECT_EQ( roles, 2u );

		Sql unregistered{ "no_such_proc( ? )", {Value{(uint)1}}, true };
		EXPECT_THROW( _ds->ExecuteSync(move(unregistered)), Exception );
	}

	TEST_P( OpTests, InsertSeqSyncThroughProcTwin ){
		//InsertClause built from a proc name dispatches to a twin, so Execute returns ExecuteProc's rows-affected and
		//never reaches its last_insert_rowid line - the twin's out row is the only source of the new pk.  Pre-fix both
		//calls returned 1 (sqlite3_changes) and every caller silently shared one pk.
		//params: [0]=name, [1]=provider_id, [2]=slug, [3]=attributes, [4]=description, [5]=is_group, [6]=email.
		let insert = []( string name, string slug ){
			return DB::InsertClause{ "access_identity_insert",
				vector<Value>{Value{move(name)}, Value{}, Value{move(slug)}, Value{}, Value{}, Value{false}, Value{}} };
		};
		let id1 = _ds->InsertSeqSync<uint>( insert("erin", "erin@example.com") );
		let id2 = _ds->InsertSeqSync<uint>( insert("frank", "frank@example.com") );
		EXPECT_GT( id1, 0u );
		EXPECT_EQ( id2, id1+1 ); //not 1,1 - identity_id is a rowid alias that auto-assigns.

		let rows = _ds->Select( {"select name from access_identities where identity_id=?", {Value{id2}}} );
		ASSERT_EQ( rows.size(), 1u ); //the returned pk must address the row just written.
		EXPECT_EQ( rows[0].GetString(0), "frank" );
	}

	TEST_P( OpTests, ProcArityGuard ){
		//Twins index params[N] positionally with unchecked operator[]; a short vector used to read past the end and
		//copy a Value variant from uninitialized memory.  RegisterProc's minParams turns that into a diagnosable throw.
		Sql tooFew{ "access_identity_insert( ?, ? )", {Value{"short"}, Value{}}, true }; //declares 7 params
		EXPECT_THROW( _ds->ExecuteSync(move(tooFew)), Exception );
		//...and the extra trailing out-param placeholder callers append must still be accepted (minParams is a floor).
		Sql extra{ "access_permission_insert( ?, ? )", {Value{false}, Value{(uint)0}}, true }; //declares 1
		EXPECT_NO_THROW( _ds->ExecuteSync(move(extra)) );
	}

	TEST_P( OpTests, ProcsSurviveSiblingTeardown ){
		{ //A 2nd data source over the same proc dlls - the registry is process-global, so its teardown must not strip procs _ds still dispatches.
			let sibling = DB::DataSource( Settings::AsObject(Ƒ("/dbServers/{}", GetParam())) );
		}
		Sql call{ "access_role_insert( ?, ?, ?, ?, ? )", {Value{"survivor"}, Value{"survivor"}, Value{}, Value{}, Value{}}, true };
		EXPECT_GT( _ds->ExecuteScalerSync(move(call), EValue::UInt64).get_number<uint>(), 0u );
	}

	//#52: IServerMeta held an *owning* sp back to its data source, so DS -> up<ServerMeta> -> sp<DS> closed a cycle the
	//moment anything called ServerMeta().  Nothing ever reset it, so such a data source could not be destroyed however
	//many external owners let go - sqlite never reached sqlite3_close_v2 and its final WAL checkpoint, MySQL never closed
	//its sessions.  It is a reference now: the meta is a up<> member of the data source and cannot outlive it.
	TEST_P( OpTests, ServerMetaDoesNotOwnItsDataSource ){
		wp<IDataSource> weak;
		{
			let ds = DB::DataSource( Settings::AsObject(Ƒ("/dbServers/{}", GetParam())) ); //standalone, as ProcsSurviveSiblingTeardown builds one.
			weak = ds;
			ds->ServerMeta(); //the call that formed the cycle.
			EXPECT_FALSE( weak.expired() );
		}
		EXPECT_TRUE( weak.expired() ); //was false forever: the data source owned itself.
	}

	//m4-closing #24: sqlite folds the WAL back into the .db only when the connection closes, and nothing closed the products'
	//- a .db copied alone, the one file the READMEs said an uninstall keeps, held none of the rows.  DB's finalize function
	//makes this same Disconnect at exit.
	TEST_P( OpTests, DisconnectFoldsTheWalIntoTheDb ){
		if( GetParam()!="file" )
			GTEST_SKIP() << "a :memory: db has no WAL.";
		_ds->ExecuteSync( {"insert into access_identities( name, slug ) values( ?, ? )", {Value{"walter"}, Value{"walter@example.com"}}} );
		let path = *Settings::FindString( "/dbServers/file/catalogs/testDb/path" );
		ASSERT_TRUE( fs::exists(path+"-wal") );

		_ds->Disconnect();
		EXPECT_FALSE( fs::exists(path+"-wal") );
		EXPECT_FALSE( fs::exists(path+"-shm") );

		let copy = path+".copy";
		fs::copy_file( path, copy, fs::copy_options::overwrite_existing );
		auto config = Settings::AsObject( "/dbServers/file" );
		config.at( "catalogs" ).as_object().at( "testDb" ).as_object()["path"] = copy;
		{
			let alone = DB::DataSource( config );
			EXPECT_EQ( alone->ScalerSync<uint>({"select count(*) from access_identities where name=?", {Value{"walter"}}}), 1u );
		}
		for( let& file : {copy, copy+"-wal", copy+"-shm"} )
			fs::remove( file );
		EXPECT_EQ( _ds->ScalerSync<uint>({"select count(*) from access_identities where name=?", {Value{"walter"}}}), 1u ); //the next statement reopens.
	}

	//#53: the async ScalerOpt read the cell with Row::Get, which ASSERTs on a NULL and returns T{} - so it answered an
	//*engaged* optional{0} where the sync ScalerSyncOpt, which goes through Row::GetOpt, answers nullopt.  One API, two
	//answers, and Scaler<T> inherited it: 0 instead of "No value returned".
	TEST_P( OpTests, ScalerOptMatchesTheSyncContract ){
		let asyncOpt = [&]( sv text ){ return BlockAwait<ScalerAwaitOpt<uint>,optional<uint>>( _ds->ScalerOpt<uint>(Sql{string{text}}) ); };

		//a NULL cell: nullopt on both paths.  This is the one that differed.
		EXPECT_FALSE( asyncOpt("select null").has_value() );
		EXPECT_FALSE( _ds->ScalerSyncOpt<uint>(Sql{"select null"}).has_value() );

		//no rows at all, and a real value: they already agreed, and have to keep agreeing.
		EXPECT_FALSE( asyncOpt("select 1 where 1=0").has_value() );
		EXPECT_FALSE( _ds->ScalerSyncOpt<uint>(Sql{"select 1 where 1=0"}).has_value() );
		EXPECT_EQ( asyncOpt("select 5").value_or(0), 5u );
		EXPECT_EQ( _ds->ScalerSyncOpt<uint>(Sql{"select 5"}).value_or(0), 5u );

		//and the non-Opt form now reports the missing value instead of quietly returning 0.
		EXPECT_THROW( _ds->ScalerSync<uint>(Sql{"select null"}), Exception );
	}

	TEST_P( OpTests, ConstraintErrorsMapped ){
		//Constraint violations used to surface as a bare Exception whose code was a crc of "step failed: {} - {}" - the
		//sqlite result code was dropped. They now carry the extended code and EDbError, so callers branch without knowing sqlite.
		_ds->ExecuteSync( {"insert into access_identities( name, slug ) values( ?, ? )", {Value{"mapped1"}, Value{"mapped@example.com"}}} );
		try{
			_ds->ExecuteSync( {"insert into access_identities( name, slug ) values( ?, ? )", {Value{"mapped2"}, Value{"mapped@example.com"}}} ); //access_identities_nk1 is unique.
			FAIL() << "duplicate slug should have thrown.";
		}
		catch( const DBException& e ){
			EXPECT_EQ( ToString(e.Error), "duplicate" );
			EXPECT_EQ( e.Code(), 2067u ); //SQLITE_CONSTRAINT_UNIQUE - the extended code, not the bare SQLITE_CONSTRAINT(19).
		}
		try{ //fks are enforced (pragma foreign_keys=on) and no provider row is seeded.
			_ds->ExecuteSync( {"insert into access_identities( name, slug, provider_id ) values( ?, ?, ? )", {Value{"mapped3"}, Value{"mappedFk@example.com"}, Value{(uint)999}}} );
			FAIL() << "unknown provider_id should have thrown.";
		}
		catch( const DBException& e ){
			EXPECT_EQ( ToString(e.Error), "foreignKey" );
		}
	}

	TEST_P( OpTests, DbSettingsHonoredAfterCache ){
		//The fixture already populated the global cluster cache - supplied dbSettings must still be honored, not silently swapped for the cache.
		auto auth = ms<Access::Authorize>( "SqliteTests" );
		EXPECT_THROW( DB::GetAppSchema("access", auth, jobject{{"scriptPaths", jarray{}}}), Exception ); //no cluster objects -> 'No db servers found.', not the cached schema.
		EXPECT_TRUE( DB::GetAppSchema("access", auth, Settings::AsObject("/dbServers")) );
	}
}
