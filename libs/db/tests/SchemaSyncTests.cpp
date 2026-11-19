#include <gtest/gtest.h>
#include "../src/meta/ServerMetaFold.h"
#include "../src/meta/ddl/SchemaDdl.h"

namespace Jde::DB::Tests{
	//db-review3 #39: MySQL's loadTables passed `isId!=0` where ColumnDdl takes `optional<uint8> skIndex`, and a bool converts to an
	//*engaged* optional{0} - "column 0 of the primary key", not "no primary key" - so every MySQL-loaded column claimed
	//to be the first pk column.  It sat next to `isIdentity!=0`, which is spelled identically and is genuinely a bool.
	//The all-bool spelling is now deleted, so getting it wrong is a compile error rather than a wrong answer.
	TEST( ColumnDdlTests, SkIndexRejectsBool ){
		static_assert( std::is_constructible_v<ColumnDdl, sv, sv, bool, EType, optional<uint>, bool, optional<uint8>, optional<uint>, optional<uint>>,
			"the real signature has to stay callable." );
		static_assert( !std::is_constructible_v<ColumnDdl, sv, sv, bool, EType, optional<uint>, bool, bool, optional<uint>, optional<uint>>,
			"a bool in skIndex's place must not compile - it means optional{0}, which is a pk claim." );

		const ColumnDdl noKey{ "c", {}, true, EType::Int, {}, false, optional<uint8>{}, {}, {} };
		EXPECT_FALSE( noKey.SKIndex.has_value() ); //what a non-pk column must report.
		const ColumnDdl firstKey{ "c", {}, true, EType::Int, {}, false, optional<uint8>{0}, {}, {} };
		ASSERT_TRUE( firstKey.SKIndex.has_value() );
		EXPECT_EQ( (uint)*firstKey.SKIndex, 0u ); //(uint): uint8 formats as a character.
	}

	//db-refactor A8: one ctor for every loader, and it keeps everything the server reported - the precision/scale pair
	//used to be discarded on the way in, the length defaulted to an engaged 0 on one dialect, and the two bools the
	//configured column initialises were left indeterminate.  #241: the default is the catalog's text, kept beside the
	//constraint name SQL Server gives it; the typed Column::Default is the configuration's alone.
	TEST( ColumnDdlTests, KeepsWhatTheLoaderPassed ){
		const ColumnDdl c{ "amount", "1", false, EType::Decimal, optional<uint>{}, false, optional<uint8>{}, optional<uint>{10}, optional<uint>{2}, "DF__t__amount__1" };
		EXPECT_EQ( c.DefaultSql, "1" ); EXPECT_EQ( c.DefaultName, "DF__t__amount__1" );
		EXPECT_FALSE( c.Default.has_value() );
		EXPECT_FALSE( c.IsNullable );
		EXPECT_EQ( c.Type, EType::Decimal );
		EXPECT_FALSE( c.MaxLength.has_value() ); //no length stays no length, not 0.
		EXPECT_EQ( c.NumericPrecision.value_or(0), 10u );
		EXPECT_EQ( c.NumericScale.value_or(0), 2u );
		EXPECT_FALSE( c.IsSequence );
		EXPECT_TRUE( c.Insertable ); EXPECT_TRUE( c.Updateable ); //the configured column's defaults.
		const ColumnDdl seq{ "id", {}, false, EType::UInt, {}, true, optional<uint8>{0}, {}, {} };
		EXPECT_FALSE( seq.Insertable ); //a sequence is not inserted, as Column's json ctor decides.
		EXPECT_TRUE( seq.DefaultSql.empty() ); EXPECT_TRUE( seq.DefaultName.empty() );
	}

	//#241: the catalogs wrap a default in parentheses of their own - SQL Server a number twice - which the compare has to
	//see through without unwrapping an expression whose first paren closes early.
	TEST( ServerMetaFoldTests, StripParens ){
		EXPECT_EQ( StripParens("((1))"), "1" );
		EXPECT_EQ( StripParens("('abc')"), "'abc'" );
		EXPECT_EQ( StripParens("(getutcdate())"), "getutcdate()" );
		EXPECT_EQ( StripParens("getutcdate()"), "getutcdate()" ); //its own parens are not a wrapper.
		EXPECT_EQ( StripParens("(1)+(2)"), "(1)+(2)" );           //the first paren closes before the end.
		EXPECT_EQ( StripParens("((1)+(2))"), "(1)+(2)" );
		EXPECT_EQ( StripParens(""), "" );
		EXPECT_EQ( StripParens("()"), "" );
		EXPECT_EQ( StripParens("(x"), "(x" );
		EXPECT_EQ( StripParens("(':)')"), "':)'" );               //a paren inside a string is the string's.
		EXPECT_EQ( StripParens("('it''s )')"), "'it''s )'" );
		EXPECT_EQ( StripParens("(')')+(')')"), "(')')+(')')" );
	}

	//#241: the compare SchemaDdl's syncDefault runs - true leaves the column alone.  #241 review #9: MySQL reports a decimal at its
	//scale and a double its own way, so numbers compare as numbers, or the column was re-altered at every startup.
	TEST( ServerMetaFoldTests, SameDefault ){
		EXPECT_TRUE( SameDefault("1", "((1))") );
		EXPECT_TRUE( SameDefault("getutcdate()", "(GETUTCDATE())") );
		EXPECT_FALSE( SameDefault("'A'", "'a'") );
		EXPECT_FALSE( SameDefault("'blue'", "") );                          //none yet.
		EXPECT_TRUE( SameDefault("1.5", "1.50") );                          //decimal(10,2)
		EXPECT_TRUE( SameDefault("0", "0.00") );
		EXPECT_TRUE( SameDefault("-2.5", "(-2.50)") );
		EXPECT_TRUE( SameDefault("1e+20", "1e20") );                        //std::format's double against MySQL's.
		EXPECT_FALSE( SameDefault("1.5", "1.51") );
		EXPECT_FALSE( SameDefault("1.5", "2") );                            //a decimal(10,0) rounds what it was given - a real difference.
		EXPECT_FALSE( SameDefault("9007199254740993", "9007199254740992") ); //one double apart.
		EXPECT_FALSE( SameDefault("'1.5'", "1.50") );                       //a string is no number.
		EXPECT_FALSE( SameDefault("1.5", "1.5x") );
	}

	//#520: a `#` inside a quote is text, not a comment - a security policy uri ends in `#None` - and a `--` or `/* */` comment's
	//apostrophe opens no quote (#520 review #6).  The newline that ends a `#` comment stays.
	TEST( SchemaDdlTests, StripHashComments ){
		EXPECT_EQ( SchemaDdl::StripHashComments("# a comment\nselect 1"), "\nselect 1" );
		EXPECT_EQ( SchemaDdl::StripHashComments("select 1 # trailing\nselect 2"), "select 1 \nselect 2" );
		EXPECT_EQ( SchemaDdl::StripHashComments("select 'a#b' # c\n"), "select 'a#b' \n" );
		EXPECT_EQ( SchemaDdl::StripHashComments("select 'it''s #1'"), "select 'it''s #1'" );
		EXPECT_EQ( SchemaDdl::StripHashComments("-- it's #1\nselect 1 # x"), "-- it's #1\nselect 1 " );
		EXPECT_EQ( SchemaDdl::StripHashComments("select 'open # to the end"), "select 'open # to the end" );//an unclosed literal is the dialect's error, not a comment.
		EXPECT_EQ( SchemaDdl::StripHashComments("#"), "" );
		EXPECT_EQ( SchemaDdl::StripHashComments("/* don't */ select 'a#b' # c"), "/* don't */ select 'a#b' " );
		EXPECT_EQ( SchemaDdl::StripHashComments("/*/ it's # */ # c"), "/*/ it's # */ " );//`/*/` opens the comment, it does not close it.
		EXPECT_EQ( SchemaDdl::StripHashComments("/* open # to the end"), "/* open # to the end" );
		EXPECT_EQ( SchemaDdl::StripHashComments("select \"it's #1\" # c"), "select \"it's #1\" " );
		EXPECT_EQ( SchemaDdl::StripHashComments("select `it's #1` # c"), "select `it's #1` " );
		EXPECT_EQ( SchemaDdl::StripHashComments("select 'it\\'s #1' # c", true), "select 'it\\'s #1' " );//MySQL's escape
		EXPECT_EQ( SchemaDdl::StripHashComments("select 'C:\\' # c", false), "select 'C:\\' " );//elsewhere a backslash is text
		EXPECT_EQ( SchemaDdl::StripHashComments("select `a\\` # c", true), "select `a\\` " );//and in a MySQL identifier
	}
}