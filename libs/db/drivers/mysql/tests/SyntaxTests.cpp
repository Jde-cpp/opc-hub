#include <gtest/gtest.h>
#include <jde/db/Value.h>
#include "../src/MySqlSyntax.h"
#include "../src/MySqlServerMeta.h" //ToDbType; MySqlServerMeta.cpp is compiled into this target - see CMakeLists.txt.

namespace Jde::DB::MySql::Tests{
	//Pure dialect assertions - no connection, no server.  Moved out of the sqlite driver's SchemaTests, which could
	//only reach MySqlSyntax while it still lived in Jde.DB.
	//#38: ToType matched whole COLUMN_TYPE spellings, so it only recognised the pre-8.0.19 ones somebody had written
	//down.  8.0.19 dropped the display width from integer types, and a live 8.4 census of the shipped schemas returns
	//`int unsigned`/`bigint unsigned`/`smallint unsigned`/`tinyint unsigned` - of which only the first was matched.
	TEST( ServerMetaTests, ToDbTypeAcrossServerVersions ){
		using enum EType;
		//The four the 8.x census actually returns.  Only `int unsigned` used to land; the rest were Long/Int16/None.
		EXPECT_EQ( ToDbType("int unsigned"), UInt );
		EXPECT_EQ( ToDbType("bigint unsigned"), ULong );
		EXPECT_EQ( ToDbType("smallint unsigned"), UInt16 );
		EXPECT_EQ( ToDbType("tinyint unsigned"), UInt8 );

		//the 5.7 spellings of the same four have to keep answering the same.
		EXPECT_EQ( ToDbType("int(10) unsigned"), UInt );
		EXPECT_EQ( ToDbType("bigint(20) unsigned"), ULong );
		EXPECT_EQ( ToDbType("bigint(21) unsigned"), ULong ); //what count(*) reports.
		EXPECT_EQ( ToDbType("smallint(5) unsigned"), UInt16 );
		EXPECT_EQ( ToDbType("tinyint(3) unsigned"), UInt8 );

		//signed integers, both spellings.
		EXPECT_EQ( ToDbType("int"), Int );
		EXPECT_EQ( ToDbType("int(11)"), Int );
		EXPECT_EQ( ToDbType("bigint"), Long );
		EXPECT_EQ( ToDbType("smallint"), Int16 );
		EXPECT_EQ( ToDbType("mediumint"), Int );        //no 24-bit EType; Int is the narrowest that holds it.
		EXPECT_EQ( ToDbType("integer"), Int );
		EXPECT_EQ( ToDbType("int(10) unsigned zerofill"), UInt ); //the suffix may not be last.

		//tinyint(1) is what BOOL/BOOLEAN alias to.  bit(1) is what this codebase's own DDL emits - both are Bit.
		EXPECT_EQ( ToDbType("tinyint(1)"), Bit );
		EXPECT_EQ( ToDbType("bit(1)"), Bit );
		EXPECT_EQ( ToDbType("tinyint"), Int8 );         //no width -> a small integer, not a flag.

		//#30's pairing, read back: MySQL DOUBLE is the 8-byte type EType::Float means, FLOAT the 4-byte one.
		EXPECT_EQ( ToDbType("double"), Float );
		EXPECT_EQ( ToDbType("double precision"), Float );
		EXPECT_EQ( ToDbType("real"), Float );           //a DOUBLE synonym unless REAL_AS_FLOAT is set.
		EXPECT_EQ( ToDbType("float"), SmallFloat );

		EXPECT_EQ( ToDbType("varchar(255)"), VarChar );
		EXPECT_EQ( ToDbType("char(3)"), Char );
		EXPECT_EQ( ToDbType("decimal(10,2)"), Decimal );
		EXPECT_EQ( ToDbType("binary(16)"), Binary );    //Guid columns: MySqlSyntax::GuidType() is binary.
		EXPECT_EQ( ToDbType("varbinary(64)"), VarBinary );
		EXPECT_EQ( ToDbType("datetime"), DateTime );
		EXPECT_EQ( ToDbType("timestamp"), DateTime );

		//the text/blob families and json, none of which had a case at all.
		for( const sv name : {"text"sv, "tinytext"sv, "mediumtext"sv, "longtext"sv, "json"sv} )
			EXPECT_EQ( ToDbType(name), Text ) << name;
		for( const sv name : {"blob"sv, "tinyblob"sv, "mediumblob"sv, "longblob"sv} )
			EXPECT_EQ( ToDbType(name), Blob ) << name; //#34 gave Blob a spelling in every dialect.

		EXPECT_EQ( ToDbType("enum('a','b')"), None );   //genuinely unmapped - MySqlServerMeta::ToType WARNs on None.
		EXPECT_EQ( ToDbType("BIGINT UNSIGNED"), ULong );//case-insensitive, as the old varchar branch already assumed.
	}

	TEST( SyntaxTests, MySqlDialect ){
		const auto& my = MySqlSyntax::Instance();

		//every unsigned width must carry 'unsigned' (sqlite maps them all to 'integer', so it could not exercise this).
		EXPECT_EQ( my.ToString(EType::UInt), "int unsigned" );
		EXPECT_EQ( my.ToString(EType::ULong), "bigint(20) unsigned" );
		EXPECT_EQ( my.ToString(EType::UInt16), "smallint unsigned" ); //#6: was "smallint" - uint16 columns got half the range.
		EXPECT_EQ( my.ToString(EType::UInt8), "tinyint unsigned" );
		EXPECT_EQ( my.ToString(EType::Int16), "smallint" ); //signed stays plain.

		//#30: MySQL has the float widths the other way round from T-SQL/sqlite - FLOAT is single precision, REAL is a
		//DOUBLE synonym - so the base names produced a 4-byte column for the 8-byte type and vice versa.
		EXPECT_EQ( my.ToString(EType::Float), "double" );      //8 bytes, as EType::Float means everywhere else.
		EXPECT_EQ( my.ToString(EType::SmallFloat), "float" );  //4 bytes.
		const Syntax base;
		EXPECT_EQ( base.ToString(EType::Float), "float" );     //T-SQL keeps its own spelling: there `float` is the 8-byte type.
		EXPECT_EQ( base.ToString(EType::SmallFloat), "real" );

		EXPECT_TRUE( Syntax::IsPatternOperator(EOperator::Glob) );
		EXPECT_TRUE( Syntax::IsPatternOperator(EOperator::Regex) );
		EXPECT_FALSE( Syntax::IsPatternOperator(EOperator::Equal) );

		const auto refusal = []( auto&& fnctn ){ string y; try{ fnctn(); }catch( const Exception& e ){ y = e.what(); } return y; };
		const auto baseOperator = refusal( [&]{ base.PatternOperator( EOperator::Regex ); } );
		EXPECT_NE( baseOperator.find("REGEXP_LIKE"), string::npos ) << baseOperator; //T-SQL has no regex before 2025.
		EXPECT_EQ( refusal([&]{ base.PatternParam(EOperator::Regex, "x"); }), baseOperator ); //one message, not two copies.
		EXPECT_TRUE( refusal([&]{ my.PatternParam(EOperator::Regex, "b.b"); }).empty() );    //MySQL supports both, so neither half refuses.
		EXPECT_TRUE( refusal([&]{ my.PatternParam(EOperator::Glob, "a*"); }).empty() );

		EXPECT_EQ( base.EscapeDdl("tbl"), "[tbl]" );
		EXPECT_EQ( base.EscapeDdl("dbo.tbl"), "[dbo].[tbl]" ); //each part quoted, not the whole string.
		EXPECT_EQ( my.EscapeDdl("tbl"), "`tbl`" );
		EXPECT_EQ( my.EscapeDdl("db.tbl"), "`db`.`tbl`" );

		//#34: Blob had no branch in any dialect, so a `types.blob` column reached `create table` with an empty type.
		EXPECT_EQ( my.ToString(EType::Blob), "blob" );
		EXPECT_EQ( base.ToString(EType::Blob), "varbinary(max)" ); //T-SQL has no BLOB keyword.
		EXPECT_FALSE( base.HasLength(EType::Blob) );              //varbinary(max) already carries its own length.
		EXPECT_FALSE( my.HasLength(EType::Blob) );

		//#29: the wide char pair carries a length like the narrow one - without it T-SQL reads nvarchar as nvarchar(1).
		EXPECT_TRUE( base.HasLength(EType::VarWChar) );
		EXPECT_TRUE( base.HasLength(EType::WChar) );
		EXPECT_TRUE( base.HasLength(EType::VarChar) );
		EXPECT_FALSE( base.HasLength(EType::Int) );

		//#31: MySQL used to answer true for everything, so a visible length on an integer or datetime produced
		//`bigint(20) unsigned(256)` / `datetime(64)` - both server errors at create table.
		EXPECT_TRUE( my.HasLength(EType::VarChar) );
		EXPECT_TRUE( my.HasLength(EType::Bit) );      //bit(1) is the one the shipped metas actually emit.
		EXPECT_TRUE( my.HasLength(EType::Guid) );     //binary(16).
		EXPECT_FALSE( my.HasLength(EType::ULong) );
		EXPECT_FALSE( my.HasLength(EType::UInt) );
		EXPECT_FALSE( my.HasLength(EType::Int) );
		EXPECT_FALSE( my.HasLength(EType::DateTime) );
		EXPECT_FALSE( my.HasLength(EType::Float) );

		EXPECT_TRUE( my.HasUnsigned() );
		EXPECT_FALSE( my.HasCatalogs() );
		EXPECT_TRUE( my.HasProcs() );
		EXPECT_FALSE( my.NeedsIdentityInsert() );
		EXPECT_EQ( my.IdentityColumnSyntax(), "AUTO_INCREMENT" );
		EXPECT_EQ( my.IdentitySelect(), "LAST_INSERT_ID()" );
		EXPECT_EQ( my.SysSchema(), "sys" );
		EXPECT_EQ( my.GuidType(), "binary" );
		EXPECT_EQ( my.NowDefault(), "CURRENT_TIMESTAMP" );
	}

	//Sync refreshes an existing generated insert proc rather than skipping it, so each dialect has to say how it is
	//replaced.  MySQL has no create-or-replace for procedures: the drop is a separate statement sync runs first, where
	//the base (SQL Server) dialect replaces in place and needs none.
	TEST( SyntaxTests, ProcRefresh ){
		const auto& my = MySqlSyntax::Instance();
		EXPECT_EQ( my.CreateProcSql(), "create procedure" );
		EXPECT_EQ( my.DropProcSql("s.`p`"), "drop procedure if exists s.`p`" );

		const Syntax base;
		EXPECT_EQ( base.CreateProcSql(), "create or alter procedure" );
		EXPECT_TRUE( base.DropProcSql("s.p").empty() );
	}

	//#10: a save cannot branch on an UPDATE's row count, because MySQL alone reports rows *changed* - re-saving an
	//identical value answers 0.  The upsert suffix removes the branch.  No ctest suite connects to MySQL, so the text is
	//the only guard this form gets; `values(col)` rather than the 8.0.19 `as new` alias, which MariaDB does not accept.
	TEST( SyntaxTests, UpsertSuffix ){
		const auto& my = MySqlSyntax::Instance();
		EXPECT_EQ( my.UpsertSuffix({"identity_id","url"}, {"value"}), " on duplicate key update value=values(value)" );
		EXPECT_EQ( my.UpsertSuffix({"a"}, {"b","c"}), " on duplicate key update b=values(b), c=values(c)" );

		const Syntax base;
		EXPECT_TRUE( base.UpsertSuffix({"a"}, {"b"}).empty() ); //SQL Server has no such form - the caller keeps update-then-insert.
	}

	//#241: a changed default on an existing column.  One statement - SET DEFAULT replaces whatever is there - but it takes
	//a literal or a parenthesized expression, so `$now` goes in parens where the create statement writes the bare keyword.
	TEST( SyntaxTests, SetDefault ){
		const auto& my = MySqlSyntax::Instance();
		EXPECT_EQ( my.SetDefault("widgets", "color", Value{"blue"}, {}), vector<string>{"ALTER TABLE widgets ALTER COLUMN color SET DEFAULT 'blue'"} );
		EXPECT_EQ( my.SetDefault("widgets", "flag", Value{true}, {}), vector<string>{"ALTER TABLE widgets ALTER COLUMN flag SET DEFAULT true"} );
		EXPECT_EQ( my.SetDefault("widgets", "created", Value{"$now"}, {}), vector<string>{"ALTER TABLE widgets ALTER COLUMN created SET DEFAULT (CURRENT_TIMESTAMP)"} );
		EXPECT_EQ( my.DefaultLiteral(Value{true}), "true" ); //MySQL's own bit spelling - the base dialect's is 1.
		EXPECT_EQ( my.DefaultLiteral(Value{"$now"}), "CURRENT_TIMESTAMP" );
	}

	//#241: INFORMATION_SCHEMA.COLUMNS reports a default without the quotes or spelling the ddl gave it (a live 8.4
	//census: `abc`, `b'1'`, `5`, `CURRENT_TIMESTAMP`, and `now()` for the parenthesized form SET DEFAULT needs), so the
	//loader restores DefaultLiteral's spelling - or the reconciliation would rewrite every string default at every start.
	TEST( ServerMetaTests, MySqlDefaultReadsBackTheDdlSpelling ){
		using enum EType;
		const auto& my = MySqlSyntax::Instance();
		EXPECT_EQ( MySqlDefault("abc", false, VarChar, my), "'abc'" );
		EXPECT_EQ( MySqlDefault("it's", false, VarChar, my), "'it''s'" );
		EXPECT_EQ( MySqlDefault("", false, Text, my), "''" );               //an empty-string default; NULL never reaches here.
		EXPECT_EQ( MySqlDefault("b'1'", false, Bit, my), "true" );
		EXPECT_EQ( MySqlDefault("b'0'", false, Bit, my), "false" );
		EXPECT_EQ( MySqlDefault("1", false, Bit, my), "true" );              //tinyint(1)
		EXPECT_EQ( MySqlDefault("5", false, Int, my), "5" );
		EXPECT_EQ( MySqlDefault("1.5", false, Float, my), "1.5" );
		EXPECT_EQ( MySqlDefault("CURRENT_TIMESTAMP", true, DateTime, my), "CURRENT_TIMESTAMP" );
		EXPECT_EQ( MySqlDefault("now()", true, DateTime, my), "CURRENT_TIMESTAMP" );
		EXPECT_EQ( MySqlDefault("2020-01-01 00:00:00", false, DateTime, my), "'2020-01-01 00:00:00'" ); //a literal datetime, not generated.
		EXPECT_EQ( MySqlDefault("uuid()", true, VarChar, my), "uuid()" );    //any other expression, as reported.
	}

	TEST( SyntaxTests, EscapeDdl ){
		EXPECT_EQ( MySqlSyntax::Instance().EscapeDdl("t"), "`t`" );          //backticks, where the base dialect uses [brackets].
		EXPECT_EQ( MySqlSyntax::Instance().EscapeDdl("s.t"), "`s`.`t`" );    //each part quoted separately.
	}

	//SqlName() quotes a reserved table name in generated sql - `groups` is reserved in mysql 8+ but fine with a prefix or in the base dialect.
	TEST( SyntaxTests, IsReservedWord ){
		EXPECT_TRUE( MySqlSyntax::Instance().IsReservedWord("groups") );
		EXPECT_FALSE( MySqlSyntax::Instance().IsReservedWord("access_groups") );
		EXPECT_FALSE( Syntax::Instance().IsReservedWord("groups") );
	}

	//MySQL is the only dialect with a regex engine, so `regex:` passes through; `glob:` becomes an anchored REGEXP rather
	//than a LIKE, because LIKE would drop the character classes.
	TEST( SyntaxTests, PatternOperators ){
		const auto& my = MySqlSyntax::Instance();
		EXPECT_EQ( my.PatternOperator(EOperator::Regex), "regexp" );
		EXPECT_EQ( my.PatternOperator(EOperator::Glob), "regexp" );
		EXPECT_THROW( my.PatternOperator(EOperator::ElementMatch), Exception );

		EXPECT_EQ( my.PatternParam(EOperator::Regex, "b.b"), "b.b" ); //verbatim - it is already a regex.
		EXPECT_EQ( my.PatternParam(EOperator::Glob, "*abc*"), "^.*abc.*$" );
		EXPECT_EQ( my.PatternParam(EOperator::Glob, "b?b"), "^b.b$" );
	}

	//glob -> regex is a total mapping, which is why it beats glob -> LIKE here.  Anchored, because glob matches the whole
	//value where REGEXP is a search.
	TEST( SyntaxTests, GlobToRegex ){
		EXPECT_EQ( GlobToRegex("*abc*"), "^.*abc.*$" );
		EXPECT_EQ( GlobToRegex("b?b"), "^b.b$" );
		EXPECT_EQ( GlobToRegex("bob"), "^bob$" );        //anchored: a bare name must not match "bobby".
		EXPECT_EQ( GlobToRegex("a.c"), "^a\\.c$" );      //'.' is literal in a glob - it must be escaped, not left as "any".
		EXPECT_EQ( GlobToRegex("a+b"), "^a\\+b$" );
		EXPECT_EQ( GlobToRegex("(x)"), "^\\(x\\)$" );
		EXPECT_EQ( GlobToRegex("[a-c]at"), "^[a-c]at$" ); //classes are spelled the same in both languages.
		EXPECT_EQ( GlobToRegex("[!a-c]at"), "^[^a-c]at$" );//glob's '!' negation becomes regex '^'.
		EXPECT_EQ( GlobToRegex("[^a-c]at"), "^[^a-c]at$" );
		EXPECT_EQ( GlobToRegex("[]x]y"), "^[]x]y$" );     //C9: a ']' first in the class is a member, and POSIX spells it the same way.
		EXPECT_EQ( GlobToRegex("[abc"), "^\\[abc$" );     //unterminated class -> a literal '[', as in sqlite.
	}

	TEST( SyntaxTests, Limit ){
		const auto& my = MySqlSyntax::Instance();
		const auto sql = string{ "select * from t" };
		EXPECT_EQ( my.Limit(sql, 10, 0), "select * from t limit 10" ); //limit-only.
		EXPECT_EQ( my.Limit(sql, 10, 5), "select * from t limit 10 offset 5" ); //limit + skip.
		//OFFSET requires a LIMIT, so skip-only sends 2^64-1 ("all rows") rather than 'limit 0', which returns none.
		EXPECT_EQ( my.Limit(sql, 0, 5), "select * from t limit 18446744073709551615 offset 5" );
	}
}
