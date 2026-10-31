#include <gtest/gtest.h>
#include <jde/db/db.h>
#include <jde/db/generators/FromClause.h>
#include <jde/db/generators/InsertClause.h>
#include <jde/db/generators/Object.h>
#include <jde/db/generators/Sql.h>
#include <jde/db/generators/Syntax.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/db/meta/Column.h>
#include <jde/db/meta/Table.h>
#include "../src/meta/ddl/ColumnDdl.h" //#39: the deleted-overload guard.
#include "../src/meta/ServerMetaFold.h" //#241: StripParens.
#include "../src/meta/ddl/SchemaDdl.h" //#520: StripHashComments.

namespace Jde::DB::Tests{
	//#10: SchemaDdl::Sync now delegates to ObjectPrefix (was a swapped-ternary duplicate that yielded "" for a "db.um_" prefix). Lock in ObjectPrefix.
	TEST( AppSchemaTests, ObjectPrefix ){
		EXPECT_EQ( (AppSchema{ "s", {}, "db.um_" }).ObjectPrefix(), "um_" ); //MSSQL-style schema.prefix -> the object prefix after the dot.
		EXPECT_EQ( (AppSchema{ "s", {}, "um_" }).ObjectPrefix(), "um_" );    //no dot -> unchanged.
		EXPECT_EQ( (AppSchema{ "s", {}, "" }).ObjectPrefix(), "" );          //empty -> empty.
	}

	//#26: InsertClause::Proc placeholder count must match the params (was a leading "(?" -> zero Values gave `name(?)` with 0 params), and SequenceColumn must not deref a null column.
	TEST( InsertClauseTests, ProcParamCountMatches ){
		EXPECT_EQ( InsertClause( "p", vector<Value>{} ).Move().Text, "p()" );          //0 values -> no placeholder (was "p(?)").
		EXPECT_TRUE( InsertClause( "p", vector<Value>{} ).Move().Params.empty() );
		auto sql = InsertClause( "p", vector<Value>{ Value{1}, Value{2} } ).Move();
		EXPECT_EQ( sql.Text, "p(?,?)" );
		EXPECT_EQ( sql.Params.size(), 2u );
	}
	TEST( InsertClauseTests, SequenceColumnNullColumnSafe ){
		InsertClause ins{ "p", vector<Value>{ Value{1} } }; //proc ctor -> Values have null columns.
		EXPECT_EQ( ins.SequenceColumn(), nullptr );          //was a null-column ->Table deref (segfault).
	}

	//#33: getMap() runs from Table's member-init list and FindColumn's "id" alias reads SurrogateKeys, which used to be declared *after* Map - so the read was of a not-yet-constructed vector.
	//The surrogate key is deliberately not named "id": with the old order the garbage size() sends FindColumn to the Columns search, which has no "id", and GetColumnPtr throws.
	TEST( TableTests, MapIdAliasResolvesSurrogateKey ){
		const auto j = Json::Parse( R"({"columns":{"entity_id":{"sk":0,"i":0},"member_id":{"i":1}},"map":{"parentId":"id","childId":"member_id"}})" );
		const Table t{ "m", j };
		ASSERT_EQ( t.SurrogateKeys.size(), 1u );
		ASSERT_TRUE( t.Map.has_value() );
		EXPECT_EQ( t.Map->Parent, t.SurrogateKeys[0] ); //"id" resolved through the surrogate key, not by column name.
		EXPECT_EQ( t.Map->Parent->Name, "entity_id" );
		EXPECT_EQ( t.Map->Child->Name, "member_id" );
	}

	//#39: MySQL's loadTables passed `isId!=0` where ColumnDdl takes `optional<uint8> skIndex`, and a bool converts to an
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

	//#520: a `#` inside a quote is text, not a comment - a security policy uri ends in `#None` - and a `--` or `/* */` comment's
	//apostrophe opens no quote (review #6).  The newline that ends a `#` comment stays.
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

	//#241: the compare SchemaDdl's syncDefault runs - true leaves the column alone.  #9: MySQL reports a decimal at its
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

	//db-refactor B1: the placeholder ctors - Column::Count, a criteria column, the pkTable/extends/qlView stand-ins, every
	//loader-built TableDdl - left the bools and the EType indeterminate, and SelectClause::ToString reads Count()'s Type.
	//Default member initialisers answer the configured column's own defaults; this pins them (it cannot fail on a build
	//that happens to zero the frame, but it says what the answers are).
	TEST( ColumnTests, PlaceholdersAreDeterminate ){
		const auto count = Column::Count();
		EXPECT_EQ( count->Type, EType::None );
		EXPECT_FALSE( count->IsNullable ); EXPECT_FALSE( count->IsSequence );
		EXPECT_TRUE( count->Insertable ); EXPECT_TRUE( count->Updateable ); //the json ctor's value_or defaults.
		const Table v{ "placeholder" };
		EXPECT_FALSE( v.HasCustomInsertProc ); EXPECT_FALSE( v.IsFlags );
		EXPECT_TRUE( v.Operations==Access::ERights{} );
	}

	//ql-review3 #3: TryAdd read join.To->Table->Name unconditionally, but a null To is a legal Join - it is the single-table
	//form operator+= merges into, and Contains/GetColumnPtr both allow for it.  A sub-table whose fk could not be resolved
	//passed one in and segfaulted here rather than at the caller.
	TEST( FromClauseTests, TryAddNullToDoesNotDereference ){
		FromClause from;
		from.TryAdd( Join{ms<Column>("a"), nullptr, false} );
		ASSERT_EQ( from.Joins.size(), 1u );
		EXPECT_FALSE( from.HasJoin() ); //a single-table from clause, not a join.
	}

	TEST( ObjectTests, ValueEquality ){
		const DB::Object a = DB::Value{5}, b = DB::Value{5}, c = DB::Value{6};
		EXPECT_TRUE( a==b );
		EXPECT_FALSE( a==c );

		const DB::Object v1 = vector<DB::Value>{ DB::Value{1}, DB::Value{2} };
		const DB::Object v2 = vector<DB::Value>{ DB::Value{1}, DB::Value{2} };
		const DB::Object v3 = vector<DB::Value>{ DB::Value{1} };
		EXPECT_TRUE( v1==v2 );
		EXPECT_FALSE( v1==v3 );
		EXPECT_FALSE( a==v1 ); //different alternatives.
	}

	//#25: an Object holding Values must render placeholders (?,?), not the literal `[ 1, 2]` - GetParams appends the values, so the counts must match.
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

	TEST( ObjectTests, ValuesRendersPlaceholders ){
		DB::Object o = vector<DB::Value>{ DB::Value{1}, DB::Value{2}, DB::Value{3} };
		EXPECT_EQ( DB::ToString(o), "(?,?,?)" );
		EXPECT_EQ( DB::GetParams(o).size(), 3u ); //placeholder count now matches param count.
	}
}