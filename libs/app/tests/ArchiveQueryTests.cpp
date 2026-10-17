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
		y.Append( ql, archiveRecords(entries) );
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

	//L3: a forwarded entry's app_pk/app_instance_pk used to be dropped on the way to logs(), so the attribution the pipeline
	//stores so carefully (review 1 #2, review 2 #9) could not be read back - the AppServer's history view was an unattributed
	//merge of every app's lines.
	TEST( ArchiveQueryTests, ForwardedEntriesKeepTheirAttribution ){
		let mine = entry( tp(0), ELogLevel::Information, 1, "mine" );
		let theirs = entry( tp(1), ELogLevel::Information, 2, "theirs" );
		auto records = archiveRecords( {mine, theirs} );
		*records.back().mutable_entry() = LogProto::LogEntryFile( theirs, 42, 84 );

		auto ql = table( "logs" );
		addTable( ql, "entries", {"line","appId","appInstanceId"} );
		ArchiveQuery af;
		af.Append( ql, vector<Log::Proto::FileEntry>{records} );
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
		filtered.Append( byApp, move(records) );
		EXPECT_EQ( lines(filtered.Sort({})), (vector<uint32_t>{2}) ) << "appId did not select the forwarded entry";
	}

	//The AppServer's merged history, read by someone denied one instance's logs:  that instance's forwarded lines drop, while
	//the AppServer's own and another instance's stay - from an archived day and the daily file.
	TEST( ArchiveQueryTests, HiddenInstancesForwardedLinesAreLeftOut ){
		let mine = entry( tp(0), ELogLevel::Information, 1, "mine" );
		let hidden = entry( tp(1), ELogLevel::Information, 2, "hidden" );
		let shown = entry( tp(2), ELogLevel::Information, 3, "shown" );
		auto records = archiveRecords( {mine, hidden, shown} );
		let n = records.size();
		*records[n-2].mutable_entry() = LogProto::LogEntryFile( hidden, 42, 84 );
		*records[n-1].mutable_entry() = LogProto::LogEntryFile( shown, 43, 85 );

		auto ql = table( "logs" );
		ArchiveQuery archived{ flat_set<ProgInstPK>{84} };
		archived.Append( ql, move(records) );
		EXPECT_EQ( lines(archived.Sort({})), (vector<uint32_t>{1,3}) );

		auto stream = fileEntries( {mine, hidden, shown} );
		for( auto& fe : stream ){
			if( fe.has_entry() && fe.entry().line()==2 ){
				fe.mutable_entry()->set_app_pk( 42 );
				fe.mutable_entry()->set_app_instance_pk( 84 );
			}
			else if( fe.has_entry() && fe.entry().line()==3 ){
				fe.mutable_entry()->set_app_pk( 43 );
				fe.mutable_entry()->set_app_instance_pk( 85 );
			}
		}
		ArchiveQuery daily{ ql.Filter(), move(stream), {84} };
		EXPECT_EQ( lines(daily.Sort({})), (vector<uint32_t>{1,3}) );
	}
	//An own line carries no app_pk - its instance pk is not consulted, so hiding the AppServer's own instance hides nothing.
	TEST( ArchiveQueryTests, HiddenInstancesNeverHideOwnLines ){
		auto ql = table( "logs" );
		ArchiveQuery archive{ flat_set<ProgInstPK>{0} };
		archive.Append( ql, archiveRecords({entry(tp(0), ELogLevel::Information, 1, "mine")}) );
		EXPECT_EQ( archive.EntrySize(), 1u );
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
