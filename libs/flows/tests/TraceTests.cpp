//The trace (docs/flows/spec.md *Trace*):  the hooks folded to one record per node per message, what each level keeps, and
//the minute figures the engine gives whoever asks;  and its day files (*History*, FileTrace.h):  the records of each level
//written through a TraceStore over a scratch directory and read back, their cut, the minute record, the roll at midnight,
//retention and the size cap, and the reads' paging and lineage.
#include <future>
#include <thread>
#include <unistd.h>
#include <jde/flows/FileTrace.h>
#include "helpers.h"
#include "../../fwk/tests/testsDir.h"

namespace Jde::Opc::Flows::Tests{
	struct TraceTests : FlowTest{
		//An inject through a change, which marks the message, into a switch:  "bad" to a write that refuses it, the rest to a log.
		α Route( ETraceLevel level )->void{
			Run( Nodes({
				Node( "in", "inject", "{}", {{"mark"}} ),
				Node( "mark", "change", R"({"rules":[{"t":"set","p":"seen","to":true}]})", {{"sw"}} ),
				Node( "sw", "switch", R"({"rules":[{"t":"eq","v":"bad"},{"t":"else"}]})", {{"w"}, {"out"}} ),
				Node( "w", "opc out" ), Node( "out", "log" )
			}), level );
		}
	};

	TEST_F( TraceTests, AHopIsOneRecordPerNodePerMessage ){
		Route( ETraceLevel::Hops );
		Advance( 30s );
		Inject( 7, "rpm" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop mark [0]*", "hop sw [1]", "hop out []"}) );
		let records = Trace->Records();
		let id = Host->Took->Of( "out" )[0].at( "_id" ).to_number<MessageId>();
		for( let& record : records ){
			EXPECT_EQ( record.Id, id ) << record.Node;
			EXPECT_EQ( record.Parent, 0u );
			EXPECT_EQ( record.Time, At(6, 0, 30) );
		}
		EXPECT_EQ( parse(*records[0].Json).at("payload"), 7 ) << "a birth carries the message";
		EXPECT_EQ( parse(*records[1].Json).at("seen"), true ) << "a hop carries it where the node changed it";
		EXPECT_FALSE( records[2].Json ) << "and not where it passed as it came";
		EXPECT_EQ( records[2].Ports, vector<uint>{1} );
	}

	TEST_F( TraceTests, HopsKeepsEverything ){
		Route( ETraceLevel::Hops );
		Inject( "bad" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop mark [0]*", "hop sw [0]", "hop w []", "error w", "status w"}) );
		let records = Trace->Records();
		EXPECT_EQ( records[4].Error, "Bad_NotWritable" );
		EXPECT_EQ( records[4].Id, records[0].Id );
		EXPECT_EQ( *records[5].Status, (NodeStatus{"red", "ring", "1 uncaught, the last:  Bad_NotWritable"}) );
	}

	TEST_F( TraceTests, ErrorsKeepsThePathOfAMessageThatFails ){
		Route( ETraceLevel::Errors );
		Inject( 7 );
		Inject( 8 );
		EXPECT_EQ( Trace->Lines(), vector<string>{} ) << "a quiet flow writes nothing";
		Inject( "bad" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop mark [0]*", "hop sw [0]", "hop w []", "error w", "status w"}) ) << "its whole path, at its end";
		Inject( 9 );
		EXPECT_EQ( Trace->Lines().size(), 6u );
	}

	TEST_F( TraceTests, ErrorsKeepsWhatAFailedCloneWasMadeFrom ){
		Run( Nodes({Node("in", "inject", "{}", {{"out", "w"}}), Node("w", "opc out"), Node("out", "log")}), ETraceLevel::Errors );
		Inject( "bad" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop out []", "clone in [0]", "hop w []", "error w", "status w"}) );
		let records = Trace->Records();
		EXPECT_EQ( records[2].Parent, records[0].Id );
		EXPECT_EQ( records[4].Id, records[2].Id );
		EXPECT_EQ( records[4].Parent, records[0].Id );
	}

	//#451 review #8:  a clone that fails while the node that sent it still has its parent writes its own record with the error.
	TEST_F( TraceTests, ErrorsKeepsACloneThatFailsBeforeItsParentIsDone ){
		Run( Nodes({
			Node( "in", "inject", "{}", {{"f"}} ),
			Node( "f", "function", R"({"code":"node.send( msg, false );\nawait new Promise( wake => setTimeout( wake, 1000 ) );\nreturn null;"})", {{"out", "w"}} ),
			Node( "w", "opc out" ), Node( "out", "log" )
		}), ETraceLevel::Errors );
		Inject( "bad" );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth in [0]*", "hop out []", "clone f [0]", "hop w []", "error w", "status w"}) ) << "f still holds the message it cloned";
		let records = Trace->Records();
		ASSERT_EQ( records.size(), 6u );
		EXPECT_EQ( records[2].Parent, records[0].Id );
		EXPECT_EQ( records[3].Id, records[2].Id );
		Advance( 1s );
		EXPECT_EQ( Trace->Lines().size(), 6u ) << "f's own hop, once done, is a quiet message's";
	}

	TEST_F( TraceTests, ErrorsKeepsDrops ){
		Run( Nodes({Node("in", "inject", "{}", {{"parts"}}), Node("parts", "split", "{}", {{"out"}}), Node("out", "log")}), ETraceLevel::Errors, 2 );
		Inject( jarray{1, 2, 3} );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"drop out", "status out"}) );
	}

	TEST_F( TraceTests, CountsAndOffWriteNothing ){
		for( let level : {ETraceLevel::Counts, ETraceLevel::Off} ){
			Route( level );
			Inject( 7 );
			Inject( "bad" );
			EXPECT_EQ( Trace->Lines(), vector<string>{} ) << ToString( level );
			EXPECT_EQ( Counts("w").Errors, 1u ) << "the figures are the engine's, whatever the level";
			Stop();
			Host = ms<TestHost>();
		}
		EXPECT_EQ( ToTraceLevel("errors"), ETraceLevel::Errors );
		EXPECT_EQ( ToString(ETraceLevel::Hops), "hops" );
		EXPECT_FALSE( ToTraceLevel("all") );
	}

	TEST_F( TraceTests, TheEngineKeepsTheMinuteFigures ){
		Route( ETraceLevel::Off );
		Advance( 30s );
		Inject( 7 );
		Inject( 8 );
		Inject( "bad" );
		EXPECT_TRUE( Deployed->TakeMinutes().empty() ) << "the minute has not ended";
		Advance( 40s );
		Inject( 9 );
		Advance( 10s );
		auto minutes = Deployed->TakeMinutes();
		ASSERT_EQ( minutes.size(), 1u ) << "06:01 is still open";
		EXPECT_EQ( minutes[0].Minute, At(6) );
		auto figures = []( const MinuteCounts& minute ){
			string y;
			for( let& node : minute.Nodes ){
				string sent;
				for( let count : node.Sent )
					sent += Ƒ( "{}{}", sent.empty() ? "" : ",", count );
				y += Ƒ( "{}{} {} [{}] {} {}", y.empty() ? "" : " | ", node.Node, node.Received, sent, node.Drops, node.Errors );
			}
			return y;
		};
		EXPECT_EQ( figures(minutes[0]), "in 0 [3] 0 0 | mark 3 [3] 0 0 | sw 3 [1,2] 0 0 | w 1 [] 0 1 | out 2 [] 0 0" ) << "node received [sent per port] drops errors";
		EXPECT_TRUE( Deployed->TakeMinutes().empty() ) << "a minute is taken once";
		Advance( 3min );
		minutes = Deployed->TakeMinutes();
		ASSERT_EQ( minutes.size(), 1u ) << "a minute with no figure is not one";
		EXPECT_EQ( minutes[0].Minute, At(6, 1) );
		EXPECT_EQ( figures(minutes[0]), "in 0 [1] 0 0 | mark 1 [1] 0 0 | sw 1 [0,1] 0 0 | out 1 [] 0 0" );
		EXPECT_EQ( Counts("sw").Sent, (vector<uint>{1, 3}) ) << "and the totals since the start";
		EXPECT_EQ( Counts("in").Sent, vector<uint>{4} );
		EXPECT_EQ( Trace->Minutes().size(), 0u ) << "`off` gives the trace no minute";
	}

	TEST_F( TraceTests, TheTraceHasEachMinuteAsItEnds ){
		Route( ETraceLevel::Counts );
		Advance( 30s );
		Inject( 7 );
		Inject( "bad" );
		EXPECT_TRUE( Trace->Minutes().empty() );
		Advance( 29s );
		EXPECT_TRUE( Trace->Minutes().empty() ) << "06:00:59";
		Advance( 1s );
		ASSERT_EQ( Trace->Minutes().size(), 1u ) << "the minute's end, on the flow's clock, not the next figure";
		EXPECT_EQ( Trace->Minutes()[0].Minute, At(6) );
		EXPECT_EQ( Trace->Minutes()[0].Nodes.size(), 5u );
		Advance( 5min );
		EXPECT_EQ( Trace->Minutes().size(), 1u ) << "a minute with no figure is none";
		Inject( 8 );
		Stop();
		ASSERT_EQ( Trace->Minutes().size(), 2u ) << "the stop ends the minute under way";
		EXPECT_EQ( Trace->Minutes()[1].Minute, At(6, 6) );
		EXPECT_EQ( Deployed->TakeMinutes().size(), 2u ) << "the same minutes TakeMinutes answers";
	}

	//The day files:  a store over a scratch directory, on the suite's clock, each case's flows traced into it.
	struct FileTraceTests : TraceTests{
		α SetUp()->void override{
			Dir = Jde::Tests::TestsDir( "flowTrace", true );
			fs::remove_all( Dir );
			Open( {} );
		}
		α TearDown()->void override{
			TraceTests::TearDown();
			if( Store )
				Store->Stop();
			Store.reset();
			fs::remove_all( Dir );
		}
		//The store, over Dir:  `edit` changes its settings first.  One that was open is stopped, its buffers written.
		α Open( function<void( TraceSettings& )> edit )->void{
			if( Store )
				Store->Stop();
			TraceSettings settings{ .Root=Dir, .Delay=1min, .Zone=&UtcZone() };
			if( edit )
				edit( settings );
			Store = ms<TraceStore>( settings, Time );
		}
		Ω UtcZone()->const std::chrono::time_zone&{ return *std::chrono::locate_zone( "UTC" ); }
		//Route, traced into the store under `slug`.
		α RunTraced( ETraceLevel level, sv slug="flow", uint queue=1000 )->void{
			Tracer = Store->Trace( slug, level, Time );
			Run( Nodes({
				Node( "in", "inject", "{}", {{"mark"}} ),
				Node( "mark", "change", R"({"rules":[{"t":"set","p":"seen","to":true}]})", {{"sw"}} ),
				Node( "sw", "switch", R"({"rules":[{"t":"eq","v":"bad"},{"t":"else"}]})", {{"w"}, {"out"}} ),
				Node( "w", "opc out" ), Node( "out", "log" )
			}), level, queue );
		}
		α Flush( sv slug="flow" )->void{
			std::promise<void> done;
			auto wait = done.get_future();
			Store->Flush( slug, [&]{ done.set_value(); } );
			ASSERT_EQ( wait.wait_for(10s), std::future_status::ready ) << "the flush did not finish";
		}
		α Read( sv slug="flow", TraceQuery query={} )->vector<TraceRecord>{
			if( !query.Start && !query.End )
				query.Start = At( 0 );
			vector<TraceRecord> y;
			for( ;; ){
				auto page = Store->Read( slug, query );
				y.insert( y.end(), std::make_move_iterator(page.Records.begin()), std::make_move_iterator(page.Records.end()) );
				if( !page.Next || page.Records.empty() )
					return y;
				query.From = page.Next;
			}
		}
		//A birth record of `flow` at the clock's time, written straight through the flow's trace, with no flow run.
		α Birth( sv slug, MessageId id, uint payload=1 )->void{
			auto trace = Store->Trace( slug, ETraceLevel::Hops, Time );
			const NodeDef in{ .Id="in", .Type=ENodeType::Inject };
			Message m{ jobject{{"_id", id}, {"payload", payload}} };
			trace->Birth( in, m );
			trace->Complete( in, id );
			Flush( slug );
		}
		α Size( sv slug, Day day )->uint{
			std::error_code ec;
			let size = fs::file_size( Dir/slug/IO::Records::DayDirectory(day)/TraceFileName, ec );
			return ec ? 0 : size;
		}
		Ω Ids( const vector<TraceRecord>& records )->vector<MessageId>{
			vector<MessageId> y;
			for( let& r : records )
				y.push_back( r.Id );
			return y;
		}

		fs::path Dir;
		sp<TraceStore> Store;
	};
	using namespace std::chrono;
	constexpr Day March9{ 2026y/March/9 }, March10{ 2026y/March/10 }, March11{ 2026y/March/11 };

	TEST_F( FileTraceTests, HopsAreWrittenAndReadBackInTimeOrder ){
		RunTraced( ETraceLevel::Hops );
		Advance( 30s );
		Inject( 7, "rpm" );
		Inject( "bad" );
		EXPECT_TRUE( Read().empty() ) << "nothing is written before the flush";
		Flush();
		let records = Read();
		EXPECT_EQ( Lines(records), (vector<string>{"birth in [0]*", "hop mark [0]*", "hop sw [1]", "hop out []", "birth in [0]*", "hop mark [0]*", "hop sw [0]", "hop w []", "error w", "status w"}) );
		ASSERT_EQ( records.size(), 10u );
		EXPECT_EQ( records[0].Time, At(6, 0, 30) );
		EXPECT_EQ( records[1].Id, records[0].Id );
		EXPECT_EQ( parse(*records[1].Json).at("seen"), true );
		EXPECT_FALSE( records[1].Cut );
		EXPECT_EQ( records[2].Ports, vector<uint>{1} );
		EXPECT_EQ( records[8].Error, "Bad_NotWritable" );
		EXPECT_EQ( records[8].Id, records[4].Id );
		EXPECT_EQ( *records[9].Status, (NodeStatus{"red", "ring", "1 uncaught, the last:  Bad_NotWritable"}) );
		EXPECT_EQ( Store->Days("flow"), vector<Day>{March9} );

		Inject( 8 );
		Flush();
		EXPECT_EQ( Read().size(), 14u ) << "a second flush is a second run of the day's file";
		Open( {} );//another process's store over the same files:  the file is scanned, and read as it was written.
		EXPECT_EQ( Lines(Read()).size(), 14u );
		EXPECT_EQ( Read("flow", {.Node="sw"}).size(), 3u ) << "narrowed to a node";
		EXPECT_EQ( Read("flow", {.Kind=ERecordKind::Error}).size(), 1u ) << "and to a kind";
		EXPECT_EQ( Read("flow", {.Msg=records[4].Id}).size(), 5u ) << "and to a message";
		EXPECT_EQ( Read("flow", {.Start=At(6, 0, 30), .End=At(6, 0, 30)}).size(), 14u ) << "the bounds are inclusive";
		EXPECT_EQ( Read("flow", {.Start=At(6, 0, 31)}).size(), 0u );
	}

	TEST_F( FileTraceTests, AJsonPastValueLimitIsCutOnACharacter ){
		//A payload of two-byte characters:  of two limits a byte apart, one falls inside a character, and that cut keeps a byte less.
		string accents;
		for( uint i=0; i<100; ++i )
			accents += "\xC3\xA9";
		uint kept{};
		for( let limit : {120u, 121u} ){
			Open( [limit]( TraceSettings& s ){ s.ValueLimit = limit; } );
			RunTraced( ETraceLevel::Hops, Ƒ("cut{}", limit) );
			Inject( jvalue{accents} );
			Flush( Ƒ("cut{}", limit) );
			let records = Read( Ƒ("cut{}", limit) );
			ASSERT_FALSE( records.empty() );
			EXPECT_TRUE( records[0].Cut );
			let& json = *records[0].Json;
			EXPECT_LE( json.size(), limit );
			EXPECT_TRUE( json.starts_with("{\"_id\":") );
			let text = json.substr( json.find("\"payload\":\"")+11 );
			EXPECT_EQ( text.size()%2, 0u ) << "whole characters:  " << text;
			for( uint i=0; i+1<text.size(); i+=2 )
				EXPECT_EQ( text.substr(i, 2), "\xC3\xA9" );
			EXPECT_FALSE( records[2].Cut ) << "a hop with no json is not cut";
			kept += json.size();
			Stop();
			Host = ms<TestHost>();
		}
		EXPECT_EQ( kept, 120u+121u-1u );
	}

	TEST_F( FileTraceTests, ErrorsAndCountsWriteWhatTheirLevelKeeps ){
		RunTraced( ETraceLevel::Errors );
		Inject( 7 );
		Flush();
		EXPECT_TRUE( Read().empty() ) << "a quiet flow writes no hop";
		Inject( "bad" );
		Flush();
		EXPECT_EQ( Lines(Read()), (vector<string>{"birth in [0]*", "hop mark [0]*", "hop sw [0]", "hop w []", "error w", "status w"}) );
		Stop();
		Host = ms<TestHost>();
		Open( {} );
		RunTraced( ETraceLevel::Counts, "counted" );
		Advance( 30s );
		Inject( 7 );
		Inject( "bad" );
		Advance( 30s );//06:01:  the minute ends.
		Flush( "counted" );
		EXPECT_TRUE( Read("counted").empty() ) << "`counts` writes no hop";
		auto rows = Store->Counts( "counted", At(6), At(7), 1min ).Rows;
		ASSERT_EQ( rows.size(), 5u ) << "one row per node of the minute";
		string figures;
		for( let& row : rows ){
			string sent;
			for( let count : row.Counts.Sent )
				sent += Ƒ( "{}{}", sent.empty() ? "" : ",", count );
			figures += Ƒ( "{}{} {} [{}] {} {}", figures.empty() ? "" : " | ", row.Counts.Node, row.Counts.Received, sent, row.Counts.Drops, row.Counts.Errors );
			EXPECT_EQ( row.Time, At(6) );
		}
		EXPECT_EQ( figures, "in 0 [2] 0 0 | mark 2 [2] 0 0 | sw 2 [1,1] 0 0 | w 1 [] 0 1 | out 1 [] 0 0" );
		Stop();
		Host = ms<TestHost>();
		RunTraced( ETraceLevel::Off, "off" );
		Inject( 7 );
		Advance( 2min );
		Flush( "off" );
		EXPECT_TRUE( Store->Days("off").empty() ) << "`off` writes nothing, the minute included";
	}

	TEST_F( FileTraceTests, CountsAreSummedIntoBuckets ){
		RunTraced( ETraceLevel::Hops );
		for( uint minute=0; minute<6; ++minute ){
			Inject( minute );
			Advance( 1min );
		}
		Stop();
		Flush();
		auto rows = Store->Counts( "flow", At(6), At(6, 6), 5min ).Rows;
		vector<string> y;
		for( let& row : rows )
			y.push_back( Ƒ("{} {} {}", ToIsoString(row.Time).substr(11, 5), row.Counts.Node, row.Counts.Received) );
		EXPECT_EQ( y, (vector<string>{"06:00 in 0", "06:00 mark 5", "06:00 sw 5", "06:00 out 5", "06:05 in 0", "06:05 mark 1", "06:05 sw 1", "06:05 out 1"}) ) << "by bucket, then by the node's first appearance";
		EXPECT_EQ( Store->Counts("flow", At(6, 2), At(6, 4), 1min).Rows.size(), 8u ) << "from the start, to before the end";
	}

	TEST_F( FileTraceTests, AReadPagesWithItsContinuation ){
		RunTraced( ETraceLevel::Hops );
		for( uint i=0; i<3; ++i )
			Inject( i );
		Flush();
		let all = Read();
		ASSERT_EQ( all.size(), 12u );
		TraceQuery query{ .Start=At(0), .Limit=5 };
		auto page = Store->Read( "flow", query );
		EXPECT_EQ( page.Records.size(), 5u );
		ASSERT_TRUE( page.Next );
		EXPECT_EQ( page.Next->Time, At(6) ) << "every record is at 06:00";
		EXPECT_EQ( page.Next->Skip, 5u ) << "five of them returned";
		query.From = page.Next;
		page = Store->Read( "flow", query );
		EXPECT_EQ( Ids(page.Records), Ids(vector<TraceRecord>{all.begin()+5, all.begin()+10}) );
		EXPECT_EQ( page.Next->Skip, 10u );
		query.From = page.Next;
		page = Store->Read( "flow", query );
		EXPECT_EQ( page.Records.size(), 2u );
		EXPECT_FALSE( page.Next ) << "the last page";
		EXPECT_EQ( Read("flow", {.Start=At(0), .Limit=5}).size(), 12u );

		auto back = Read( "flow", {.End=At(7), .Limit=5} );
		ASSERT_EQ( back.size(), 12u ) << "newest first, paged";
		std::ranges::reverse( back );
		EXPECT_EQ( Ids(back), Ids(all) );
		EXPECT_EQ( Lines(back), Lines(all) );
		EXPECT_EQ( Read("flow", {.Start=At(7), .End=At(0), .Limit=4}).front().Node, "out" ) << "a start after the end reads back too";
	}

	TEST_F( FileTraceTests, ALineageWalksUpToTheSourceAndDownToTheClones ){
		Tracer = Store->Trace( "flow", ETraceLevel::Hops, Time );
		Run( Nodes({
			Node( "in", "inject", "{}", {{"split"}} ),
			Node( "split", "split", "{}", {{"outA", "outB"}} ),
			Node( "outA", "log" ), Node( "outB", "log" )
		}) );
		Inject( jarray{1, 2} );
		Flush();
		let a = Host->Took->Of( "outA" ), b = Host->Took->Of( "outB" );
		ASSERT_EQ( a.size(), 2u );
		let part = a[1].at( "_id" ).to_number<MessageId>();//the second part, cloned for outB.
		let clone = b[1].at( "_id" ).to_number<MessageId>();
		let source = a[1].at( "_parent" ).to_number<MessageId>();
		let named = [&]( const TraceLineage& lineage ){
			vector<string> y;
			for( let& r : lineage.Records )
				y.push_back( Ƒ("{} {} {}", ToString(r.Kind), r.Node, r.Id==clone ? "clone" : r.Id==part ? "part" : r.Id==source ? "source" : "?") );
			return y;
		};
		auto lineage = Store->Lineage( "flow", clone, nullopt, nullopt, 100 );
		EXPECT_FALSE( lineage.More );
		const vector<string> expected{ "birth in source", "birth split part", "clone split clone", "hop split source", "hop outA part", "hop outB clone" };
		EXPECT_EQ( named(lineage), expected ) << "the source's path, the part's, and the clone's own, in time order, every record at one time, so none after the clone's first;  not the first part's.  A part is born as the split sends it, before the split's own hop completes.";
		EXPECT_EQ( named(Store->Lineage("flow", part, nullopt, nullopt, 100)), expected ) << "the part's descendants come with it";
		EXPECT_EQ( named(Store->Lineage("flow", source, nullopt, nullopt, 100)).size(), 10u ) << "the source's lineage is every record";
		EXPECT_TRUE( Store->Lineage("flow", part, nullopt, nullopt, 2).More ) << "the limit cuts it short";
		EXPECT_TRUE( Store->Lineage("flow", 12345, nullopt, nullopt, 100).Records.empty() );
	}

	TEST_F( FileTraceTests, TheRollRemovesTheDaysPastRetention ){
		Open( []( TraceSettings& s ){ s.Retention = days{2}; } );
		Store->Start();//the roll now, and at each midnight on the way.
		for( uint day=0; day<3; ++day ){
			if( day )
				Time->Advance( 24h );
			Birth( "flow", 100+day );
		}
		EXPECT_EQ( Store->Days("flow"), (vector<Day>{March9, March10, March11}) ) << "11 March, 06:00:  nothing is older than two days";
		Time->AdvanceTo( At(0)+days{3} );//12 March, 00:00:  the roll.
		EXPECT_EQ( Store->Days("flow"), (vector<Day>{March10, March11}) );
		EXPECT_FALSE( fs::exists(Dir/"flow"/"2026"/"3"/"9") ) << "an emptied day directory goes with its file";
		Birth( "flow", 200 );//the file of a day the roll took is made again.
		Open( {} );
		EXPECT_EQ( Read("flow", {.Start=At(0)+days{3}}).size(), 1u );
	}

	TEST_F( FileTraceTests, TheRollRemovesTheOldestPastMaxSizeAcrossTheFlows ){
		Time->Step( -days{3} );//6 March.
		Birth( "b", 1 );
		Time->Advance( 24h );
		Birth( "a", 2 );
		Time->Advance( 24h );
		Birth( "a", 3 );
		Time->Advance( 24h );
		Birth( "a", 4 );
		Birth( "b", 5 );
		const Day March6{ 2026y/March/6 }, March7{ 2026y/March/7 }, March8{ 2026y/March/8 };
		EXPECT_EQ( Store->Days("a"), (vector<Day>{March7, March8, March9}) );
		EXPECT_EQ( Store->Days("b"), (vector<Day>{March6, March9}) );
		let kept = Size( "a", March9 )+Size( "b", March9 )+Size( "a", March8 );
		Open( [&]( TraceSettings& s ){ s.MaxSize = kept; } );
		Store->Roll();
		EXPECT_EQ( Store->Days("a"), (vector<Day>{March8, March9}) ) << "the newest that fit the cap stay";
		EXPECT_EQ( Store->Days("b"), vector<Day>{March9} ) << "the oldest go, across the flows";
		Open( [&]( TraceSettings& s ){ s.MaxSize = kept-1; } );
		Store->Roll();
		EXPECT_EQ( Store->Days("a"), vector<Day>{March9} ) << "a byte less, and the day that crosses it goes too";
		Store->Purge( "a" );
		EXPECT_FALSE( fs::exists(Dir/"a") );
		EXPECT_EQ( Store->Days("b"), vector<Day>{March9} );
	}

	TEST_F( FileTraceTests, AWriteThatFailsKeepsTheRecordsForTheNextFlush ){
		fs::create_directories( Dir );
		{ std::ofstream block{ Dir/"flow" }; }//a file where the flow's directory goes.
		RunTraced( ETraceLevel::Hops );
		Inject( 7 );
		Flush();
		EXPECT_TRUE( Store->Days("flow").empty() );
		fs::remove( Dir/"flow" );
		Flush();
		EXPECT_EQ( Read().size(), 4u ) << "the records waited in the buffer";
	}

	//#451 review #1:  a status a Function body gave, whose text quickjs-ng wrote with an unmatched surrogate, reads back.
	TEST_F( FileTraceTests, AStatusThatIsNotUtf8ReadsBack ){
		auto trace = Store->Trace( "flow", ETraceLevel::Hops, Time );
		const NodeDef f{ .Id="f", .Type=ENodeType::Function };
		const string lone{ "\xED\xA0\x80" };//U+D800 as JS_ToCStringLen writes it:  not UTF-8.
		const NodeStatus status{ lone, "ring"+lone, "x"+lone };
		trace->Status( f, status );
		Flush();
		let records = Read();
		ASSERT_EQ( records.size(), 1u );
		EXPECT_EQ( *records[0].Status, status );
	}

	//#451 review #3:  a healthy flow's buffer past its cap is flushed then, with nothing dropped.
	TEST_F( FileTraceTests, ABufferPastItsCapIsFlushedNotDropped ){
		Open( []( TraceSettings& s ){ s.ValueLimit = 1<<14; } );
		auto trace = Store->Trace( "flow", ETraceLevel::Hops, Time );
		const NodeDef in{ .Id="in", .Type=ENodeType::Inject };
		const string payload( 8192, 'x' );
		constexpr uint count{ 2200 };//some 18 MB:  past the 16 MiB a buffer holds.
		for( MessageId id=1; id<=count; ++id ){
			Message m{ jobject{{"_id", id}, {"payload", payload}} };
			trace->Birth( in, m );
			trace->Complete( in, id );
		}
		Time->AdvanceTo( Time->Now() );//the flush the cap brought forward, due now rather than a minute on.
		for( uint i=0; Store->Days("flow").empty() && i<1000; ++i )
			std::this_thread::sleep_for( 10ms );
		EXPECT_EQ( Store->Days("flow"), vector<Day>{March9} ) << "written before its delay";
		Flush();
		EXPECT_EQ( Read().size(), count ) << "nothing dropped";
	}

	//#451 review #5:  a day the roll can't measure stays, and so does every day before it.
	TEST_F( FileTraceTests, ADayTheRollCannotMeasureStaysWithTheDaysBeforeIt ){
		if( ::geteuid()==0 )
			GTEST_SKIP() << "root measures any file.";
		Time->Step( -days{3} );//6 March.
		Birth( "b", 1 );
		Time->Advance( 24h );
		Birth( "a", 2 );
		Time->Advance( 24h );
		Birth( "a", 3 );
		Time->Advance( 24h );
		Birth( "a", 4 );
		Birth( "b", 5 );
		const Day March6{ 2026y/March/6 }, March7{ 2026y/March/7 }, March8{ 2026y/March/8 };
		let shut = Dir/"a"/"2026"/"3"/"8";
		fs::permissions( shut, fs::perms::none );
		Store->Roll();
		fs::permissions( shut, fs::perms::owner_all );
		EXPECT_EQ( Store->Days("a"), (vector<Day>{March7, March8, March9}) ) << "the day it couldn't measure, and the one before";
		EXPECT_EQ( Store->Days("b"), (vector<Day>{March6, March9}) ) << "and the other flow's";
	}

	//#451 review #6:  an ancestor's pass ends at the first record of the message made from it.
	TEST_F( FileTraceTests, AnAncestorsRecordsEndWhereItsDescendantBegins ){
		Tracer = Store->Trace( "flow", ETraceLevel::Hops, Time );
		Run( Nodes({
			Node( "in", "inject", "{}", {{"wait", "now"}} ),
			Node( "wait", "delay", R"({"duration":"PT1M"})", {{"late"}} ),
			Node( "now", "log" ), Node( "late", "log" )
		}) );
		Inject( 1 );
		Advance( 1min );
		Flush();
		ASSERT_EQ( Host->Took->Of("late").size(), 1u );
		let clone = Host->Took->Of( "now" )[0].at( "_id" ).to_number<MessageId>();
		let original = Host->Took->Of( "late" )[0].at( "_id" ).to_number<MessageId>();
		let lineage = Store->Lineage( "flow", clone, nullopt, nullopt, 100 );
		vector<string> lines;
		for( let& r : lineage.Records )
			lines.push_back( Ƒ("{} {} {}", ToString(r.Kind), r.Node, r.Id==clone ? "clone" : r.Id==original ? "original" : "?") );
		EXPECT_EQ( lines, (vector<string>{"birth in original", "clone in clone", "hop now clone", "hop wait original"}) ) << "not the original's hop at `late`, a minute after the clone";
		EXPECT_EQ( Store->Lineage("flow", original, nullopt, nullopt, 100).Records.size(), 5u ) << "the original's own lineage has it";
	}

	//#451 review #7:  the counts a page of whole buckets at a time.
	TEST_F( FileTraceTests, CountsPageByWholeBuckets ){
		RunTraced( ETraceLevel::Hops );
		for( uint minute=0; minute<3; ++minute ){
			Inject( minute );
			Advance( 1min );
		}
		Stop();
		Flush();
		let all = Store->Counts( "flow", At(6), At(7), 1min ).Rows;
		ASSERT_EQ( all.size(), 12u ) << "three minutes of four nodes";
		vector<CountRow> paged;
		optional<TimePoint> from;
		uint pages{};
		do{
			auto page = Store->Counts( "flow", At(6), At(7), 1min, 6, from );
			EXPECT_EQ( page.Rows.size(), 4u ) << "a second bucket would pass 6 rows";
			paged.insert( paged.end(), page.Rows.begin(), page.Rows.end() );
			from = page.Next;
			++pages;
		}while( from && pages<10 );
		EXPECT_EQ( pages, 3u );
		ASSERT_EQ( paged.size(), all.size() );
		for( uint i=0; i<all.size(); ++i ){
			EXPECT_EQ( paged[i].Time, all[i].Time );
			EXPECT_EQ( paged[i].Counts.Node, all[i].Counts.Node );
			EXPECT_EQ( paged[i].Counts.Received, all[i].Counts.Received );
		}
		EXPECT_EQ( Store->Counts("flow", At(6), At(7), 1min, 2).Rows.size(), 4u ) << "a bucket is never split";
	}

	//#451 review #9:  a day whose write fails holds back no other day.
	TEST_F( FileTraceTests, ADayThatFailsHoldsBackNoOtherDay ){
		fs::create_directories( Dir/"flow"/"2026"/"3" );
		{ std::ofstream block{ Dir/"flow"/"2026"/"3"/"8" }; }//a file where 8 March's directory goes.
		auto trace = Store->Trace( "flow", ETraceLevel::Hops, Time );
		const NodeDef in{ .Id="in", .Type=ENodeType::Inject };
		let birth = [&]( MessageId id ){
			Message m{ jobject{{"_id", id}, {"payload", 1}} };
			trace->Birth( in, m );
			trace->Complete( in, id );
		};
		Time->Step( -24h );
		birth( 1 );//8 March, late.
		Time->Step( 24h );
		birth( 2 );
		Flush();
		EXPECT_EQ( Ids(Read("flow", {.Start=At(0)})), vector<MessageId>{2} ) << "9 March's written";
		fs::remove( Dir/"flow"/"2026"/"3"/"8" );
		Flush();
		EXPECT_EQ( Ids(Read("flow", {.Start=At(0)-24h})), (vector<MessageId>{1, 2}) ) << "8 March's waited in the buffer";
	}

	//#451 review #11:  a roll asked for leaves the midnight roll as the only one.
	TEST_F( FileTraceTests, ARollAskedForSetsNoTimer ){
		Store->Start();
		Time->AdvanceTo( Time->Now() );//the start's roll.
		ASSERT_EQ( Time->Pending(), 1u ) << "the next midnight's";
		Store->Roll();
		Store->Roll();
		EXPECT_EQ( Time->Pending(), 1u );
		Time->AdvanceTo( At(0)+days{1} );
		EXPECT_EQ( Time->Pending(), 1u ) << "the midnight roll sets the next";
	}

	//#451 review #13:  a purge after the store stopped still takes a flow's directory, one an earlier process wrote.
	TEST_F( FileTraceTests, APurgeAfterTheStopTakesTheDirectory ){
		Birth( "flow", 1 );
		Open( {} );
		Store->Stop();
		Store->Purge( "flow" );
		EXPECT_FALSE( fs::exists(Dir/"flow") );
		fs::create_directories( Dir/"kept" );
		TraceStore::Remove( Dir/"kept", ".." );
		EXPECT_TRUE( fs::exists(Dir/"kept") ) << "a slug that is no directory name is refused";
		TraceStore::Remove( Dir, "kept" );
		EXPECT_FALSE( fs::exists(Dir/"kept") );
	}

	//#451 review #14:  a valueLimit of 0 keeps a message's json whole.
	TEST_F( FileTraceTests, AValueLimitOf0KeepsTheWholeJson ){
		Open( []( TraceSettings& s ){ s.ValueLimit = 0; } );
		RunTraced( ETraceLevel::Hops );
		Inject( jvalue{string(5000, 'x')} );
		Flush();
		let records = Read();
		ASSERT_FALSE( records.empty() );
		EXPECT_FALSE( records[0].Cut );
		EXPECT_EQ( parse(*records[0].Json).at("payload").as_string().size(), 5000u );
	}
}