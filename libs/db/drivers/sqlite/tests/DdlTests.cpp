#include <gtest/gtest.h>
#include <jde/db/DBException.h>
#include <jde/db/IDataSource.h>
#include <jde/db/meta/Table.h>
#include "../../../src/meta/IServerMeta.h"
#include "../../../src/meta/ddl/ColumnDdl.h"
#include "../src/SqliteDdl.h"
#include "globals.h"

#define let const auto

namespace Jde::DB::Sqlite::Tests{
	//#241: sqlite has no `alter table … set default`, so a changed `default:` on an existing table is an edit of the
	//CREATE TABLE text in sqlite_master - the manual's own route for it.  RewriteDefault is the pure edit; these pin it
	//on what our ddl writes (TableDdl::CreateStatement) and on the hand-made shapes the schema text may also hold.
	constexpr sv ours{ "CREATE TABLE zz_widgets(\n\tname varchar not null,\n\tcolor varchar null default 'red',\n\tqty integer not null default 1,\n\tcreated datetime null,\n\tPRIMARY KEY( name )\n)" };

	TEST( RewriteDefaultTests, ReplacesEachKindOfValue ){
		EXPECT_EQ( RewriteDefault(ours, "color", "'blue'"), "CREATE TABLE zz_widgets(\n\tname varchar not null,\n\tcolor varchar null default 'blue',\n\tqty integer not null default 1,\n\tcreated datetime null,\n\tPRIMARY KEY( name )\n)" );
		EXPECT_EQ( RewriteDefault(ours, "qty", "2"), "CREATE TABLE zz_widgets(\n\tname varchar not null,\n\tcolor varchar null default 'red',\n\tqty integer not null default 2,\n\tcreated datetime null,\n\tPRIMARY KEY( name )\n)" );
		EXPECT_EQ( RewriteDefault(ours, "created", "(unixepoch())"), "CREATE TABLE zz_widgets(\n\tname varchar not null,\n\tcolor varchar null default 'red',\n\tqty integer not null default 1,\n\tcreated datetime null default (unixepoch()),\n\tPRIMARY KEY( name )\n)" ); //none before: appended, the newline kept where it was.
		EXPECT_EQ( RewriteDefault("create table t( a int, b int default (1+(2)) check( b>0 ), c text default 'x' )", "b", "3"), "create table t( a int, b int default 3 check( b>0 ), c text default 'x' )" ); //a parenthesized expression, with a constraint after it.
		EXPECT_EQ( RewriteDefault("create table t( a text default 'it''s', b text default 'a,b' )", "b", "'c'"), "create table t( a text default 'it''s', b text default 'c' )" ); //doubled quotes and a comma inside a string neither end the value nor split the item.
		EXPECT_EQ( RewriteDefault("create table t( a text default 'it''s', b text default 'a,b' )", "a", "'z'"), "create table t( a text default 'z', b text default 'a,b' )" );
		EXPECT_EQ( RewriteDefault("create table t( a int DEFAULT -1 not null, b int )", "a", "5"), "create table t( a int default 5 not null, b int )" ); //a signed bare token, the keyword in any case, constraints after.
		EXPECT_EQ( RewriteDefault("create table t( a int default x'ff', b int )", "a", "1"), "create table t( a int default 1, b int )" ); //a blob literal.
	}

	TEST( RewriteDefaultTests, FindsTheColumnByItsNameAlone ){
		EXPECT_EQ( RewriteDefault("create table t( name2 text default 'x', name text default 'y' )", "name", "'z'"), "create table t( name2 text default 'x', name text default 'z' )" ); //a prefix of another name is not it.
		EXPECT_EQ( RewriteDefault("create table t( \"name\" text default 'x', [qty] int, `n` int, 'q' int )", "qty", "7"), "create table t( \"name\" text default 'x', [qty] int default 7, `n` int, 'q' int )" ); //quoted in any of sqlite's styles.
		EXPECT_EQ( RewriteDefault("create table t( Name text )", "name", "'z'"), "create table t( Name text default 'z' )" ); //identifiers are case-insensitive.
		EXPECT_EQ( RewriteDefault("create table t( a int, check( a <> 'default' ), b text )", "b", "'z'"), "create table t( a int, check( a <> 'default' ), b text default 'z' )" ); //a table constraint between columns, with the keyword inside a string.
		EXPECT_THROW( RewriteDefault(ours, "nope", "1"), Exception );
		EXPECT_THROW( RewriteDefault("not a create statement", "a", "1"), Exception );
	}

	//The whole route through the server meta on a live connection:  the text rewritten under writable_schema, read back
	//before the commit, and in force for the next insert while the rows already there keep their values.
	struct DdlTests : BackendTests{};
	INSTANTIATE_BACKENDS( DdlTests );
	TEST_P( DdlTests, SetDefaultRewritesTheStoredText ){
		_ds->ExecuteSync( DB::Sql{"drop table if exists zz_241"} );
		_ds->ExecuteSync( DB::Sql{"create table zz_241(\n\tname varchar not null,\n\tcolor varchar null default 'red',\n\tcreated datetime null,\n\tPRIMARY KEY( name )\n)"} );
		_ds->ExecuteSync( DB::Sql{"insert into zz_241(name) values('w1')"} );
		let version = _ds->ScalerSync<uint>( DB::Sql{"pragma schema_version"} );
		let dflt = [&]( sv column ){ return _ds->ScalerSyncOpt<string>( {"select dflt_value from pragma_table_info('zz_241') where name=?", {Value{string{column}}}} ); };
		Table table{ "zz_241" }; //SetDefault reads DBName alone.
		auto& meta = _ds->ServerMeta();

		meta.SetDefault( table, ColumnDdl{"color", "'red'", true, EType::VarChar, {}, false, optional<uint8>{}, {}, {}}, Value{"blue"} );
		EXPECT_EQ( dflt("color").value_or(""), "'blue'" );
		meta.SetDefault( table, ColumnDdl{"created", "", true, EType::DateTime, {}, false, optional<uint8>{}, {}, {}}, Value{"$now"} );
		EXPECT_EQ( dflt("created").value_or(""), "unixepoch()" ); //pragma_table_info reads the ddl's `(unixepoch())` back without its parens.
		EXPECT_EQ( _ds->ScalerSync<uint>(DB::Sql{"pragma schema_version"}), version+2 ); //one bump per rewrite - what tells other connections to reload.

		_ds->ExecuteSync( DB::Sql{"insert into zz_241(name) values('w2')"} );
		EXPECT_EQ( _ds->ScalerSync<string>(DB::Sql{"select color from zz_241 where name='w2'"}), "blue" );
		EXPECT_EQ( _ds->ScalerSync<string>(DB::Sql{"select color from zz_241 where name='w1'"}), "red" );
		EXPECT_EQ( _ds->ScalerSync<uint>(DB::Sql{"select count(*) from zz_241 where name='w2' and created is not null"}), 1u );
		EXPECT_EQ( _ds->ScalerSync<uint>(DB::Sql{"select count(*) from zz_241 where name='w1' and created is null"}), 1u );
		EXPECT_EQ( _ds->ScalerSync<string>(DB::Sql{"pragma integrity_check"}), "ok" );

		//a column the text does not have fails before anything is written - the schema cookie is where it was.
		EXPECT_THROW( meta.SetDefault(table, ColumnDdl{"nope", "", true, EType::Int, {}, false, optional<uint8>{}, {}, {}}, Value{1}), Exception );
		EXPECT_EQ( _ds->ScalerSync<uint>(DB::Sql{"pragma schema_version"}), version+2 );
		_ds->ExecuteSync( DB::Sql{"drop table zz_241"} );
	}
}