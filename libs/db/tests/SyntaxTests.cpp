#include <gtest/gtest.h>
#include <jde/db/generators/Syntax.h>

namespace Jde::DB::Tests{
	//There is no MsSqlSyntax type - the base DB::Syntax *is* the SQL Server dialect, and OdbcDataSource returns it
	//unchanged.  It lives in Jde.DB, so it is pinned here, on every platform, rather than in the WIN32-only odbc suite.
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

	TEST( SyntaxTests, EscapeDdl ){
		EXPECT_EQ( Syntax::Instance().EscapeDdl("t"), "[t]" );        //bracket quoting, where mysql uses `backticks`.
		EXPECT_EQ( Syntax::Instance().EscapeDdl("s.t"), "[s].[t]" );  //each part quoted separately.
	}

	TEST( SyntaxTests, Limit ){
		const auto& s = Syntax::Instance();
		const auto sql = string{ "select * from t order by id" };
		//T-SQL: OFFSET is mandatory before FETCH, so it is emitted even when skip is 0.
		EXPECT_EQ( s.Limit(sql, 10, 0), "select * from t order by id offset 0 rows fetch next 10 rows only" );
		EXPECT_EQ( s.Limit(sql, 10, 5), "select * from t order by id offset 5 rows fetch next 10 rows only" );
		EXPECT_EQ( s.Limit(sql, 0, 5), "select * from t order by id offset 5 rows" ); //skip-only: no FETCH clause.
	}

	//#241: one spelling of a configured `default:` for the create statement and for the reconciliation of an existing
	//table, so what the one writes the other recognises when the loader reads it back.  Strings used to go in verbatim -
	//`default abc` - which only ever parsed by luck; `'` doubles.  The base dialect is SQL Server's.
	TEST( SyntaxTests, DefaultLiteral ){
		const Syntax s;
		EXPECT_EQ( s.DefaultLiteral(Value{true}), "1" );
		EXPECT_EQ( s.DefaultLiteral(Value{false}), "0" );
		EXPECT_EQ( s.DefaultLiteral(Value{"abc"}), "'abc'" );
		EXPECT_EQ( s.DefaultLiteral(Value{"it's"}), "'it''s'" );
		EXPECT_EQ( s.DefaultLiteral(Value{"$now"}), "getutcdate()" );      //NowDefault, not a string.
		EXPECT_EQ( s.DefaultLiteral(Value{5}), "5" );
		EXPECT_EQ( s.DefaultLiteral(Value{(int8_t)-2}), "-2" );
		EXPECT_EQ( s.DefaultLiteral(Value{1.5}), "1.5" );
		EXPECT_TRUE( s.DefaultLiteral(Value{}).empty() );                    //a nullable column's implicit null: no default.
		EXPECT_TRUE( s.DefaultLiteral(Value{vector<uint8_t>{}}).empty() );  //Column::getDefault's nullable-binary placeholder.
		EXPECT_THROW( s.DefaultLiteral(Value{vector<uint8_t>{1}}), Exception );
		EXPECT_THROW( s.DefaultLiteral(Value{DBTimePoint{}}), Exception );
		EXPECT_EQ( s.QuoteLiteral(""), "''" );
		EXPECT_EQ( s.DefaultLiteral(Value{"C:\\temp"}), "'C:\\temp'" ); //a backslash is no escape here - MySQL's is.
	}

	//#241: a changed default on an existing column.  T-SQL keeps a column's default as a constraint of its own, so one
	//already there is dropped by the name the loader read before the new one is added, in one transaction; a column with
	//none just gets the add.  The add names its constraint, so a second change drops it by that name - not by the one
	//just dropped.
	TEST( SyntaxTests, SetDefault ){
		const Syntax s;
		EXPECT_EQ( s.SetDefault("dbo.widgets", "color", Value{"blue"}, "DF__widgets__color__3B"), "set xact_abort on; begin tran; alter table dbo.widgets drop constraint DF__widgets__color__3B; alter table dbo.widgets add constraint DF_widgets_color default 'blue' for color; commit" );
		EXPECT_EQ( s.SetDefault("dbo.widgets", "color", Value{"red"}, s.DefaultConstraint("dbo.widgets", "color")), "set xact_abort on; begin tran; alter table dbo.widgets drop constraint DF_widgets_color; alter table dbo.widgets add constraint DF_widgets_color default 'red' for color; commit" );
		EXPECT_EQ( s.SetDefault("dbo.widgets", "created", Value{"$now"}, {}), "alter table dbo.widgets add constraint DF_widgets_created default getutcdate() for created" );
		EXPECT_EQ( s.DefaultConstraint("widgets", "color"), "DF_widgets_color" ); //schema-scoped, so the same name with or without one.
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
		EXPECT_EQ( GlobToLike("[]x]y"), "[]x]y" );      //ql-review3 C9: a ']' first in the class is a member - the body passes through verbatim.
		EXPECT_EQ( GlobToLike("[abc"), "[[]abc" );      //unterminated -> a literal '[', which LIKE also has to escape.
	}

	//ql-review3 C9: the glob bracket-class grammar used to be written out three times - GlobToLike, GlobToRegex and
	//QL::matchClass.  It is one function now, so this is where its rules live.  `Body` excludes the '[', the negation and
	//the ']', and `Close` indexes the ']'.
	TEST( SyntaxTests, ParseGlobClass ){
		const auto parse = []( sv glob, uint open=0 ){ return ParseGlobClass( glob, open ); };
		auto c = parse( "[abc]" );          ASSERT_TRUE( c ); EXPECT_FALSE( c->Negate ); EXPECT_EQ( c->Body, "abc" ); EXPECT_EQ( c->Close, 4u );
		c = parse( "[a-c]" );               ASSERT_TRUE( c ); EXPECT_EQ( c->Body, "a-c" );   //ranges are body text - only the ql matcher interprets them.
		c = parse( "[!a-c]" );              ASSERT_TRUE( c ); EXPECT_TRUE( c->Negate ); EXPECT_EQ( c->Body, "a-c" ); EXPECT_EQ( c->Close, 5u );
		c = parse( "[^a-c]" );              ASSERT_TRUE( c ); EXPECT_TRUE( c->Negate ); EXPECT_EQ( c->Body, "a-c" ); //both spellings of negation.
		c = parse( "[]x]" );                ASSERT_TRUE( c ); EXPECT_EQ( c->Body, "]x" ); EXPECT_EQ( c->Close, 3u ); //a ']' first is a member, not the terminator.
		c = parse( "[!]x]" );               ASSERT_TRUE( c ); EXPECT_TRUE( c->Negate ); EXPECT_EQ( c->Body, "]x" ); //…including right after the negation.
		c = parse( "[a-]" );                ASSERT_TRUE( c ); EXPECT_EQ( c->Body, "a-" );    //a trailing '-' spans nothing, so it is a member.
		c = parse( "x[ab]y", 1 );           ASSERT_TRUE( c ); EXPECT_EQ( c->Body, "ab" ); EXPECT_EQ( c->Close, 4u ); //not only at index 0.
		EXPECT_FALSE( parse("[abc") );      //unterminated: the caller treats the '[' as a literal, as sqlite does.
		EXPECT_FALSE( parse("[") );
		EXPECT_FALSE( parse("[]") );        //that ']' is a member, so the class is still unterminated.
	}
}