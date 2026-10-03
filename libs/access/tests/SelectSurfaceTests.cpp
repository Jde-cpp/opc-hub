//ql-review3 #60: the SQL-touching select surface that no suite reached.  FilterTests covers Filter::Test - the in-memory
//half - and says so in its own header ("ToWhereClause (the SQL half) needs a DB::Table, so it is not covered here"); the
//dialect suites assert generated strings.  Between them, nothing issued a query that pushed a pattern, a page, or an
//array-under-a-scalar-operator through a real database.  Everything below runs against sqlite `:memory:`, which is what
//Jde.Access.Tests already is, and each case is one end-to-end query.
//Not repeated here, because the findings that own them already wrote them: the mis-ordered parent key (#22) is
//SubTableKeyTests, the unauthorized executer on a qlView table (#10) is QlViewAuthorizeTests, and `__schema{ mutationType }`
//(#31) is SchemaIntrospectionTests.
#include "gtest/gtest.h"
#include <future>
#include <thread>
#include <jde/db/IDataSource.h>
#include <jde/db/generators/Syntax.h>
#include <jde/ql/QLAwait.h>
#include "globals.h"

#define let const auto

namespace Jde::Access::Tests{
	using namespace Json;

	//#12's hazard, which is why two of these tests do not just EXPECT_THROW.  SelectSubTables is a fire-and-forget Task:
	//before that fix a throw inside it never resumed the awaiting thread, so the failure mode is a *hang*, not an
	//assertion - and QuerySync is a BlockAwait, so a hang on the test thread takes the whole binary down with no summary.
	//Run the query detached against a deadline: a regression then reports as a failing test.  The thread is never joined
	//(joining is the very thing that would hang); the promise is kept alive by the lambda's own copy.
	//nullopt = never came back;  "" = returned without throwing;  otherwise the message.
	Ω queryWithin( string ql, UserPK executer, Duration deadline=10s )ι->optional<string>{
		auto state = ms<std::promise<string>>();
		auto future = state->get_future();
		std::thread{ [state, ql=move(ql), executer]{
			try{
				QL().QuerySync<jvalue>( ql, {}, executer );
				state->set_value( {} );
			}
			catch( const std::exception& e ){ state->set_value( e.what() ); }
			catch( ... ){ state->set_value( "unknown exception" ); }
		} }.detach();
		return future.wait_for( deadline )==std::future_status::ready ? optional<string>{ future.get() } : nullopt;
	}

	class SelectSurfaceTests : public ::testing::Test{
	protected:
		Ω names( str ql )ε->vector<string>{
			vector<string> y;
			for( let& row : QL().QuerySync<jarray>(ql, {}, GetRoot()) )
				y.emplace_back( AsSV(row.get_object(), "loginName") );
			return y;
		}
		//The two refusal tests pin what a dialect with no regex operator does - sqlite, which ctest runs, and SQL Server.  mysql
		//has one, so when the suite is run directly against it there is nothing to refuse and they are skipped.
		Ω hasRegex()ι->bool{
			try{ DS().Syntax().PatternOperator( DB::EOperator::Regex ); return true; }
			catch( const std::exception& ){ return false; }
		}
	};

	//`glob:` is the only pattern operator sqlite takes, and it takes it verbatim - SqliteSyntax::PatternOperator returns
	//"glob" and PatternParam passes the pattern through, so the where clause and QL::globMatch read the same language.
	//This is the first test in the tree to send one to a database at all.
	TEST_F( SelectSurfaceTests, GlobFilterSelectsThroughSql ){
		let root = GetRoot();
		let alpha = GetId( GetUser("review60-alpha", root) );
		let beta = GetId( GetUser("review60-beta", root) );

		EXPECT_EQ( names(R"(users( loginName:{glob:"review60-a*"} ){ id loginName })"), (vector<string>{"review60-alpha"}) );
		EXPECT_EQ( names(R"(users( loginName:{glob:"review60-alph?"} ){ id loginName })"), (vector<string>{"review60-alpha"}) ) << "'?' is one character, not a run";
		//a class, which is where a LIKE rewrite would part company with glob - sqlite reads '[ab]' as this driver's
		//globMatch does, so the pushed-down clause and the in-memory one agree.
		EXPECT_EQ( names(R"(users( loginName:{glob:"review60-[ab]*"}, orderBy:"loginName" ){ id loginName })"), (vector<string>{"review60-alpha","review60-beta"}) );
		EXPECT_TRUE( names(R"(users( loginName:{glob:"review60-z*"} ){ id loginName })").empty() );

		PurgeUser( UserPK{alpha}, root );
		PurgeUser( UserPK{beta}, root );
	}

	//sqlite parses REGEXP but ships no implementation and this driver registers no udf, so the statement would die at
	//execution with "no such function".  SqliteSyntax refuses it at build time instead - and the refusal has to reach the
	//caller rather than the log, which is what this pins.
	TEST_F( SelectSurfaceTests, RegexFilterIsRefusedWithATellingMessage ){
		if( hasRegex() )
			GTEST_SKIP() << "this dialect has a regex operator - nothing is refused.";
		try{
			QL().QuerySync<jarray>( R"(users( loginName:{regex:"^review60"} ){ id })", {}, GetRoot() );
			ADD_FAILURE() << "sqlite has no regexp - the select cannot have succeeded";
		}
		catch( const Exception& e ){
			let what = string{ e.what() };
			EXPECT_NE( what.find("regex"), string::npos ) << what;
			EXPECT_NE( what.find("glob"), string::npos ) << what << " - the message should name the operator that does work";
		}
	}

	//#12's catch, made permanent - the temporary TU that verified that fix was deleted with it.  The filter is on the
	//*parent*: SelectSubTables copies the parent's where into each sub-statement and renders that statement itself, so a
	//clause sqlite refuses throws inside the fire-and-forget Task rather than in Query()'s synchronous prologue.  With the
	//catch removed this does not fail, it hangs - hence the deadline.
	TEST_F( SelectSurfaceTests, AParentFilterSqliteRefusesThrowsRatherThanHangingTheSubTableSelect ){
		if( hasRegex() )
			GTEST_SKIP() << "this dialect has a regex operator - nothing is refused.";
		let outcome = queryWithin( R"(roles( slug:{regex:"a"} ){ id permissions{ id } })", GetRoot() );
		ASSERT_TRUE( outcome ) << "the request never came back - SelectSubTables swallowed the throw again (#12)";
		ASSERT_NE( *outcome, "" ) << "sqlite has no regexp; the sub-table select cannot have succeeded";
		EXPECT_NE( outcome->find("regex"), string::npos ) << *outcome;
		//the control: the same shape with an operator sqlite does have still answers, so the guard did not break sub-tables.
		let control = queryWithin( R"(roles( slug:{glob:"*"} ){ id permissions{ id } })", GetRoot() );
		ASSERT_TRUE( control );
		EXPECT_EQ( *control, "" ) << *control;
	}

	//an unknown column under a sub-table.  #12 reproduced this one through SelectSubTables, but columnSql recurses into
	//qlTable.Tables itself, so the parent's SelectStatement resolves the child's columns first and the THROW_IF lands in
	//Query()'s own try - synchronous, and unaffected by the fire-and-forget catch.  Kept because the message reaching the
	//caller is the point either way, and because which path answers it is exactly what would drift.
	TEST_F( SelectSurfaceTests, AnUnknownSubTableColumnIsNamedToTheCaller ){
		let outcome = queryWithin( "roles{ id permissions{ id bogus } }", GetRoot() );
		ASSERT_TRUE( outcome ) << "the request never came back";
		ASSERT_NE( *outcome, "" ) << "'bogus' is not a column of permissions";
		EXPECT_NE( outcome->find("bogus"), string::npos ) << *outcome << " - the caller should be told which column";
		let control = queryWithin( "roles{ id permissions{ id } }", GetRoot() );
		ASSERT_TRUE( control );
		EXPECT_EQ( *control, "" ) << *control;
	}

	//limit/offset/orderBy are the one filter group ToWhereClause deliberately skips - SelectStatement applies them to the
	//statement instead.  Nothing exercised that split.
	TEST_F( SelectSurfaceTests, LimitOffsetAndSkipPageTheResult ){
		let root = GetRoot();
		vector<uint32> created;
		for( let& slug : {"review60-page-1","review60-page-2","review60-page-3"} )
			created.push_back( GetId(GetUser(slug, root)) );
		const string all{ R"(users( loginName:{glob:"review60-page-*"}, orderBy:"loginName")" };

		EXPECT_EQ( names(all+R"( ){ id loginName })"), (vector<string>{"review60-page-1","review60-page-2","review60-page-3"}) );
		EXPECT_EQ( names(all+R"(, limit:2 ){ id loginName })"), (vector<string>{"review60-page-1","review60-page-2"}) );
		//offset with no limit: sqlite needs a LIMIT for an OFFSET, and SqliteSyntax supplies `limit -1` for exactly this.
		EXPECT_EQ( names(all+R"(, offset:1 ){ id loginName })"), (vector<string>{"review60-page-2","review60-page-3"}) );
		EXPECT_EQ( names(all+R"(, limit:1, offset:1 ){ id loginName })"), (vector<string>{"review60-page-2"}) );
		EXPECT_EQ( names(all+R"(, limit:1, skip:2 ){ id loginName })"), (vector<string>{"review60-page-3"}) ) << "'skip' is Input::Offset's other spelling";
		//descending is an object, not the string form - OrderByJson reads "desc" out of the value, so the page comes off the other end.
		EXPECT_EQ( names(R"(users( loginName:{glob:"review60-page-*"}, orderBy:{loginName:"desc"}, limit:1 ){ id loginName })"), (vector<string>{"review60-page-3"}) );

		for( let& id : created )
			PurgeUser( UserPK{id}, root );
	}

	//db-review3 #22: `eq:["a","b"]` rendered one '?' against two bound params.  sqlite answered SQLITE_RANGE and mysql
	//"wrong number of parameters"; the zero case was worse - sqlite ran the statement with the placeholder left null and
	//returned nothing at all.  FormatOperator counts now, and the count has to survive the whole ql path to the driver.
	TEST_F( SelectSurfaceTests, AnArrayUnderAScalarOperatorIsRefused ){
		try{
			QL().QuerySync<jarray>( R"(users( loginName:{eq:["review60-alpha","review60-beta"]} ){ id })", {}, GetRoot() );
			ADD_FAILURE() << "'=' cannot take two values";
		}
		catch( const Exception& e ){
			EXPECT_NE( string{e.what()}.find("single value"), string::npos ) << e.what();
		}
		//`in:` is the operator that does take a list, and it is the control: the same two literals select through it.
		EXPECT_NO_THROW( QL().QuerySync<jarray>(R"(users( loginName:{in:["review60-alpha","review60-beta"]} ){ id })", {}, GetRoot()) );
	}
}
