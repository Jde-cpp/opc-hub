//The in-memory half of QL::Filter - the predicate the subscription fan-out uses to decide whether a changed row
//still matches a live query.  ToWhereClause (the SQL half) needs a DB::Table, so it is not covered here.
#include <gtest/gtest.h>
#include <jde/db/Value.h>
#include <jde/fwk/chrono.h>
#include <jde/ql/types/FilterQL.h>
#include <jde/ql/types/Input.h>
#include <jde/ql/types/Parser.h>

#define let const auto

namespace Jde::QL::Tests{
	struct FilterInput final : Input{
		FilterInput( sv args )ε: Input{ Parser::ParseArgs(string{args}), ms<jobject>() }{}
		α JTableName()Ι->string override{ return "tests"; }
	};
	Ω filterOf( sv args )ε->Filter{ return FilterInput{args}.Filter(); }
	Ω filterValue( DB::EOperator op, jvalue v )ι->FilterValue{ return FilterValue{ op, move(v) }; }

	TEST( FilterTests, OperatorNamesRoundTrip ){
		using enum DB::EOperator;
		for( let op : {Equal, NotEqual, Regex, Glob, In, NotIn, Greater, GreaterOrEqual, Less, LessOrEqual, ElementMatch} )
			EXPECT_EQ( ToQLOperator(QL::ToString(op)), op ); //qualified: DB::ToString(EOperator) - the SQL spelling - is an equally good ADL match.
		EXPECT_EQ( QL::ToString(Equal), "eq" );
		EXPECT_EQ( QL::ToString(GreaterOrEqual), "gte" );
		EXPECT_EQ( ToQLOperator("notAnOperator"), Equal ); //unknown spellings degrade to equality.
	}

	TEST( FilterTests, EqualityOperators ){
		EXPECT_TRUE( filterValue(DB::EOperator::Equal, 42).Test(DB::Value{uint{42}}) );
		EXPECT_FALSE( filterValue(DB::EOperator::Equal, 42).Test(DB::Value{uint{43}}) );
		EXPECT_TRUE( filterValue(DB::EOperator::NotEqual, 42).Test(DB::Value{uint{43}}) );
		EXPECT_FALSE( filterValue(DB::EOperator::NotEqual, 42).Test(DB::Value{uint{42}}) );
		EXPECT_TRUE( filterValue(DB::EOperator::Equal, "bob").Test(DB::Value{string{"bob"}}) );
		EXPECT_FALSE( filterValue(DB::EOperator::Equal, "bob").Test(DB::Value{string{"alice"}}) );
	}

	//review3 #4: makeTimes runs from this constructor, and its `add` lambda is ι with a `catch( const runtime_error& )`.
	//Chrono::ToTimePoint read the zone with std::stoi, which throws std::invalid_argument - a *logic*_error - so an
	//ISO-shaped literal with a junk offset walked out of the lambda and terminated the process, anonymously, from
	//`providers(name:"2026-08-02T10:00:00+ab"){ id }`.  ToTimePoint now only throws Jde::Exception, which the lambda catches,
	//and the literal falls back to being an ordinary string - what an unparseable literal has always done.
	TEST( FilterTests, MalformedTimeLiteralIsJustAString ){
		let junk = filterValue( DB::EOperator::Equal, "2026-08-02T10:00:00+ab" );
		EXPECT_TRUE( junk.Test(DB::Value{string{"2026-08-02T10:00:00+ab"}}) );
		EXPECT_FALSE( junk.Test(DB::Value{string{"2026-08-02T10:00:00Z"}}) );
		EXPECT_FALSE( junk.Test(DB::Value{Chrono::ToTimePoint("2026-08-02T10:00:00Z")}) ); //no _times, so a time column cannot match it.
		EXPECT_NO_THROW( filterValue(DB::EOperator::In, jarray{"2026-08-02T10:00:00Z", "2026-08-02T10:00:00+ab"}) ); //one bad literal in an array drops them all.
		//#17: the other half of the repro - a junk *fraction* rather than a junk offset.  It must be as harmless as the offset is.
		EXPECT_NO_THROW( filterValue(DB::EOperator::Equal, "2026-08-02T10:00:00.x") );
		EXPECT_TRUE( filterValue(DB::EOperator::Equal, "2026-08-02T10:00:00.x").Test(DB::Value{string{"2026-08-02T10:00:00.x"}}) );

		let real = filterValue( DB::EOperator::Equal, "2026-08-02T10:00:00Z" ); //the control: a parseable literal is still compared chronologically.
		EXPECT_TRUE( real.Test(DB::Value{Chrono::ToTimePoint("2026-08-02T10:00:00Z")}) );
		EXPECT_FALSE( real.Test(DB::Value{Chrono::ToTimePoint("2026-08-02T10:00:01Z")}) );
	}

	TEST( FilterTests, RelationalOperators ){
		EXPECT_TRUE( filterValue(DB::EOperator::Greater, 41).Test(DB::Value{uint{42}}) );
		EXPECT_FALSE( filterValue(DB::EOperator::Greater, 42).Test(DB::Value{uint{42}}) );
		EXPECT_TRUE( filterValue(DB::EOperator::GreaterOrEqual, 42).Test(DB::Value{uint{42}}) );
		EXPECT_TRUE( filterValue(DB::EOperator::Less, 43).Test(DB::Value{uint{42}}) );
		EXPECT_FALSE( filterValue(DB::EOperator::Less, 42).Test(DB::Value{uint{42}}) );
		EXPECT_TRUE( filterValue(DB::EOperator::LessOrEqual, 42).Test(DB::Value{uint{42}}) );
	}

	TEST( FilterTests, UInt32ComparesLikeAnyOtherNumber ){
		EXPECT_TRUE( filterValue(DB::EOperator::Equal, 42).Test(DB::Value{uint32_t{42}}) );
		EXPECT_FALSE( filterValue(DB::EOperator::Equal, 42).Test(DB::Value{uint32_t{43}}) );
		EXPECT_TRUE( filterValue(DB::EOperator::Greater, 41).Test(DB::Value{uint32_t{42}}) );
		EXPECT_TRUE( filterValue(DB::EOperator::In, jarray{1,2,3}).Test(DB::Value{uint32_t{2}}) );
	}

	TEST( FilterTests, InAndNotIn ){
		let list = jarray{ 1, 2, 3 };
		EXPECT_TRUE( filterValue(DB::EOperator::In, list).Test(DB::Value{uint{2}}) );
		EXPECT_FALSE( filterValue(DB::EOperator::In, list).Test(DB::Value{uint{4}}) );
		EXPECT_TRUE( filterValue(DB::EOperator::NotIn, list).Test(DB::Value{uint{4}}) );
		EXPECT_FALSE( filterValue(DB::EOperator::NotIn, list).Test(DB::Value{uint{2}}) );
	}

	TEST( FilterTests, Regex ){
		EXPECT_TRUE( filterValue(DB::EOperator::Regex, "b.b").Test(DB::Value{string{"bob"}}) );
		EXPECT_FALSE( filterValue(DB::EOperator::Regex, "b.b").Test(DB::Value{string{"alice"}}) );
		EXPECT_TRUE( filterValue(DB::EOperator::Regex, "b.b").Test(DB::Value{string{"bib"}}) ); //the same compiled regex, reused - it is built in the ctor, not per Test().
	}

	//A pattern that can't be used matches nothing rather than throwing:  the ctor rejects it once instead of Test() throwing per row.
	TEST( FilterTests, UnusablePatternsMatchNothing ){
		bool matched{ true };
		EXPECT_NO_THROW( matched = filterValue(DB::EOperator::Regex, "[").Test(DB::Value{string{"bob"}}) ); //doesn't compile.
		EXPECT_FALSE( matched );
		EXPECT_FALSE( filterValue(DB::EOperator::Regex, 42).Test(DB::Value{string{"42"}}) );   //not a string.
		EXPECT_FALSE( filterValue(DB::EOperator::Glob, 42).Test(DB::Value{string{"42"}}) );
		let tooLongGlob = string( MaxPatternLength+1, 'a' );
		EXPECT_FALSE( filterValue(DB::EOperator::Glob, jstring{tooLongGlob}).Test(DB::Value{tooLongGlob}) );
		let tooLongRegex = string( MaxRegexLength+1, 'a' ); //#37: the regex limit is the smaller of the two.
		EXPECT_FALSE( filterValue(DB::EOperator::Regex, jstring{tooLongRegex}).Test(DB::Value{tooLongRegex}) );
	}

	//#37: boost's budget is N*S^2 states with S the *pattern length*, so MaxPatternLength=1024 admitted patterns that cost
	//seconds per 4KB subject - measured 12.3s for a 1000-character `.*` chain, on the log fan-out thread, under
	//SubscribeLog::Write's lock.  A regex now gets MaxRegexLength instead, and is refused before it is compiled.
	TEST( FilterTests, ALongRegexIsRefusedRatherThanRun ){
		let subject = string( 4000, 'a' )+"!"; //the '!' denies the match, so every split of the a's is tried.
		let chain = []( uint characters ){ string y; while( y.size()+3<=characters ) y += ".*"; return y+"="; };//'=' included in the count.
		let pattern = chain( 1000 );
		let start = std::chrono::steady_clock::now();
		EXPECT_FALSE( filterValue(DB::EOperator::Regex, jstring{pattern}).Test(DB::Value{subject}) );
		let elapsed = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()-start );
		EXPECT_LT( elapsed, std::chrono::milliseconds{50} ) << pattern.size() << "-character pattern took " << elapsed.count() << "ms";

		//and the worst one that is still admitted:  S is capped, so the cost is.  S^2 - a 16th of the length is a 256th of the
		//work.  Measured here (win-clang debug, this 4KB subject): 127 characters 894ms, 63 characters 220ms.
		let admitted = chain( MaxRegexLength );
		ASSERT_LE( admitted.size(), MaxRegexLength );
		auto worst = filterValue( DB::EOperator::Regex, jstring{admitted} );
		let start2 = std::chrono::steady_clock::now();
		EXPECT_FALSE( worst.Test(DB::Value{subject}) );
		let elapsed2 = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()-start2 );
		//and it is paid once, not per row:  a pattern that costs this much has exhausted the budget, which poisons it.
		let start3 = std::chrono::steady_clock::now();
		for( uint i{}; i<100; ++i )
			EXPECT_FALSE( worst.Test(DB::Value{subject}) );
		let elapsed3 = std::chrono::duration_cast<std::chrono::milliseconds>( std::chrono::steady_clock::now()-start3 );
		EXPECT_LT( elapsed3, elapsed2 ) << "100 more rows cost " << elapsed3.count() << "ms - the pattern was not poisoned";
		EXPECT_LT( elapsed2, std::chrono::milliseconds{500} ) << admitted.size() << "-character pattern took " << elapsed2.count() << "ms";
	}
	//glob keeps the longer limit:  globMatch walks the subject, it does not backtrack over a compiled machine.
	TEST( FilterTests, GlobKeepsTheLongerLimit ){
		let pattern = string( MaxRegexLength+1, '*' );
		ASSERT_LT( pattern.size(), MaxPatternLength );
		EXPECT_TRUE( filterValue(DB::EOperator::Glob, jstring{pattern}).Test(DB::Value{string{"anything"}}) );
	}

	//Catastrophically-backtracking patterns must come back promptly instead of pinning the fan-out thread.  All of these
	//are valid, tiny, and far inside MaxPatternLength, so the compile-side guards do not see them - only the engine's
	//own bounds do, which is the whole reason the operator is on boost::regex rather than std::regex.  The first two
	//shapes are stopped by boost's recursion-depth guard, the `.*`-chains by the N*S^2 budget (#37: not by
	//BOOST_REGEX_MAX_STATE_COUNT, which caps only the N^2 term).  On libc++, whose std::regex is unbounded, this never finishes.
	TEST( FilterTests, CatastrophicRegexIsBounded ){
		let subject = string( 42, 'a' )+"!"; //the trailing '!' denies the match, forcing every way to split the a's.
		for( let pattern : {"(a+)+$", "(a|a)*$", ".*.*.*.*.*.*=", "a*a*a*a*a*a*a*a*b"} ){
			let start = std::chrono::steady_clock::now();
			EXPECT_FALSE( filterValue(DB::EOperator::Regex, jstring{pattern}).Test(DB::Value{subject}) ) << pattern;
			EXPECT_LT( std::chrono::steady_clock::now()-start, std::chrono::seconds{5} ) << pattern << " was not bounded";
		}
	}

	//The bound is per match call, so the first abort poisons the pattern: one filter is tested against every log entry,
	//and SubscribeLog::Write would otherwise pay the whole budget - and it is a budget, not a hang - once per entry.
	TEST( FilterTests, BoundedRegexIsPoisonedAfterTheFirstAbort ){
		let subject = string( 42, 'a' )+"!";
		auto filter = filterValue( DB::EOperator::Regex, "(a+)+$" );
		EXPECT_FALSE( filter.Test(DB::Value{subject}) );
		let start = std::chrono::steady_clock::now();
		for( uint i{}; i<1000; ++i )
			EXPECT_FALSE( filter.Test(DB::Value{subject}) );
		EXPECT_LT( std::chrono::steady_clock::now()-start, std::chrono::milliseconds{100} ) << "1000 rows re-ran the aborted match instead of short-circuiting";
	}

	//The cap has to leave ordinary patterns room: a linear match over a long subject costs states too, and a filter that
	//starts returning false under load would be a far worse bug than the one the cap fixes.
	TEST( FilterTests, StateCapLeavesHeadroomForOrdinaryPatterns ){
		let haystack = string( 4000, 'x' )+"needle"+string( 4000, 'y' );
		EXPECT_TRUE( filterValue(DB::EOperator::Regex, ".*needle.*").Test(DB::Value{haystack}) );
		EXPECT_FALSE( filterValue(DB::EOperator::Regex, ".*absent.*").Test(DB::Value{haystack}) );
	}

	//glob is a glob, not a regex.  `*abc*` is not even a legal regex (nothing for the leading '*' to repeat), so this used
	//to throw inside Test() and report "no match" for everything.
	TEST( FilterTests, Glob ){
		let glob = []( sv pattern, sv value ){ return filterValue(DB::EOperator::Glob, jstring{pattern}).Test( DB::Value{string{value}} ); };
		EXPECT_TRUE( glob("*abc*", "xxabcyy") );
		EXPECT_TRUE( glob("*abc*", "abc") );
		EXPECT_FALSE( glob("*abc*", "xxabyy") );
		EXPECT_TRUE( glob("abc*", "abcdef") );
		EXPECT_FALSE( glob("abc*", "xabcdef") );
		EXPECT_TRUE( glob("*def", "abcdef") );
		EXPECT_TRUE( glob("a*b*c", "axxbyyc") );  //two stars, each backtracking independently.
		EXPECT_TRUE( glob("b?b", "bob") );
		EXPECT_FALSE( glob("b?b", "boob") );
		EXPECT_TRUE( glob("bob", "bob") );
		EXPECT_FALSE( glob("bob", "bobby") );
		EXPECT_TRUE( glob("*", "anything") );
		EXPECT_TRUE( glob("*", "") );
		EXPECT_FALSE( glob("", "notEmpty") );
		EXPECT_TRUE( glob("", "") );
	}

	//The metacharacters are the glob ones - a regex's are literal, which is the whole point of the operator being distinct.
	TEST( FilterTests, GlobMetacharactersAreLiteral ){
		let glob = []( sv pattern, sv value ){ return filterValue(DB::EOperator::Glob, jstring{pattern}).Test( DB::Value{string{value}} ); };
		EXPECT_TRUE( glob("a.c", "a.c") );
		EXPECT_FALSE( glob("a.c", "abc") ); //'.' is not "any character" here.
		EXPECT_TRUE( glob("a+b", "a+b") );
		EXPECT_TRUE( glob("(x)", "(x)") );
	}

	TEST( FilterTests, GlobCharacterClasses ){
		let glob = []( sv pattern, sv value ){ return filterValue(DB::EOperator::Glob, jstring{pattern}).Test( DB::Value{string{value}} ); };
		EXPECT_TRUE( glob("[bB]ob", "Bob") );
		EXPECT_TRUE( glob("[bB]ob", "bob") );
		EXPECT_FALSE( glob("[bB]ob", "rob") );
		EXPECT_TRUE( glob("[a-c]at", "bat") );
		EXPECT_FALSE( glob("[a-c]at", "hat") );
		EXPECT_TRUE( glob("[^a-c]at", "hat") );  //sqlite spells negation '^'…
		EXPECT_FALSE( glob("[!a-c]at", "bat") ); //…and '!' is accepted too.
		EXPECT_TRUE( glob("[]x]y", "]y") );      //a ']' first is a literal, not the terminator.
		EXPECT_TRUE( glob("[abc", "[abc") );     //unterminated -> the '[' is a literal.
		EXPECT_TRUE( glob("*[0-9]", "port 8") );
	}

	TEST( FilterTests, TestAndIsBitwise ){
		EXPECT_TRUE( filterValue(DB::EOperator::Equal, 0x6).TestAnd(0x4) );
		EXPECT_FALSE( filterValue(DB::EOperator::Equal, 0x6).TestAnd(0x1) );
		EXPECT_FALSE( filterValue(DB::EOperator::Equal, "notANumber").TestAnd(0x4) );
	}

	//Input::Filter() turns args into per-column predicates: scalars are equality, arrays are In,
	//objects are one predicate per {operator: value} member.
	TEST( FilterTests, FilterFromArgsScalarIsEquality ){
		let f = filterOf( "{id: 42}" );
		EXPECT_FALSE( f.Empty() );
		EXPECT_TRUE( f.Test<uint>("id", 42u) );
		EXPECT_FALSE( f.Test<uint>("id", 43u) );
		EXPECT_TRUE( f.Test<uint>("other", 43u) ); //an unfiltered column always passes.
	}

	TEST( FilterTests, FilterFromArgsArrayIsIn ){
		let f = filterOf( R"({name: ["bob","alice"]})" );
		EXPECT_TRUE( f.Test<string>("name", "bob") );
		EXPECT_FALSE( f.Test<string>("name", "carol") );
	}

	//Multiple members on one column AND together: 1 < id < 10.
	TEST( FilterTests, FilterFromArgsObjectAndsOperators ){
		let f = filterOf( "{id: {gt: 1, lt: 10}}" );
		EXPECT_TRUE( f.Test<uint>("id", 5u) );
		EXPECT_FALSE( f.Test<uint>("id", 1u) );
		EXPECT_FALSE( f.Test<uint>("id", 10u) );
	}

	TEST( FilterTests, EmptyFilter ){
		EXPECT_TRUE( filterOf("{}").Empty() );
		EXPECT_TRUE( filterOf("{}").Test<uint>("id", 42u) );
	}

	TEST( FilterTests, TestF ){
		let f = filterOf( "{id: 42}" );
		bool evaluated{};
		EXPECT_TRUE( f.TestF<uint>("id", [&]{ evaluated = true; return 42u; }) );
		EXPECT_TRUE( evaluated );
		evaluated = false;
		EXPECT_TRUE( f.TestF<uint>("unfiltered", [&]{ evaluated = true; return 42u; }) );
		EXPECT_FALSE( evaluated ); //no filter on the column -> the value is never computed.
	}

	//FilterValue::ToString uses DB::ToString - the SQL spelling - not the "eq"/"gte" QL one.
	TEST( FilterTests, ToString ){
		EXPECT_EQ( filterOf("{id: 42}").ToString("id"), "==42" );
		EXPECT_EQ( filterOf("{id: 42}").ToString("other"), "none" );
	}
}
