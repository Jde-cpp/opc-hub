#include <gtest/gtest.h>
#include <jde/db/db.h>
#include <jde/db/meta/Column.h>
#include "../../../src/meta/IServerMeta.h"
#include "../../../src/meta/ddl/TableDdl.h" //complete type for LoadTable's sp<TableDdl>.
#include "../../../src/meta/ddl/Index.h" //complete type for LoadIndexes' vector<Index>.
#include "../../../src/meta/ddl/ForeignKey.h" //complete type for LoadForeignKeys' flat_map<string,ForeignKey>.
#include "../../../src/meta/ddl/Procedure.h" //complete type for LoadProcs' flat_map<string,Procedure>.
#include "../src/SqliteSyntax.h"
#include <jde/app/AppQL.h>
#include <jde/db/DBException.h>
#include <jde/db/generators/FromClause.h>
#include <jde/db/generators/Functions.h> //Coalesce
#include <jde/db/generators/Sql.h>
#include <jde/db/generators/UpdateClause.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Cluster.h>
#include <jde/access/Authorize.h>

#define let const auto

namespace Jde::DB::Sqlite::Tests{
	struct SchemaTests : BackendTests{};
	INSTANTIATE_BACKENDS( SchemaTests );

	TEST_P( SchemaTests, ServerMetaLoadTable ){
		let table = _ds->ServerMeta().LoadTable( "main", "access_identities" );
		ASSERT_TRUE( table );
		ASSERT_EQ( table->Columns.size(), 11u );

		let id = table->FindColumn( "identity_id" );
		ASSERT_TRUE( id );
		EXPECT_TRUE( id->IsSequence ); //single-column integer pk = rowid alias.
		EXPECT_FALSE( id->IsNullable );

		let name = table->FindColumn( "name" );
		ASSERT_TRUE( name );
		EXPECT_EQ( name->Type, EType::VarChar );

		let created = table->FindColumn( "created" );
		ASSERT_TRUE( created );
		EXPECT_EQ( created->Type, EType::DateTime );

		let description = table->FindColumn( "description" );
		ASSERT_TRUE( description );
		EXPECT_TRUE( description->IsNullable );

		//composite pk is not an identity.
		let members = _ds->ServerMeta().LoadTable( "main", "access_role_members" );
		let roleId = members->FindColumn( "role_id" );
		ASSERT_TRUE( roleId );
		EXPECT_FALSE( roleId->IsSequence );

		EXPECT_THROW( _ds->ServerMeta().LoadTable("main", "missing"), Exception );
	}

	TEST_P( SchemaTests, ServerMetaLoadTables ){
		let tables = _ds->ServerMeta().LoadTables( "main", "access_identities" ); //prefix filter, like 'access_identities%'.
		EXPECT_EQ( tables.size(), 1u );
		EXPECT_TRUE( tables.contains("access_identities") );
		EXPECT_FALSE( tables.contains("access_roles") ); //other access_ tables excluded by the prefix.
	}

	//#35: LoadTables filtered to m.type='table', so a view never reached Tables() and SchemaDdl::SyncScripts' "already
	//exists" guard could not fire on sqlite - every *.sql view script was re-run on each sync of a file-backed db.
	//MySQL's LoadTables has always returned VIEWs; this is the contract the other drivers now match.
	TEST_P( SchemaTests, ServerMetaLoadsViews ){
		_ds->ExecuteSync( DB::Sql{"drop view if exists access_zz_view"} ); //the file backend keeps it between runs.
		_ds->ExecuteSync( DB::Sql{"create view access_zz_view as select identity_id, name from access_identities"} );

		let objects = _ds->ServerMeta().LoadTables( "main", "access_zz_" );
		let view = objects.find( "access_zz_view" );
		ASSERT_NE( view, objects.end() );
		EXPECT_EQ( view->second->Columns.size(), 2u ); //pragma_table_info answers for a view too.
		EXPECT_TRUE( view->second->SurrogateKeys.empty() ); //no pk, so nothing is mistaken for a rowid alias.

		let one = _ds->ServerMeta().LoadTable( "main", "access_zz_view" ); //LoadTable shares the filter.
		ASSERT_TRUE( one );
		EXPECT_EQ( one->Name, "access_zz_view" );

		EXPECT_TRUE( _ds->ServerMeta().LoadIndexes({}, "access_zz_").empty() ); //index/fk queries keep the table-only filter.
		_ds->ExecuteSync( DB::Sql{"drop view access_zz_view"} );
	}

	//#36: the "already exists" guard looked the script up as the unprefixed stem ("group_members") while Tables() is keyed
	//by the DB name ("access_group_members"), so the view half never matched and every view script re-ran on each sync.
	//Give the view a marker definition and sync again: guard fires -> the marker survives; script re-runs -> it is dropped
	//and replaced by the shipped one.  Depends on #35 too - without it a view is not in Tables() to be found at all.
	TEST_P( SchemaTests, SyncScriptsSkipsExistingViews ){
		_ds->ExecuteSync( DB::Sql{"drop view if exists access_group_members"} );
		_ds->ExecuteSync( DB::Sql{"create view access_group_members as select 1 zz_marker"} );

		Schema::Resync( GetParam() );
		let marked = _ds->ServerMeta().LoadTable( "main", "access_group_members" );
		ASSERT_TRUE( marked );
		ASSERT_EQ( marked->Columns.size(), 1u ); //the shipped definition has 11 - re-running the script is what would restore it.
		EXPECT_EQ( marked->Columns[0]->Name, "zz_marker" );

		_ds->ExecuteSync( DB::Sql{"drop view access_group_members"} ); //restore: gone from Tables(), so the next sync recreates it.
		Schema::Resync( GetParam() );
		EXPECT_GT( _ds->ServerMeta().LoadTable("main","access_group_members")->Columns.size(), 1u );
	}

	//#40: sqlite leaves an unbound `?` as NULL and runs the statement anyway, so `name=?` with no params silently became
	//`name=NULL` - zero rows, no error, nothing in the log.  Too *many* params already failed (SQLITE_RANGE), so the
	//contract was asymmetric; MySQL rejects both with client_errc::wrong_num_params.  Now so does this driver.
	TEST_P( SchemaTests, BindRejectsPlaceholderCountMismatch ){
		let count = [&]( DB::Sql&& sql ){ return _ds->ExecuteSync( move(sql) ); };
		EXPECT_NO_THROW( count(DB::Sql{"select count(*) from access_identities where name=?", vector<Value>{Value{string{"nobody"}}}}) );
		EXPECT_THROW( count(DB::Sql{"select count(*) from access_identities where name=?"}), DBException );      //1 placeholder, 0 params - the silent one.
		EXPECT_THROW( count(DB::Sql{"select count(*) from access_identities", vector<Value>{Value{1}}}), DBException ); //0 placeholders, 1 param - already failed, pinned for symmetry.

		//EDbError::None, not a constraint class: this is our bug, so it is a 500 - the same answer boost.mysql's
		//wrong_num_params gets after #9's client_errc split.  The SqliteException type has to survive, too (#11).
		optional<EDbError> error;
		try{ count( DB::Sql{"select count(*) from access_identities where name=?"} ); }
		catch( const DBException& e ){ error = e.Error; }
		ASSERT_TRUE( error.has_value() );
		EXPECT_EQ( (uint)*error, (uint)EDbError::None );
	}

	//#41: prepare_v2 compiles one statement and reports where it stopped; the tail was discarded, so everything after the
	//first statement was dropped without a word - including text that is not SQL at all.  MySQL runs the whole string
	//(multi_queries=true), so the two dialects disagreed silently.
	TEST_P( SchemaTests, ExecuteStatementRunsEveryStatement ){
		let exec = [&]( DB::Sql&& sql ){ return _ds->ExecuteSync( move(sql) ); };
		let rows = [&]{ return BlockAwait<ScalerAwait<uint>,uint>( _ds->Scaler<uint>(DB::Sql{"select count(*) from zz_multi"}) ); };
		exec( DB::Sql{"drop table if exists zz_multi"} );

		//three statements in one text: all three run, and the row count is the sum rather than whatever the last step left.
		EXPECT_EQ( exec(DB::Sql{"create table zz_multi(c integer); insert into zz_multi values(1); insert into zz_multi values(2);"}), 2u );
		EXPECT_EQ( rows(), 2u ); //was 0: the create ran and both inserts were dropped.

		//trailing garbage is a statement too, and now reaches prepare instead of being discarded.  Statements run in order,
		//so the insert before it has already committed - same as MySQL, and why all-or-nothing needs a transaction.
		EXPECT_THROW( exec(DB::Sql{"insert into zz_multi values(3); this is not sql"}), DBException );
		EXPECT_EQ( rows(), 3u );

		//a lone statement with a trailing bare ';' and padding still prepares once and binds its params: those tails
		//prepare to no statement at all, which the loop has to skip rather than treat as a second statement.
		EXPECT_EQ( exec(DB::Sql{"insert into zz_multi values(?);  ;  ", vector<Value>{Value{4}}}), 1u );
		EXPECT_EQ( rows(), 4u );

		//params cannot be shared across statements, so that is a misuse - and it is rejected before the first statement
		//steps, so nothing runs at all.  That is what the extra trial-prepare of the tail buys.
		EXPECT_THROW( exec(DB::Sql{"insert into zz_multi values(?); insert into zz_multi values(9)", vector<Value>{Value{5}}}), DBException );
		EXPECT_EQ( rows(), 4u );
		exec( DB::Sql{"drop table zz_multi"} );
	}

	//#42: ExecuteProc drops the trailing out placeholder with `Params.end()-1`, unguarded - so a proc called with an out
	//value and no params at all builds a transposed range.  That is a logic_error (std::length_error on libc++, a
	//debug-iterator abort on the MS STL), which every catch( runtime_error& ) in the driver and the awaits lets through.
	TEST_P( SchemaTests, ExecuteProcRejectsOutParamWithNoParams ){
		let procs = _ds->ServerMeta().LoadProcs( "main" ); //sqlite has no server procs - this is the native-twin registry.
		ASSERT_FALSE( procs.empty() );
		let registered = procs.begin()->first;

		//IsProc + an out value + no params.  The guard runs before `begin immediate`, so no transaction is left open.
		EXPECT_THROW( _ds->ExecuteScalerSync(DB::Sql{registered+"()", {}, true}, EValue::UInt64), DBException );
		//and it is reached only for a registered proc - an unknown name still fails the earlier guard.
		EXPECT_THROW( _ds->ExecuteScalerSync(DB::Sql{"zz_no_such_proc()", {}, true}, EValue::UInt64), Exception );
		//a proc with no out value and no params is legitimate and must still get past it.
		EXPECT_NO_THROW( _ds->ExecuteSync(DB::Sql{"select 1"}) );
	}

	//#43: ExecuteProc's catch(...) issues `rollback` unconditionally, and ExecuteStatement throws on a failed step - so
	//when sqlite had already auto-rolled-back (SQLITE_FULL/IOERR/NOMEM/INTERRUPT) the `throw;` never ran and the real
	//failure was replaced by "cannot rollback - no transaction is active", reported as EDbError::Syntax.
	TEST_P( SchemaTests, ProcRollbackKeepsTheOriginalError ){
		let scalar = [&]( sv text ){ return BlockAwait<ScalerAwait<uint>,uint>( _ds->Scaler<uint>(DB::Sql{string{text}}) ); };
		let message = [&]( DB::Sql&& sql ){
			string y;
			try{ _ds->ExecuteScalerSync( move(sql), EValue::UInt64 ); }
			catch( const Exception& e ){ y = e.what(); }
			return y;
		};
		//name, provider_id(1 is seeded by access.mutation), slug, attributes, description, is_group, email, + the out slot.
		let identityInsert = []( uint descriptionSize ){
			vector<Value> params{ Value{string{"zz_43"}}, Value{1}, Value{string{"zz_43"}}, Value{0},
				Value{string( descriptionSize, 'x' )}, Value{false}, Value{string{"zz_43@nowhere"}}, Value{} };
			return DB::Sql{ "access_identity_insert()", move(params), true };
		};

		//the ordinary path: the twin fails, the transaction is still open, rollback succeeds and the original error stands.
		EXPECT_NE( message(DB::Sql{"access_identity_insert()", vector<Value>{Value{1},Value{2},Value{3}}, true}).find("expects 7 params"), string::npos );

		//the auto-rollback path: cap the db at its current size, then give the twin a description far too big to fit -
		//sqlite answers SQLITE_FULL, rolls the transaction back itself, and the explicit rollback is then an error too.
		let pages = scalar( "pragma page_count" );
		_ds->ExecuteSync( DB::Sql{Ƒ("pragma max_page_count={}", pages)} );
		let full = message( identityInsert(4*1024*1024) );
		_ds->ExecuteSync( DB::Sql{"pragma max_page_count=1073741823"} );
		_ds->ExecuteSync( DB::Sql{"delete from access_identities where slug='zz_43'"} ); //in case it fit after all.

		EXPECT_EQ( full.find("no transaction is active"), string::npos ) << full; //the bug: the real error was replaced.
		EXPECT_NE( full.find("full"), string::npos ) << full;                     //"database or disk is full" survives.
	}

	//#44: _connMutex is a plain std::mutex held for the whole statement, and the RowΛ used to be called from inside the
	//sqlite3_step loop - so a callback that queried the same data source deadlocked against itself.  The identical
	//callback on MySQL just takes another pooled session, because that driver iterates result.rows() unlocked after
	//execute.  Rows are now collected under the lock and delivered after it, so one virtual has one contract.
	TEST_P( SchemaTests, SelectCallbackMayReenterTheDataSource ){
		uint seen{}, inner{};
		let rows = _ds->Select( DB::Sql{"select 1 union all select 2 union all select 3"}, [&]( Row&& ){ //literals: no seeded data to depend on.
			++seen;
			inner += (uint)_ds->Select( DB::Sql{"select 1"} ).size(); //re-entrant: takes _connMutex again.
		});
		EXPECT_GT( seen, 0u );
		EXPECT_EQ( inner, seen ); //every callback got its own answer back rather than hanging on the first.
		EXPECT_EQ( rows, 0u );    //Select returns rows *affected*, which a select has none of - MySQL answers affected_rows() here too.

		//A write from the callback works too - the statement is finalised before the callback runs, so nothing is in flight.
		//(Disconnect() is the other half of the same deadlock, but calling it here would drop a :memory: db the rest of the
		//suite shares, so it is not asserted.)
		EXPECT_NO_THROW( _ds->Select(DB::Sql{"select 1"}, [&]( Row&& ){ _ds->ExecuteSync( DB::Sql{"create table if not exists zz_44(c)"} ); }) );
		_ds->ExecuteSync( DB::Sql{"drop table if exists zz_44"} );
	}

	TEST_P( SchemaTests, ServerMetaIndexes ){
		let indexes = _ds->ServerMeta().LoadIndexes( {}, "access_" );
		let named = find_if( indexes, [](let& i){ return i.Name=="access_identities_nk0"; } );
		ASSERT_NE( named, indexes.end() );
		EXPECT_TRUE( named->Unique );
		EXPECT_FALSE( named->PrimaryKey );
		EXPECT_EQ( named->Columns, (vector<string>{"name", "provider_id"}) );

		//the access_user_insert_key twins document this index as the thing enforcing slug uniqueness - their pre-check only supplies the message.
		let slug = find_if( indexes, [](let& i){ return i.Name=="access_identities_nk1"; } );
		ASSERT_NE( slug, indexes.end() );
		EXPECT_TRUE( slug->Unique );
		EXPECT_EQ( slug->Columns, (vector<string>{"slug"}) );

		//rowid-alias single-integer pk is omitted by pragma_index_list; a composite pk surfaces as an autoindex, origin 'pk'.
		let pk = find_if( indexes, [](let& i){ return i.TableName=="access_role_members" && i.PrimaryKey; } );
		ASSERT_NE( pk, indexes.end() );
		EXPECT_EQ( pk->Columns, (vector<string>{"role_id", "member_id"}) );
	}

	TEST_P( SchemaTests, ServerMetaForeignKeys ){
		let fks = _ds->ServerMeta().LoadForeignKeys( "main" );
		let fk = find_if( fks, [](let& kv){ return kv.second.Table=="access_identities"; } );
		ASSERT_NE( fk, fks.end() );
		EXPECT_EQ( fk->second.pkTable, "access_providers" );
		EXPECT_EQ( fk->second.Columns, vector<string>{"provider_id"} );
	}

	TEST_P( SchemaTests, SyntaxDialect ){
		let& syntax = SqliteSyntax::Instance();
		EXPECT_EQ( syntax.ToString(EType::UInt), "integer" ); //rowid alias requires 'integer' exactly.
		EXPECT_EQ( syntax.ToString(EType::Long), "integer" );
		EXPECT_EQ( syntax.ToString(EType::VarChar), "varchar" );
		EXPECT_EQ( syntax.IdentityColumnSyntax(), "primary key autoincrement" );
		EXPECT_TRUE( syntax.IdentityIsPrimaryKey() ); //so TableDdl leaves the table-level PRIMARY KEY out for a table keyed by its sequence.
		let refusal = []( auto&& fnctn ){ string y; try{ fnctn(); }catch( const Exception& e ){ y = e.what(); } return y; };
		let sqliteOperator = refusal( [&]{ syntax.PatternOperator( EOperator::Regex ); } );
		EXPECT_NE( sqliteOperator.find("needs a udf"), string::npos ) << sqliteOperator;
		EXPECT_EQ( refusal([&]{ syntax.PatternParam(EOperator::Regex, "x"); }), sqliteOperator );
		EXPECT_TRUE( refusal([&]{ syntax.PatternParam(EOperator::Glob, "a*"); }).empty() );
		EXPECT_EQ( syntax.EscapeDdl("tbl"), "\"tbl\"" );
		EXPECT_EQ( syntax.EscapeDdl("main.tbl"), "\"main\".\"tbl\"" );
		EXPECT_EQ( syntax.ToString(EType::Blob), "blob" ); //#34: had no branch anywhere, so a types.blob column reached create table untyped.
		EXPECT_FALSE( syntax.HasProcs() );
		EXPECT_FALSE( syntax.CanAddForeignKeys() );
		EXPECT_EQ( syntax.GuidType(), "blob" );

		//Limit(): OFFSET requires a LIMIT, so skip-only must use a negative (=unbounded) limit, not 'limit 0' which returns zero rows.
		let sql = string{"select * from t"};
		EXPECT_EQ( syntax.Limit(sql, 10, 0), "select * from t limit 10" ); //limit-only.
		EXPECT_EQ( syntax.Limit(sql, 10, 5), "select * from t limit 10 offset 5" ); //limit + skip.
		EXPECT_EQ( syntax.Limit(sql, 0, 5), "select * from t limit -1 offset 5" ); //skip-only: returns the tail, not zero rows.
	}

	TEST_P( SchemaTests, AtSchemaMainOnly ){
		EXPECT_EQ( _ds->AtSchema("main").get(), _ds.get() );
		EXPECT_THROW( _ds->AtSchema("other"), Exception );
		EXPECT_EQ( _ds->AtCatalog("other").get(), _ds.get() ); //db-refactor A6: no catalogs, so IDataSource's base answers this data source (and logs Critical).
	}

	//#7: a single-table FromClause leaves Joins[0].To null; Contains/GetColumnPtr must not deref it.
	TEST_P( SchemaTests, SingleTableFromClauseNullJoin ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto table = schema->GetTablePtr( "identities" ); //config-side, Initialized -> Column::Table set, pk resolvable.
		ASSERT_TRUE( table );

		FromClause fc{ table }; //Joins[0].To is null (no join).
		ASSERT_FALSE( fc.HasJoin() );
		EXPECT_FALSE( fc.Contains("no_such_table") ); //was: deref of null To->Table.
		EXPECT_TRUE( fc.GetColumnPtr("id", SRCE_CUR) ); //surrogate pk, found in the From table.
		EXPECT_THROW( fc.GetColumnPtr("no_such_column", SRCE_CUR), Exception ); //typo'd column now THROWs instead of segfaulting on null To.
	}

	//#24: empty IN/NOT IN must not emit invalid `col in()` (debug ASSERT / release garbage) - use the 1=0 / 1=1 idiom.
	TEST_P( SchemaTests, EmptyInOperator ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto col = schema->GetTablePtr("identities")->GetColumnPtr( "id", SRCE_CUR );
		let& syntax = SqliteSyntax::Instance();
		EXPECT_EQ( syntax.FormatOperator(*col, EOperator::In, 0), "1=0" );    //empty IN matches nothing.
		EXPECT_EQ( syntax.FormatOperator(*col, EOperator::NotIn, 0), "1=1" ); //empty NOT IN matches everything.
		EXPECT_NE( syntax.FormatOperator(*col, EOperator::In, 2).find("in(?,?)"), string::npos ); //non-empty still an IN list.
	}

	//A word operator needs whitespace, punctuation does not.  FormatOperator used to render every non-IN operator as
	//`Ƒ("{}{}?", FQName, OperatorStrings[op])`, which is right for '=' and '>=' but glued the three word-valued entries
	//onto the column name - `regex` gave "access_identities.idregex?", one identifier, a syntax error on every backend.
	//Only IN/NOT IN were covered by a test, which is the branch that was never broken.
	TEST_P( SchemaTests, FormatOperatorSpacing ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto col = schema->GetTablePtr("identities")->GetColumnPtr( "id", SRCE_CUR );
		let& syntax = SqliteSyntax::Instance();
		let fq = col->FQName();
		EXPECT_EQ( syntax.FormatOperator(*col, EOperator::Equal), fq+"=?" );          //punctuation self-delimits.
		EXPECT_EQ( syntax.FormatOperator(*col, EOperator::GreaterOrEqual), fq+">=?" );
		EXPECT_EQ( syntax.FormatOperator(*col, EOperator::Glob), fq+" glob ?" );      //…a word does not.
		EXPECT_EQ( syntax.FormatOperator(*col, EOperator::Glob).find(fq+"glob"), string::npos ); //the bug.
		EXPECT_THROW( syntax.FormatOperator(*col, EOperator::Regex), Exception );     //sqlite REGEXP needs a udf; refused, not emitted.
		EXPECT_THROW( syntax.FormatOperator(*col, EOperator::ElementMatch), Exception ); //no SQL form at all - was "…idelemMatch?".
		//One pattern, one placeholder: a `glob:["a*","b*"]` array must not bind 2 params against the single '?'.
		EXPECT_THROW( syntax.FormatOperator(*col, EOperator::Glob, 2), Exception );
		//#22: the comparison operators owe the same count check.  N>=2 was an opaque driver bind error; N==0 was silent -
		//sqlite ran `name=?` with nothing bound and returned no rows.
		EXPECT_THROW( syntax.FormatOperator(*col, EOperator::Equal, 2), Exception );
		EXPECT_THROW( syntax.FormatOperator(*col, EOperator::Equal, 0), Exception );
		EXPECT_THROW( syntax.FormatOperator(*col, EOperator::Greater, 3), Exception );
		EXPECT_EQ( syntax.FormatOperator(*col, EOperator::Equal, 1), fq+"=?" ); //the single-value form still renders.
		//In/NotIn keep their own counts - 0 is a legitimate empty set there, not an error.
		EXPECT_EQ( syntax.FormatOperator(*col, EOperator::In, 0), "1=0" );
		EXPECT_EQ( syntax.FormatOperator(*col, EOperator::NotIn, 0), "1=1" );
	}

	//#8: nothing pinned the placeholder-vs-param invariant on the update/where generators, which is why #5 and #22-#25 all
	//sat unnoticed.  The invariant every clause owes its caller: one '?' in the text per bound param, and a text the
	//backend will actually parse.  These live here rather than in Jde.DB.Tests because UpdateClause::Move and
	//WhereClause::Add reach Table::Syntax() -> AppSchema::Syntax() -> DBSchema->DS(), and that suite opens no data source.
	Ω placeholders( str sql )ι->uint{ return (uint)std::ranges::count( sql, '?' ); }
	Ω countOf( str haystack, sv needle )ι->uint{
		uint n{};
		for( size_t i=haystack.find(needle); i!=string::npos; i=haystack.find(needle, i+1) )
			++n;
		return n;
	}
	//UpdateClause::Values is a flat_map keyed by sp address, so which column renders first is an allocation detail - the
	//original bug only showed when the no-param column happened to sort first.  Choose deliberately instead of hoping.
	Ω lowHigh( sp<Column> a, sp<Column> b )ι->std::pair<sp<Column>,sp<Column>>{ return a<b ? std::pair{a,b} : std::pair{b,a}; }

	//The whole point of the dialect hook: a where clause built from a glob filter has to be executable, so the pattern
	//is bound as-is for sqlite (GLOB *is* QL::globMatch) while other dialects rewrite it.
	TEST_P( SchemaTests, GlobWhereClauseRoundTrips ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto table = schema->GetTablePtr( "identities" );
		auto col = table->GetColumnPtr( "name", SRCE_CUR );
		WhereClause where;
		where.Add( col, EOperator::Glob, Value{string{"*bob*"}} );
		EXPECT_EQ( where.ToString(), Ƒ("where {} glob ?", col->FQName()) );
		EXPECT_EQ( placeholders(where.ToString()), where.Params().size() ) << where.ToString(); //#8: one '?' per bound param.

		//and it parses - a clause reaching a real sqlite parser is what the old spelling could never survive.
		auto sql = DB::SelectSql( vector<sp<Column>>{col}, FromClause{table}, move(where) );
		let text = sql.Text;
		EXPECT_NO_THROW( _ds->Select(move(sql)) ) << text;
	}

	//#40: SelectSql with empty columns overwrote the trailing space of "select " with '\n' ("select\nfrom..."). Guard the comma->newline replacement.
	TEST_P( SchemaTests, SelectSqlEmptyColumns ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto table = schema->GetTablePtr( "identities" );
		ASSERT_TRUE( table );
		let empty = DB::SelectSql( vector<sp<Column>>{}, FromClause{table}, WhereClause{} );
		EXPECT_TRUE( empty.Text.starts_with("select \n") ) << empty.Text; //space preserved, not clobbered to '\n'.
		EXPECT_FALSE( empty.Text.starts_with("select\n") ) << empty.Text; //the bug.
		let sks = DB::SelectSKsSql( table ); //non-empty columns still well-formed: trailing ',' -> newline, no dangling comma.
		EXPECT_TRUE( sks.Text.starts_with("select ") ) << sks.Text;
		EXPECT_EQ( sks.Text.find(",\n"), string::npos ) << sks.Text; //no comma left immediately before the from-newline.
	}

	//#15: Table::Initialize must be idempotent - SchemaDdl::SyncTables re-initializes every table after the ctor's Initialize, and re-init duplicated PKTable->Children.
	TEST_P( SchemaTests, InitializeIdempotentChildren ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		sp<Table> mapped;
		for( let& kv : schema->Tables )
			if( kv.second->Map.has_value() && kv.second->Map->Parent->PKTable ){ mapped = kv.second; break; } //a parent/child-mapped table (e.g. members).
		ASSERT_TRUE( mapped );
		auto pkTable = mapped->Map->Parent->PKTable;
		let before = pkTable->Children.size();
		mapped->Initialize( schema, mapped ); //re-init, exactly as SyncTables does.
		EXPECT_EQ( pkTable->Children.size(), before ); //no duplicate child on re-init.
	}

	//db-refactor B4: a `views:` entry is a Table held in AppSchema::Views - map membership is the whole distinction.  FindView
	//answers from Views, then falls back to Tables; FindTable never answers a view; and a view ran Initialize like a table
	//(Schema set, and users' qlView points back at users through Owner).
	TEST_P( SchemaTests, ViewsAreTables ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto view = schema->FindView( "users_ql" );
		ASSERT_TRUE( view );
		ASSERT_NE( schema->Views.find("users_ql"), schema->Views.end() );
		EXPECT_EQ( schema->Views.find("users_ql")->second, view );
		EXPECT_FALSE( schema->FindTable("users_ql") );
		EXPECT_EQ( schema->FindView("users"), schema->FindTable("users") ); //the fallback.
		EXPECT_EQ( view->Schema, schema );
		auto users = schema->GetTablePtr( "users" );
		ASSERT_TRUE( users->QLView );
		EXPECT_EQ( users->QLView, view );
		EXPECT_EQ( users->QLView->Owner.lock(), users );
	}

	//#17: the (Table,alias,cols) SelectClause ctor resolves each name with Table::GetColumnPtr, which THROWs.  While the
	//ctor was ι that throw was std::terminate, and the caller's own try/catch could not see it - so a meta rename, or a
	//typo in a new call site, took the process down.  Note the failure mode if this regresses: the suite does not report
	//a failed test, it dies here.
	TEST_P( SchemaTests, SelectClauseUnknownColumnThrows ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto table = schema->GetTablePtr( "identities" );
		ASSERT_TRUE( table );
		EXPECT_NO_THROW( (SelectClause{*table, "i", {"identity_id"}}) );                       //a name it has.
		EXPECT_THROW( (SelectClause{*table, "i", {"identity_id","zz_no_such_column"}}), Exception );
	}

	//#11: both relays hand the error to a `function<void(Exception&&)>`.  Catching only `runtime_error` there ran it
	//through Exception's non-explicit (runtime_error&&) ctor, which field-copies - so the driver's SqliteException was
	//sliced to a plain Jde::Exception before the awaitable's virtual Move() ever saw it, and HttpStatus()/Error/
	//ClientDetail() answered the base defaults.  What both paths owe the caller is the type the driver threw.
	TEST_P( SchemaTests, RelaysKeepTheDriverExceptionType ){
		const DB::Sql bad{ "select id from zz_no_such_table" };
		uint sliced{}, kept{};
		auto classify = [&]( auto&& fnctn ){
			try{ fnctn(); }
			catch( const DBException& e ){ ++kept; EXPECT_EQ( e.Error, EDbError::Syntax ); } //sqlite: no such table -> Syntax.
			catch( const Exception& ){ ++sliced; }                                            //the bug.
		};
		classify( [&]{ BlockAwait<ScalerAwait<uint>,uint>( _ds->Scaler<uint>(DB::Sql{bad}) ); } ); //ScalerExecute -> RunQuery
		//TSelectAwait's fold projection through RunQuery (SelectMap/CacheAwait) - SelectAsync's SelectAwait talks to the driver directly.
		using EnumMap = flat_map<uint,string>;
		classify( [&]{ BlockAwait<CacheAwait<EnumMap>,EnumMap>( _ds->SelectMap<uint,string>(DB::Sql{bad}, "zz_no_such_table") ); } );
		EXPECT_EQ( kept, 2u );
		EXPECT_EQ( sliced, 0u );
	}

	//#5: a null or "$now" column pushes no param, so inferring "first column" from Params.empty() made the column *after*
	//one re-emit the "<table> set " prefix - `update t set a = nullt set b = ?`, which no backend will parse.
	TEST_P( SchemaTests, UpdateClauseNoParamFirstColumn ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto table = schema->GetTablePtr( "identities" );
		ASSERT_TRUE( table );
		let [lo,hi] = lowHigh( table->GetColumnPtr("description", SRCE_CUR), table->GetColumnPtr("email", SRCE_CUR) );
		for( let& noParam : vector<Value>{ Value{}, Value{string{"$now"}} } ){ //null, then the sql-function sentinel.
			UpdateClause update;
			update.Add( lo, noParam );
			update.Add( hi, Value{string{"x"}} );
			update.Where.Add( table->GetPK(), Value{(uint)999999} ); //no such row: this pins that it parses, not that it writes.
			auto sql = update.Move();
			let text = sql.Text;
			let setClause = text.substr( 0, text.find("where") ); //the where clause names the table again, legitimately.
			EXPECT_EQ( countOf(setClause, " set "), 1u ) << text;
			EXPECT_EQ( placeholders(text), sql.Params.size() ) << text;
			EXPECT_NO_THROW( _ds->ExecuteSync(move(sql)) ) << text;
		}
	}

	//#23: the (Object,EOperator,Object) ctor renders the operator with DB::ToString(op) - the wire spelling QL parses -
	//rather than Syntax::FormatOperator, so `nin` reaches the backend verbatim; and its null-b branch never collects a's
	//params, so a Coalesce on the left leaves a '?' with nothing bound.
	//#24: a rejected Add must leave the clause as it found it.  Both vector overloads used to push their params before
	//the call that rejects them, so a refused `glob:[null,"a*","b*"]` left _params holding two values with no clause to
	//match - invisible in-tree only because the sole caller drops the whole WhereClause when the throw propagates.
	TEST_P( SchemaTests, WhereClauseAddIsAllOrNothing ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto table = schema->GetTablePtr( "identities" );
		auto col = table->GetColumnPtr( "name", SRCE_CUR );
		let patterns = []{ return vector<Value>{ Value{string{"a*"}}, Value{string{"b*"}} }; };
		for( let haveNull : {false, true} ){
			WhereClause where;
			where.Add( col, EOperator::Equal, Value{string{"seed"}} );          //one good clause, so a leak is visible.
			let before = where.Params().size();
			EXPECT_THROW( where.Add(col, EOperator::Glob, patterns(), haveNull), Exception ) << haveNull;
			EXPECT_EQ( where.Params().size(), before ) << haveNull;             //no params from the refused Add.
			EXPECT_EQ( placeholders(where.ToString()), where.Params().size() ) << where.ToString();
		}
	}

	TEST_P( SchemaTests, WhereClauseObjectCtorInvariants ){
		auto schema = DB::GetCluster( GetParam(), ms<Access::Authorize>("SqliteTests") )->GetAppSchema( "access" );
		auto table = schema->GetTablePtr( "identities" );
		auto col = table->GetColumnPtr( "name", SRCE_CUR );
		//#23: the operators whose ToString is a wire spelling, not SQL, are refused - this ctor composes both sides
		//itself and so cannot reach Syntax::FormatOperator/PatternParam.  `nin` used to reach the backend verbatim.
		for( let op : {EOperator::NotIn, EOperator::In, EOperator::Regex, EOperator::Glob, EOperator::ElementMatch} )
			EXPECT_THROW( (WhereClause{Object{AliasCol{col}}, op, Object{vector<Value>{Value{(uint)1}}}}), Exception ) << (int)op;
		//the punctuation operators still compose, and bind both sides.
		{
			const WhereClause where{ Object{AliasCol{col}}, EOperator::Greater, Object{Value{string{"m"}}} };
			let text = where.ToString();
			EXPECT_NE( text.find(">"), string::npos ) << text;
			EXPECT_EQ( placeholders(text), where.Params().size() ) << text;
		}
		//#23's other half: the null-b branch never collected `a`'s params, so a Coalesce on the left left its '?' unbound.
		{
			const WhereClause where{ Object{Coalesce{Object{AliasCol{col}}, Object{Value{string{"x"}}}}}, EOperator::Equal, Object{Value{}} };
			let text = where.ToString();
			EXPECT_EQ( placeholders(text), where.Params().size() ) << text; //coalesce's '?' must keep its param.
			EXPECT_EQ( where.Params().size(), 1u ) << text;
		}
	}

}
