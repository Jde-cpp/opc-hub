#include <gtest/gtest.h>
#include <jde/db/Value.h>
#include <jde/db/generators/Syntax.h>

namespace Jde::DB::Odbc::Tests{
	//There is no MsSqlSyntax type - the base DB::Syntax *is* the SQL Server dialect, and OdbcDataSource returns it
	//unchanged.  These pin that dialect the way the mysql driver's SyntaxTests pin theirs; no connection is opened.
	TEST( SyntaxTests, SqlServerDialect ){
		const auto& s = Syntax::Instance();
		EXPECT_EQ( s.IdentityColumnSyntax(), "identity(1001,1)" );
		EXPECT_EQ( s.IdentitySelect(), "@@identity" );
		EXPECT_EQ( s.SysSchema(), "dbo" );
		EXPECT_EQ( s.GuidType(), "uniqueidentifier" );
		EXPECT_EQ( s.NowDefault(), "getutcdate()" );
		EXPECT_EQ( s.ProcParameterPrefix(), "@" );

		EXPECT_TRUE( s.HasSchemas() );
		EXPECT_TRUE( s.HasCatalogs() );
		EXPECT_TRUE( s.HasProcs() );
		EXPECT_TRUE( s.CanAddForeignKeys() );
		EXPECT_TRUE( s.NeedsIdentityInsert() );
		EXPECT_FALSE( s.HasUnsigned() ); //no unsigned integers - uint columns fall back to the signed width.
		EXPECT_EQ( s.ToString(EType::UInt), "int" );
		EXPECT_EQ( s.ToString(EType::ULong), "bigint" );
	}

	//#241: a default is a constraint of its own on SQL Server, so changing one is a drop by the name the loader read
	//(sys.default_constraints.name) and an add, in one transaction; a column with none skips the drop.  The add names its
	//constraint, which is what a second change drops.
	TEST( SyntaxTests, SetDefault ){
		const auto& s = Syntax::Instance();
		EXPECT_EQ( s.SetDefault("dbo.widgets", "color", Value{"blue"}, "DF__widgets__color__3B"), "set xact_abort on; begin tran; alter table dbo.widgets drop constraint DF__widgets__color__3B; alter table dbo.widgets add constraint DF_widgets_color default 'blue' for color; commit" );
		EXPECT_EQ( s.SetDefault("dbo.widgets", "created", Value{"$now"}, {}), "alter table dbo.widgets add constraint DF_widgets_created default getutcdate() for created" );
		EXPECT_EQ( s.DefaultConstraint("dbo.widgets", "color"), "DF_widgets_color" );
	}

	TEST( SyntaxTests, EscapeDdl ){
		EXPECT_EQ( Syntax::Instance().EscapeDdl("t"), "[t]" );        //bracket quoting, where mysql uses `backticks`.
		EXPECT_EQ( Syntax::Instance().EscapeDdl("s.t"), "[s].[t]" );  //each part quoted separately.
	}

	//SQL Server has LIKE but no regex before 2025, so `glob:` translates and `regex:` is refused at build time rather
	//than sent to the server to fail.
	TEST( SyntaxTests, PatternOperators ){
		const auto& s = Syntax::Instance();
		EXPECT_EQ( s.PatternOperator(EOperator::Glob), "like" );
		EXPECT_THROW( s.PatternOperator(EOperator::Regex), Exception );
		EXPECT_THROW( s.PatternOperator(EOperator::ElementMatch), Exception );
		EXPECT_EQ( s.PatternParam(EOperator::Glob, "*abc*"), "%abc%" );
		EXPECT_THROW( s.PatternParam(EOperator::Regex, "b.b"), Exception );
	}

	//T-SQL LIKE is glob with two characters renamed.  A literal '%'/'_' has to be bracket-escaped, which is also how
	//QL::globMatch reads it, so the same pattern text means the same thing on both sides.
	TEST( SyntaxTests, GlobToLike ){
		EXPECT_EQ( GlobToLike("*abc*"), "%abc%" );
		EXPECT_EQ( GlobToLike("b?b"), "b_b" );
		EXPECT_EQ( GlobToLike("bob"), "bob" );
		EXPECT_EQ( GlobToLike("a.c"), "a.c" );          //'.' is not a metacharacter in either language.
		EXPECT_EQ( GlobToLike("50%"), "50[%]" );        //a literal '%' would otherwise become a wildcard.
		EXPECT_EQ( GlobToLike("a_b"), "a[_]b" );        //…and a literal '_' a single-character wildcard.
		EXPECT_EQ( GlobToLike("[a-c]at"), "[a-c]at" );  //T-SQL LIKE has classes - they survive.
		EXPECT_EQ( GlobToLike("[!a-c]at"), "[^a-c]at" );//glob accepts '!' for negation, T-SQL only '^'.
		EXPECT_EQ( GlobToLike("[^a-c]at"), "[^a-c]at" );
		EXPECT_EQ( GlobToLike("[]x]y"), "[]x]y" );      //C9: a ']' first in the class is a member - the body passes through verbatim.
		EXPECT_EQ( GlobToLike("[abc"), "[[]abc" );      //unterminated -> a literal '[', which LIKE also has to escape.
	}

	TEST( SyntaxTests, Limit ){
		const auto& s = Syntax::Instance();
		const auto sql = string{ "select * from t order by id" };
		//T-SQL: OFFSET is mandatory before FETCH, so it is emitted even when skip is 0.
		EXPECT_EQ( s.Limit(sql, 10, 0), "select * from t order by id offset 0 rows fetch next 10 rows only" );
		EXPECT_EQ( s.Limit(sql, 10, 5), "select * from t order by id offset 5 rows fetch next 10 rows only" );
		EXPECT_EQ( s.Limit(sql, 0, 5), "select * from t order by id offset 5 rows" ); //skip-only: no FETCH clause.
	}
}