//Everything Jde.App.Shared does to a log archive once it is in memory:  which entries a QL filter keeps, which strings
//come with them, the sort, the paging and the json the `logs` query returns.  ArchiveQuery is fed the protos ArchiveAwait
//and ProtoLog would have read off disk, so no file and no data source is involved.
#include <gtest/gtest.h>
#include <boost/uuid/uuid_io.hpp>
#include <jde/fwk/chrono.h>
#include <jde/app/log/ArchiveQuery.h>
#include "helpers.h"

#define let const auto

namespace Jde::App::Tests{
	Ω load( const vector<Logging::Entry>& entries, QL::TableQL& ql )ε->ArchiveQuery{
		ArchiveQuery y;
		y.Append( ql, archiveProto(entries) );
		return y;
	}
	Ω load( const vector<Logging::Entry>& entries )ε->ArchiveQuery{
		auto ql = table( "logs" );
		return load( entries, ql );
	}
	Ω lines( const vector<Log::Proto::LogEntryFile>& entries )ι->vector<uint32_t>{
		vector<uint32_t> y;
		for( let& e : entries )
			y.push_back( e.line() );
		return y;
	}

	TEST( ArchiveQueryTests, EntrySizeCountsEveryTimeBucket ){
		let af = load( {entry(tp(0), ELogLevel::Information, 1, "a"), entry(tp(0), ELogLevel::Information, 2, "b"), entry(tp(1), ELogLevel::Information, 3, "c")} );
		EXPECT_EQ( af.Entries.size(), 2u ); //keyed by time - two of the three share a timestamp.
		EXPECT_EQ( af.EntrySize(), 3u );
	}

	TEST( ArchiveQueryTests, NoFilterKeepsEverything ){
		EXPECT_EQ( load({entry(tp(0), ELogLevel::Error, 1, "a"), entry(tp(1), ELogLevel::Debug, 2, "b")}).EntrySize(), 2u );
	}

	TEST( ArchiveQueryTests, FilterSelectsEntries ){
		auto ql = table( "logs", "{level: 4}" ); //Error
		let af = load( {entry(tp(0), ELogLevel::Error, 1, "boom"), entry(tp(1), ELogLevel::Debug, 2, "noise")}, ql );
		ASSERT_EQ( af.EntrySize(), 1u );
		EXPECT_EQ( af.Entries.begin()->second.front().line(), 1u );
	}

	//T10: the filter is `filterBits & entryBits`, non-zero ([`FilterQL.cpp:263-266`]).  Two single-bit tags against a
	//single-bit filter says nothing about that - an equality test passes every one of those cases identically.  What
	//separates them is an entry whose tags are a *superset* of the filter, which bitwise keeps and equality drops.
	Ω tagged( uint32_t line, ELogTags tags )ι->Logging::Entry{ return entry( tp(line), ELogLevel::Information, line, Ƒ("tagged {}", line), {}, "src/x.cpp", "Fn", tags ); }

	TEST( ArchiveQueryTests, TagFilterIsBitwise ){
		auto ql = table( "logs", Ƒ("{{tags: {}}}", (uint)ELogTags::Test) );
		let af = load( {tagged(1, ELogTags::Test|ELogTags::Sql), tagged(2, ELogTags::Test), tagged(3, ELogTags::Sql)}, ql );
		EXPECT_EQ( lines(af.Sort({})), (vector<uint32_t>{1,2}) ) << "an entry is kept when it carries the filter's bit, not when its tags equal it";
	}

	//...and the same test read the other way: a filter of several bits keeps an entry carrying any one of them, which is
	//what a UI tag selection is - `tags: Test|Sql` means either, not both.
	TEST( ArchiveQueryTests, TagFilterMatchesAnyBitOfTheFilter ){
		auto ql = table( "logs", Ƒ("{{tags: {}}}", (uint)(ELogTags::Test|ELogTags::Sql)) );
		let af = load( {tagged(1, ELogTags::Test), tagged(2, ELogTags::Sql), tagged(3, ELogTags::Threads)}, ql );
		EXPECT_EQ( lines(af.Sort({})), (vector<uint32_t>{1,2}) ) << "one bit in common is a match; none is not";
	}

	//The archived-file path loads the whole string table before it filters, so a dropped entry's strings stay behind.
	TEST( ArchiveQueryTests, ArchiveProtoKeepsEveryStringItWasGiven ){
		auto ql = table( "logs", "{level: 4}" );
		let af = load( {entry(tp(0), ELogLevel::Error, 1, "boom"), entry(tp(1), ELogLevel::Debug, 2, "noise")}, ql );
		ASSERT_EQ( af.EntrySize(), 1u );
		EXPECT_EQ( af.Strings[EStringKind::Template].size(), 2u );
	}

	//The daily-file path partitions strings from entries first, and - like the archive path above - loads all of them
	//before filtering.  It used to keep only the strings a surviving entry named, which was not a policy but #5: it
	//filtered first, so Test() resolved "text"/"message"/"args" against maps that were still empty, nothing survived, and
	//so nothing was ever loaded.  Carrying a dropped entry's strings costs nothing - ToJson emits only the ids a surviving
	//entry names.
	TEST( ArchiveQueryTests, FileEntriesKeepEveryStringItWasGiven ){
		let entries = vector<Logging::Entry>{ entry(tp(0), ELogLevel::Error, 1, "boom", {"42"}), entry(tp(1), ELogLevel::Debug, 2, "noise") };
		auto ql = table( "logs", "{level: 4}" );
		let filter = ql.Filter();
		ArchiveQuery af{ filter, fileEntries(entries) };
		ASSERT_EQ( af.EntrySize(), 1u );//the filter still selects entries - only the string table is unconditional.
		ASSERT_EQ( af.Strings[EStringKind::Template].size(), 2u );
		EXPECT_EQ( af.Strings[EStringKind::Template].at(entries[0].Id()), "boom" );
		EXPECT_EQ( af.Strings[EStringKind::Template].at(entries[1].Id()), "noise" );
		ASSERT_EQ( af.Strings[EStringKind::Arg].size(), 1u );//only entry 0 has an arg, so this is unchanged.
		EXPECT_EQ( af.Strings[EStringKind::Arg].at(Logging::Entry::GenerateId("42")), "42" );
	}

	//#5, at the level it broke:  a filter on a string column has to be able to see the strings.  These are the daily-file
	//counterparts of FilterOnMessageFormatsTheTemplate below, which only ever covered the archive path.
	TEST( ArchiveQueryTests, FileEntriesFilterOnText ){
		let entries = vector<Logging::Entry>{ entry(tp(0), ELogLevel::Information, 1, "keep me"), entry(tp(1), ELogLevel::Information, 2, "drop me") };
		auto ql = table( "logs", R"({text: "keep me"})" );
		let filter = ql.Filter();
		ArchiveQuery af{ filter, fileEntries(entries) };
		ASSERT_EQ( af.EntrySize(), 1u );
		EXPECT_EQ( af.Entries.begin()->second.front().line(), 1u );
	}

	TEST( ArchiveQueryTests, FileEntriesFilterOnMessage ){
		let entries = vector<Logging::Entry>{ entry(tp(0), ELogLevel::Information, 1, "zeta"), entry(tp(1), ELogLevel::Information, 2, "alpha {}", {"9"}) };
		auto ql = table( "logs", R"({message: "alpha 9"})" );
		let filter = ql.Filter();
		ArchiveQuery af{ filter, fileEntries(entries) };
		ASSERT_EQ( af.EntrySize(), 1u );
		EXPECT_EQ( af.Entries.begin()->second.front().line(), 2u );//args resolve through Args, which the entry pass fills.
	}

	TEST( ArchiveQueryTests, FileEntriesFilterOnArgs ){
		let entries = vector<Logging::Entry>{ entry(tp(0), ELogLevel::Information, 1, "a {}", {"keep"}), entry(tp(1), ELogLevel::Information, 2, "b {}", {"drop"}) };
		auto ql = table( "logs", R"({args: "keep"})" );
		let filter = ql.Filter();
		ArchiveQuery af{ filter, fileEntries(entries) };
		ASSERT_EQ( af.EntrySize(), 1u );
		EXPECT_EQ( af.Entries.begin()->second.front().line(), 1u );
	}

	//M9: the args filter ANDed across an entry's arguments, so `args:"alpha"` rejected any entry that also carried an
	//unrelated argument - and an entry with no arguments at all passed, because the loop that could have rejected it never
	//ran.  Pre-fix this returned {3}, the exact complement of the right answer.
	TEST( ArchiveQueryTests, ArgsFilterMatchesAnyArgument ){
		let entries = vector<Logging::Entry>{
			entry( tp(0), ELogLevel::Information, 1, "two {} {}", {"alpha","beta"} ),
			entry( tp(1), ELogLevel::Information, 2, "one {}", {"beta"} ),
			entry( tp(2), ELogLevel::Information, 3, "none" ) };
		auto ql = table( "logs", R"({args: "alpha"})" );
		EXPECT_EQ( lines(load(entries, ql).Sort({})), (vector<uint32_t>{1}) ) << "1 has alpha among its args; 2 does not; 3 has no args to match with";
	}

	//An archive an older build wrote keeps forwarded entries in externalEntries;  they are read back as entries.
	TEST( ArchiveQueryTests, OlderExternalEntriesAreAppended ){
		let e = entry( tp(0), ELogLevel::Warning, 7, "external" );
		DayArchive proto;
		*proto.add_externalentries() = asOlderExternal( LogProto::LogEntryFile(e, 3, 4) );
		addStrings( proto, e );

		ArchiveQuery af;
		auto ql = table( "logs" );
		af.Append( ql, move(proto) );
		ASSERT_EQ( af.EntrySize(), 1u );
		EXPECT_EQ( af.Entries.begin()->second.front().line(), 7u );
		EXPECT_EQ( af.Entries.begin()->second.front().app_pk(), 3 );
	}

	//IsComplete is what stops the reader walking further back through the archived days.  It can only answer for a
	//time-ordered page:  any other order needs every entry before the first one can be named.
	TEST( ArchiveQueryTests, IsComplete ){
		let af = load( {entry(tp(0), ELogLevel::Information, 1, "a"), entry(tp(1), ELogLevel::Information, 2, "b"), entry(tp(2), ELogLevel::Information, 3, "c")} );
		EXPECT_FALSE( af.IsComplete(table("logs", R"({orderBy: "time"})")) );              //no limit -> read it all.
		EXPECT_FALSE( af.IsComplete(table("logs", R"({limit: 2})")) );                     //no orderBy.
		EXPECT_FALSE( af.IsComplete(table("logs", R"({limit: 2, orderBy: "level"})")) );
		EXPECT_TRUE( af.IsComplete(table("logs", R"({limit: 2, orderBy: "time"})")) );
		EXPECT_TRUE( af.IsComplete(table("logs", R"({limit: 3, orderBy: "time"})")) );
		EXPECT_FALSE( af.IsComplete(table("logs", R"({limit: 3, offset: 1, orderBy: "time"})")) ); //offset counts against the limit.
	}

	//T10: descending time was never asserted, and it is the order the page actually asks for (web log-entry.ts) - the one
	//IsComplete has to answer for, since LoadArchives' *reverse* walk is the branch every non-ascending order takes and
	//the early stop is the only thing that keeps a limited page off the whole history.  Direction is not read at all: the
	//check is on the first key's name, which is right for both walks - a day's entries never outrank another day's on
	//time, so once a page is full nothing further back can displace it either way round.
	TEST( ArchiveQueryTests, IsCompleteForDescendingTime ){
		let af = load( {entry(tp(0), ELogLevel::Information, 1, "a"), entry(tp(1), ELogLevel::Information, 2, "b"), entry(tp(2), ELogLevel::Information, 3, "c")} );
		EXPECT_TRUE( af.IsComplete(table("logs", R"({limit: 2, orderBy: {time: "desc"}})")) );
		EXPECT_TRUE( af.IsComplete(table("logs", R"({limit: 2, orderBy: {time: "asc"}})")) ) << "the explicit spelling of the bare string above";
		EXPECT_FALSE( af.IsComplete(table("logs", R"({orderBy: {time: "desc"}})")) ) << "no limit - read it all, whichever way round";
		EXPECT_FALSE( af.IsComplete(table("logs", R"({limit: 4, orderBy: {time: "desc"}})")) ) << "the page is not full yet";
		EXPECT_FALSE( af.IsComplete(table("logs", R"({limit: 2, orderBy: {level: "desc"}})")) ) << "a non-time first key needs every entry before the first can be named";
		//A secondary key only breaks ties within one timestamp, and two days cannot share one, so time-first still stops.
		EXPECT_TRUE( af.IsComplete(table("logs", R"({limit: 2, orderBy: [{time: "desc"}, {level: "asc"}]})")) );
	}

	TEST( ArchiveQueryTests, SortDefaultsToTimeAscending ){
		let af = load( {entry(tp(2), ELogLevel::Information, 3, "c"), entry(tp(0), ELogLevel::Information, 1, "a"), entry(tp(1), ELogLevel::Information, 2, "b")} );
		EXPECT_EQ( lines(af.Sort({})), (vector<uint32_t>{1,2,3}) );
	}

	TEST( ArchiveQueryTests, SortByLevelDescending ){
		let af = load( {entry(tp(0), ELogLevel::Debug, 1, "a"), entry(tp(1), ELogLevel::Critical, 2, "b"), entry(tp(2), ELogLevel::Information, 3, "c")} );
		auto ql = table( "logs", R"({orderBy: {level: "desc"}})" );
		EXPECT_EQ( lines(af.Sort(ql.OrderByJson())), (vector<uint32_t>{2,3,1}) );
	}

	TEST( ArchiveQueryTests, SortByFileAndFunctionUsesTheName ){
		let entries = vector<Logging::Entry>{
			entry( tp(0), ELogLevel::Information, 1, "a", {}, "d.cpp", "Dd" ),
			entry( tp(1), ELogLevel::Information, 2, "b", {}, "b.cpp", "Bb" ),
			entry( tp(2), ELogLevel::Information, 3, "c", {}, "a.cpp", "Aa" ),
			entry( tp(3), ELogLevel::Information, 4, "d", {}, "c.cpp", "Cc" ) };
		let af = load( entries );

		auto byFile = table( "logs", R"({orderBy: "file"})" );
		EXPECT_EQ( lines(af.Sort(byFile.OrderByJson())), (vector<uint32_t>{3,2,4,1}) ) << "a.cpp, b.cpp, c.cpp, d.cpp";

		auto byFunction = table( "logs", R"({orderBy: {function: "desc"}})" );
		EXPECT_EQ( lines(af.Sort(byFunction.OrderByJson())), (vector<uint32_t>{1,4,2,3}) ) << "Dd, Cc, Bb, Aa";
	}

	//M3: the fast path returned early whenever the *first* key was time-ascending, discarding every key after it.  Skipping
	//the sort is right when time-ascending is the whole ordering - Entries is a std::map keyed on time, so the flatten is
	//already in that order - but only then.  Both entries share a timestamp, so they land in one bucket in insertion order
	//and the secondary key is the only thing that can separate them.
	TEST( ArchiveQueryTests, SecondaryOrderByKeyIsApplied ){
		let af = load( {entry(tp(0), ELogLevel::Information, 1, "a", {}, "b.cpp"), entry(tp(0), ELogLevel::Information, 2, "b", {}, "a.cpp")} );
		auto ql = table( "logs", R"({orderBy: ["time", {file: "asc"}]})" );
		EXPECT_EQ( lines(af.Sort(ql.OrderByJson())), (vector<uint32_t>{2,1}) ) << "a.cpp before b.cpp within the same timestamp";
	}

	//The sort is stable, so entries tied on every requested key keep the source's time order rather than an arbitrary one -
	//which is what the fast path above does too, so the two agree.
	//Two interleaved levels rather than one, and 64 of them: libc++ insertion-sorts short ranges and short-circuits a range
	//whose elements all compare equivalent, so neither a handful of entries nor a single level would tell the two sorts
	//apart.  Real partitioning across two groups is what moves tied elements past each other.
	TEST( ArchiveQueryTests, TiedEntriesKeepTimeOrder ){
		vector<Logging::Entry> entries;
		vector<uint32_t> information, critical;
		for( uint32_t i=0; i<64; ++i ){
			let isInformation = i%2==0;
			entries.push_back( entry(tp(i), isInformation ? ELogLevel::Information : ELogLevel::Critical, i+1, "same") );
			(isInformation ? information : critical).push_back( i+1 );
		}
		auto expected = information;//Information sorts below Critical; within each, time order has to survive.
		expected.insert( expected.end(), critical.begin(), critical.end() );

		let af = load( entries );
		auto ql = table( "logs", R"({orderBy: {level: "asc"}})" );
		EXPECT_EQ( lines(af.Sort(ql.OrderByJson())), expected );
	}

	//"message" is the formatted text, not the template:  sorting by it has to expand the args first.
	TEST( ArchiveQueryTests, SortByMessageFormatsTheTemplate ){
		let af = load( {entry(tp(0), ELogLevel::Information, 1, "zeta"), entry(tp(1), ELogLevel::Information, 2, "alpha {}", {"9"})} );
		auto ql = table( "logs", R"({orderBy: "message"})" );
		EXPECT_EQ( lines(af.Sort(ql.OrderByJson())), (vector<uint32_t>{2,1}) ); //"alpha 9" < "zeta".
	}

	TEST( ArchiveQueryTests, FilterOnMessageFormatsTheTemplate ){
		auto ql = table( "logs", R"({message: "alpha 9"})" );
		let af = load( {entry(tp(0), ELogLevel::Information, 1, "zeta"), entry(tp(1), ELogLevel::Information, 2, "alpha {}", {"9"})}, ql );
		ASSERT_EQ( af.EntrySize(), 1u );
		EXPECT_EQ( af.Entries.begin()->second.front().line(), 2u );
	}

	//#6: the time filter compared the *rendered* ISO strings, and ToIsoString is variable width - a whole second drops the
	//fraction.  So "…T22:13:25.250000Z" is lexicographically below "…T22:13:25Z" ('.'=0x2E < 'Z'=0x5A at index 19) and each
	//comparison came out backwards for entries sharing a second with the bound.  The coarse day window in LogQLAwait uses
	//real TimePoints, so the right archive was loaded and the entries were then silently discarded one at a time.
	TEST( ArchiveQueryTests, TimeFilterIsChronologicalNotLexicographic ){
		let boundary = tp( 5 );                                     //whole second -> renders with no fraction.
		let sub = boundary+std::chrono::milliseconds{ 250 };        //renders "…:25.250000Z", which sorts *before* "…:25Z".
		let entries = vector<Logging::Entry>{ entry(boundary, ELogLevel::Information, 1, "boundary"), entry(sub, ELogLevel::Information, 2, "sub") };
		let filtered = [&entries]( sv op, TimePoint bound )ε->vector<uint32_t>{
			auto ql = table( "logs", Ƒ(R"({{time: {{{}: "{}"}}}})", op, ToIsoString(bound)) );
			return lines( load(entries, ql).Sort({}) );
		};
		EXPECT_EQ( filtered("gt", boundary), (vector<uint32_t>{2}) ) << "250ms after the boundary is after it";
		EXPECT_EQ( filtered("lt", boundary), (vector<uint32_t>{}) ) << "nothing precedes the boundary";
		//the symmetric case: a whole-second entry is lexicographically *above* a sub-second bound in the same second.
		EXPECT_EQ( filtered("lt", boundary+std::chrono::milliseconds{100}), (vector<uint32_t>{1}) );
		EXPECT_EQ( filtered("gte", boundary), (vector<uint32_t>{1,2}) );
		EXPECT_EQ( filtered("lte", sub), (vector<uint32_t>{1,2}) );
		EXPECT_EQ( filtered("eq", sub), (vector<uint32_t>{2}) ) << "a sub-second literal must match its own entry";
	}

	TEST( ArchiveQueryTests, SubTableFiltersFileNameAndUser ){
		let entries = vector<Logging::Entry>{
			entry( tp(0), ELogLevel::Information, 1, "a", {}, "keep.cpp", "Fn", ELogTags::Test, UserPK{7} ),
			entry( tp(1), ELogLevel::Information, 2, "b", {}, "drop.cpp", "Fn", ELogTags::Test, UserPK{7} ),
			entry( tp(2), ELogLevel::Information, 3, "c", {}, "keep.cpp", "Fn", ELogTags::Test, UserPK{8} ) };

		auto byFile = table( "logs" );
		addTable( byFile, "file", {"name"}, R"({name: "keep.cpp"})" );
		EXPECT_EQ( lines(load(entries, byFile).Sort({})), (vector<uint32_t>{1,3}) );

		auto byUser = table( "logs" );
		addTable( byUser, "user", {"id"}, "{id: 8}" );
		EXPECT_EQ( lines(load(entries, byUser).Sort({})), (vector<uint32_t>{3}) );
	}

	//L3: ToEntry folds an external entry into LogEntryFile for everything downstream and used to drop app_pk/app_instance_pk there,
	//so the attribution the pipeline stores so carefully (review 1 #2, review 2 #9) could not be read back through logs() - the
	//AppServer's history view was an unattributed merge of every app's lines.
	TEST( ArchiveQueryTests, ForwardedEntriesKeepTheirAttribution ){
		let mine = entry( tp(0), ELogLevel::Information, 1, "mine" );
		let theirs = entry( tp(1), ELogLevel::Information, 2, "theirs" );
		DayArchive proto;
		*proto.add_entries() = LogProto::LogEntryFile( mine );
		*proto.add_entries() = LogProto::LogEntryFile( theirs, 42, 84 );
		addStrings( proto, mine );
		addStrings( proto, theirs );

		auto ql = table( "logs" );
		addTable( ql, "entries", {"line","appId","appInstanceId"} );
		ArchiveQuery af;
		af.Append( ql, DayArchive{proto} );
		let jentries = af.ToJson( ql ).at( "entries" ).as_array();
		ASSERT_EQ( jentries.size(), 2u );
		for( let& je : jentries ){
			let& o = je.as_object();
			let external = o.at("line").to_number<uint32>()==2u;
			EXPECT_EQ( o.at("appId").to_number<uint32>(), external ? 42u : 0u ) << "line " << o.at("line");
			EXPECT_EQ( o.at("appInstanceId").to_number<uint32>(), external ? 84u : 0u ) << "line " << o.at("line");
		}

		//...and filterable, which is what makes one app's lines findable in that merge.
		auto byApp = table( "logs", "{appId: 42}" );
		addTable( byApp, "entries", {"line"} );
		ArchiveQuery filtered;
		filtered.Append( byApp, move(proto) );
		EXPECT_EQ( lines(filtered.Sort({})), (vector<uint32_t>{2}) ) << "appId did not select the forwarded entry";
	}

	TEST( ArchiveQueryTests, ToJsonEntryColumns ){
		let e = entry( tp(0), ELogLevel::Error, 42, "boom {}", {"9"}, "src/x.cpp", "Fn", ELogTags::Test, UserPK{7} );
		let af = load( {e} );

		auto ql = table( "logs" );
		addTable( ql, "entries", {"templateId","argIds","level","tags","line","time","userId","fileId","functionId"} );
		let o = af.ToJson( ql );
		let& jentries = o.at( "entries" ).as_array();
		ASSERT_EQ( jentries.size(), 1u );
		let& j = jentries[0].as_object();
		EXPECT_EQ( j.at("templateId").as_string(), to_string(e.Id()) );
		EXPECT_EQ( j.at("fileId").as_string(), to_string(e.FileId()) );
		EXPECT_EQ( j.at("functionId").as_string(), to_string(e.FunctionId()) );
		EXPECT_EQ( j.at("level").as_string(), "Error" );
		EXPECT_EQ( j.at("line").to_number<uint32_t>(), 42u );
		EXPECT_EQ( j.at("userId").to_number<uint32_t>(), 7u );
		EXPECT_EQ( j.at("time").as_string(), ToIsoString(tp(0)) );
		let& tags = j.at( "tags" ).as_array();
		ASSERT_EQ( tags.size(), 1u );
		EXPECT_EQ( tags[0].as_string(), "test" );
		let& args = j.at( "argIds" ).as_array();
		ASSERT_EQ( args.size(), 1u );
		EXPECT_EQ( args[0].as_string(), to_string(Logging::Entry::GenerateId("9")) );
	}

	//Only the columns the query asked for are emitted - the web client trims on what it requested.
	TEST( ArchiveQueryTests, ToJsonEmitsOnlyRequestedColumns ){
		let af = load( {entry(tp(0), ELogLevel::Error, 42, "boom")} );
		auto ql = table( "logs" );
		addTable( ql, "entries", {"line"} );
		let o = af.ToJson( ql );
		let& j = o.at( "entries" ).as_array()[0].as_object();
		EXPECT_EQ( j.size(), 1u );
		EXPECT_TRUE( j.contains("line") );
	}

	//No `entries` sub-table -> no entries key at all, rather than an empty array.
	TEST( ArchiveQueryTests, ToJsonWithoutEntriesTable ){
		let af = load( {entry(tp(0), ELogLevel::Error, 42, "boom")} );
		auto ql = table( "logs" );
		EXPECT_FALSE( af.ToJson(ql).contains("entries") );
	}

	TEST( ArchiveQueryTests, ToJsonPages ){
		let af = load( {entry(tp(0), ELogLevel::Information, 1, "a"), entry(tp(1), ELogLevel::Information, 2, "b"), entry(tp(2), ELogLevel::Information, 3, "c")} );
		auto ql = table( "logs", "{limit: 1, offset: 1}" );
		addTable( ql, "entries", {"line"} );
		let o = af.ToJson( ql );
		let& jentries = o.at( "entries" ).as_array();
		ASSERT_EQ( jentries.size(), 1u );
		EXPECT_EQ( jentries[0].as_object().at("line").to_number<uint32_t>(), 2u );
	}

	//`strings` is the id->text side table the client joins the ids against.  It is filled by the id columns that were
	//asked for, so a query with no id column gets an empty one.
	TEST( ArchiveQueryTests, ToJsonStringsTable ){
		let e = entry( tp(0), ELogLevel::Error, 42, "boom {}", {"9"}, "src/x.cpp", "Fn" );
		let af = load( {e} );

		auto ql = table( "logs" );
		addTable( ql, "entries", {"templateId","argIds","fileId","functionId"} );
		addTable( ql, "strings", {"id","value"} );
		let o = af.ToJson( ql );
		flat_map<string,string> strings;
		for( let& s : o.at("strings").as_array() )
			strings.emplace( string{s.as_object().at("id").as_string()}, string{s.as_object().at("value").as_string()} );

		ASSERT_EQ( strings.size(), 4u );
		EXPECT_EQ( strings.at(to_string(e.Id())), "boom {}" );          //the template, not the formatted message.
		EXPECT_EQ( strings.at(to_string(e.FileId())), "src/x.cpp" );
		EXPECT_EQ( strings.at(to_string(e.FunctionId())), "Fn" );
		EXPECT_EQ( strings.at(to_string(Logging::Entry::GenerateId("9"))), "9" );
	}

	TEST( ArchiveQueryTests, ToJsonSubTables ){
		let e = entry( tp(0), ELogLevel::Error, 42, "boom", {}, "src/x.cpp", "Fn", ELogTags::Test, UserPK{7} );
		let af = load( {e} );

		auto ql = table( "logs" );
		addTable( ql, "entries", {"line"} );
		addTable( ql, "file", {"id","name"} );
		addTable( ql, "function", {"name"} );
		addTable( ql, "user", {"id"} );
		let o = af.ToJson( ql );
		let& j = o.at( "entries" ).as_array()[0].as_object();
		EXPECT_EQ( j.at("file").as_object().at("name").as_string(), "src/x.cpp" );
		EXPECT_EQ( j.at("file").as_object().at("id").as_string(), to_string(e.FileId()) );
		EXPECT_EQ( j.at("function").as_object().at("name").as_string(), "Fn" );
		EXPECT_FALSE( j.at("function").as_object().contains("id") ); //not asked for.
		EXPECT_EQ( j.at("user").as_object().at("id").to_number<uint32_t>(), 7u );
	}
}
