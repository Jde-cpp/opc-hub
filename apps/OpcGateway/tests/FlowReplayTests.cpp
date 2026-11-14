//Flows 7 (#453):  a replay (docs/flows/spec.md *Replay*) - replayFlow over records written into a group through its edit, a
//dry run capturing an `opc out`'s values under its run's id with nothing written, its flow_runs row, a run paged past a
//small readLimit, a second run refused while one is under way, cancelFlowRun, an `opc in` taking its group's records and
//one no group holds refusing the run by its node, and the reads' `run`.  The cases replay seconds and minutes:  the days
//are the library's, on the stream's clock (libs/flows/tests/ReplayTests.cpp).
#include <jde/opc/uatypes/DateTime.h>
#include "utils/FlowFixture.h"

namespace Jde::Opc::Gateway::Tests{
	struct FlowReplayTests : FlowFixture{
	protected:
		Ω Replay( uint32 flow, TimePoint from, TimePoint to, const jvalue& mode="dry" )ε->jobject{
			return Mutate( "replayFlow( flow:$flow, from:$from, to:$to, mode:$mode ){ id flow version start finish mode started userName status }", {{"flow", flow}, {"from", ToIsoString(from)}, {"to", ToIsoString(to)}, {"mode", mode}} ).as_object();
		}
		Ω RunId( const jobject& replayed )ε->uint32{ return Json::AsNumber<uint32>( replayed, "id" ); }
		Ω Cancel( uint32 run )ε->jobject{ return Mutate( "cancelFlowRun( id:$id ){ id status finished }", {{"id", run}} ).as_object(); }
		Ω Runs( uint32 flow )ε->jarray{ return QL().QuerySync<jarray>( "flowRuns( flowId:$flow ){ id version start finish mode started finished identityId userName status }", {{"flow", flow}}, User() ); }
		Ω Row( uint32 flow, uint32 run )ε->jobject{
			for( let& row : Runs(flow) ){
				if( Json::AsNumber<uint32>(row.as_object(), "id")==run )
					return row.as_object();
			}
			THROW( "Flow {} has no run {}.", flow, run );
		}
		//The run's row once it says how the run ended.
		Ω Ended( uint32 flow, uint32 run )ε->jobject{
			jobject row;
			EXPECT_TRUE( WaitUntil([&]{ row = Row( flow, run ); return !row.at("status").is_null(); }, 30s) ) << "run " << run << " did not end";
			return row;
		}
		//The run's captures in the range, oldest first.
		Ω Captured( uint32 flow, uint32 run, TimePoint from, TimePoint to )ε->jarray{
			return Json::AsArray( Trace({{"flow", flow}, {"start", ToIsoString(from)}, {"end", ToIsoString(to)}, {"run", run}, {"kind", "captured"}}), "records" );
		}
		//RpmManual's value a second from `t0`, 1000 on, written into the group through its edit.
		Ω Record( uint32 group, TimePoint t0, uint count=60 )ε->void{
			vector<Sample> samples;
			for( uint i=0; i<count; ++i )
				samples.push_back( {RpmManual, t0+seconds{i}, 1000+i} );
			let inserted = Update( "createHistory", group, samples );
			THROW_IF( inserted.Values!=vector<StatusCode>(count, UA_STATUSCODE_GOODENTRYINSERTED), "The group took {} of the {} records.", std::ranges::count(inserted.Values, (StatusCode)UA_STATUSCODE_GOODENTRYINSERTED), count );
		}
		//The line a run's end logged, `Run <id> of flow …`.
		Ω EndLine( const Logging::Capture& capture, uint32 run )ι->string{
			let head = Ƒ( "Run {} of flow", run );
			let found = capture.Find( [&]( const Logging::Entry& e ){ return e.Message().find( head )!=string::npos && e.Message().find( "pages" )!=string::npos; } );
			return found.empty() ? string{} : found.front().Message();
		}
	};

	//A minute of one-second records, written into a group through its edit, replayed dry through an `opc out`:  each value a
	//capture under the run's id at its record's time, and none written.  A second run, paged at a small readLimit, has the same.
	TEST_F( FlowReplayTests, ADryReplayCapturesAnOpcOutsValues ){
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		let [group, rpm, host, _] = OneNodeGroup{ "replay-dry", RpmManual };
		auto open = EnforcedHistGroups();//an edit of a group's history needs an enforced grant, as an `opc out`'s admission does.
		const TimePoint from{ floor<seconds>(Clock::now())-2h }, to{ from+59s };//a TimePoint, as the gateway writes one.
		Record( group, from );
		let flow = CreateFlow( "replays", false );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("src", "group in", serialize(jobject{{"group", group}}), {{"w"}}), N("w", "opc out", On(Ex(RpmManual)))})).at("running"), false ) << "a flow is replayed before it is let loose";
		let before = Current( RpmManual ).AsNumber<double>();

		let started = Replay( flow, from, to );
		EXPECT_EQ( Json::AsNumber<uint>(started, "version"), 1u );
		EXPECT_EQ( started.at("mode"), "dry" );
		EXPECT_TRUE( started.at("status").is_null() ) << "answered as the run starts";
		let run = RunId( started );
		let row = Ended( flow, run );
		EXPECT_EQ( row.at("status"), "completed" ) << serialize( row );
		EXPECT_FALSE( row.at("finished").is_null() );
		EXPECT_EQ( row.at("mode"), "dry" );
		EXPECT_EQ( Json::AsNumber<uint>(row, "version"), 1u );
		EXPECT_EQ( row.at("userName"), started.at("userName") );
		EXPECT_EQ( Json::AsNumber<uint>(row, "identityId"), User().Value );

		let captured = Captured( flow, run, from, to );
		ASSERT_EQ( captured.size(), 60u );
		for( uint i=0; i<captured.size(); ++i ){
			let& record = captured[i].as_object();
			EXPECT_EQ( record.at("node"), "w" );
			EXPECT_EQ( Json::AsNumber<uint>(record, "runId"), run );
			EXPECT_EQ( record.at("json").to_number<double>(), 1000.0+i ) << "the value it would have written";
			EXPECT_EQ( record.at("time"), jvalue{ToIsoString(from+seconds{i})} ) << "at its record's time";
		}
		EXPECT_EQ( Current(RpmManual).AsNumber<double>(), before ) << "and wrote none";
		EXPECT_TRUE( Json::AsArray(Trace({{"flow", flow}, {"start", ToIsoString(from)}, {"end", ToIsoString(to)}}), "records").empty() ) << "the live trace has none of the run's records";
		EXPECT_TRUE( Has(EndLine(capture, run), "completed, after 60 records in 1 pages") ) << EndLine( capture, run );

		let lineageQL = "flowMessage( flow:$flow, id:$id, run:$run ){ more records{ node kind runId } }";
		let first = captured[0].as_object().at( "msgId" );
		EXPECT_EQ( Lines(QL().QuerySync<jobject>(lineageQL, {{"flow", flow}, {"id", first}, {"run", run}}, User())), (vector<string>{"birth src", "captured w", "hop w"}) );
		EXPECT_TRUE( Lines(QL().QuerySync<jobject>(lineageQL, {{"flow", flow}, {"id", first}, {"run", nullptr}}, User())).empty() ) << "a read with no run is the live trace's";
		let counts = [&]( const jvalue& of ){
			uint received{};
			let page = QL().QuerySync<jobject>( "flowCounts( flow:$flow, start:$start, end:$end, by:$by, run:$run ){ counts{ node received } }", {{"flow", flow}, {"start", ToIsoString(floor<minutes>(from))}, {"end", ToIsoString(to+1min)}, {"by", "PT1H"}, {"run", of}}, User() );
			for( let& count : Json::AsArray(page, "counts") ){
				if( count.as_object().at("node")=="w" )
					received += Json::AsNumber<uint>( count.as_object(), "received" );
			}
			return received;
		};
		EXPECT_EQ( counts(run), 60u );
		EXPECT_EQ( counts(nullptr), 0u );

		absl::Cleanup restore = []{ try{ Settings::Set( "/gateway/flows/readLimit", Hist::Settings::DefaultReadLimit ); }catch( const std::exception& ){} };
		Settings::Set( "/gateway/flows/readLimit", 7 );
		let paged = RunId( Replay(flow, from, to) );
		EXPECT_EQ( Ended(flow, paged).at("status"), "completed" );
		EXPECT_TRUE( Has(EndLine(capture, paged), "60 records in 9 pages") ) << EndLine( capture, paged );
		let again = Captured( flow, paged, from, to );
		ASSERT_EQ( again.size(), 60u ) << "none lost between two pages, and none twice";
		for( uint i=0; i<again.size(); ++i ){
			let& a = again[i].as_object(); let& b = captured[i].as_object();
			EXPECT_TRUE( a.at("json")==b.at("json") && a.at("time")==b.at("time") && a.at("msgId")==b.at("msgId") ) << i << ":  the same records give the same trace twice, its ids among it";
		}
		EXPECT_EQ( Captured(flow, run, from, to).size(), 60u ) << "the first run's are its own";

		Mutate( "updateFlow( id:$id, trace:\"off\" )", {{"id", flow}} );
		let untraced = RunId( Replay(flow, from, to) );
		EXPECT_EQ( Ended(flow, untraced).at("status"), "completed" );
		EXPECT_EQ( Captured(flow, untraced, from, to).size(), 60u ) << "a run is traced at errors at the least, so its captures are written";
		EXPECT_TRUE( Json::AsArray(Trace({{"flow", flow}, {"start", ToIsoString(from)}, {"end", ToIsoString(to)}, {"run", untraced}, {"kind", "birth"}}), "records").empty() ) << "and no more than that";
	}

	TEST_F( FlowReplayTests, OneRunAtATimeAndACancelEndsIt ){
		let flow = CreateFlow( "replay-long", false );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("t", "timer", R"({"every":"PT0.001S"})", {{"d"}}), N("d", "debug")})).at("running"), false );
		Mutate( "updateFlow( id:$id, trace:\"counts\" )", {{"id", flow}} );//a month of ticks then writes its minutes alone.
		let to = floor<seconds>( Clock::now() ), from = to-24h*30;
		let run = RunId( Replay(flow, from, to) );
		EXPECT_EQ( Flows().ReplayOf(flow), optional<FlowRunPK>{run} );
		let second = Refused( [&]{ Replay( flow, from, to ); } );
		EXPECT_EQ( second.Status, EHttpStatus::Conflict ) << second.What;
		EXPECT_TRUE( Has(second.What, Ƒ("has run {} under way", run)) );
		EXPECT_TRUE( Row(flow, run).at("status").is_null() ) << "under way";

		let cancelled = Cancel( run );
		EXPECT_EQ( cancelled.at("status"), "cancelled" );
		EXPECT_EQ( Row(flow, run).at("status"), "cancelled" );
		EXPECT_FALSE( Row(flow, run).at("finished").is_null() );
		EXPECT_FALSE( Flows().ReplayOf(flow).has_value() );
		let again = Refused( [&]{ Cancel( run ); } );
		EXPECT_EQ( again.Status, EHttpStatus::Conflict ) << again.What;
		EXPECT_TRUE( Has(again.What, "is not under way:  it cancelled") );
		EXPECT_EQ( Refused([&]{ Cancel( run+1000 ); }).Status, EHttpStatus::NotFound );

		let next = RunId( Replay(flow, to-1s, to) );
		EXPECT_EQ( Ended(flow, next).at("status"), "completed" ) << "another, once the first has ended";
		EXPECT_EQ( Runs(flow).size(), 2u );
		EXPECT_EQ( Refused([&]{ Replay( flow, to, from ); }).Status, EHttpStatus::BadRequest ) << "from before to";
		EXPECT_EQ( Refused([&]{ Replay( flow, from, to, "wet" ); }).Status, EHttpStatus::BadRequest );
		EXPECT_EQ( Refused([&]{ Replay( flow+1000, from, to ); }).Status, EHttpStatus::NotFound );
		auto enforced = EnforcedFlows();
		ScopeFlow( flow, ERights::Read );//no Update.
		EXPECT_EQ( Refused([&]{ Replay( flow, to-1s, to ); }).Status, EHttpStatus::Forbidden ) << "Update on the flow";
		EXPECT_EQ( Refused([&]{ Cancel( next ); }).Status, EHttpStatus::Forbidden );
		UnscopeFlow( flow );
	}

	//An `opc in` whose node a group holds takes the group's records, and one no group holds refuses the run by its node.  A
	//log's line is its capture.
	TEST_F( FlowReplayTests, AnOpcInReplaysTheRecordsOfTheGroupThatHoldsItsNode ){
		let flow = CreateFlow( "replay-reads", false );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( DeployBySession(flow, Graph({N("src", "opc in", On(Ex(RpmManual)), {{"line"}}), Line("rpm {{payload}}")})).at("running"), false );
		const TimePoint from{ floor<seconds>(Clock::now())-3h }, to{ from+9s };
		let ungrouped = Refused( [&]{ Replay( flow, from, to ); } );
		EXPECT_EQ( ungrouped.Status, EHttpStatus::BadRequest ) << ungrouped.What;
		EXPECT_TRUE( ungrouped.What.starts_with("Node 'src':  ") && Has(ungrouped.What, "is in no group:  a replay has no records of it.") ) << ungrouped.What;
		EXPECT_TRUE( Runs(flow).empty() ) << "a refusal writes no run";

		let [group, rpm, host, _] = OneNodeGroup{ "replay-reads", RpmManual };
		auto open = EnforcedHistGroups();
		Record( group, from, 10 );
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		let run = RunId( Replay(flow, from, to) );
		EXPECT_EQ( Ended(flow, run).at("status"), "completed" );
		let captured = Captured( flow, run, from, to );
		ASSERT_EQ( captured.size(), 10u );
		EXPECT_EQ( captured[0].as_object().at("node"), "line" );
		EXPECT_EQ( captured[0].as_object().at("json"), "rpm 1000" );
		EXPECT_EQ( captured[9].as_object().at("json"), "rpm 1009" );
		EXPECT_TRUE( Lines(capture, "replay-reads").empty() ) << "and no line of the log";
		let births = Json::AsArray( Trace({{"flow", flow}, {"start", ToIsoString(from)}, {"end", ToIsoString(to)}, {"run", run}, {"kind", "birth"}}), "records" );
		ASSERT_EQ( births.size(), 10u );
		let& message = Json::AsObject( births[0].as_object(), "json" );
		EXPECT_EQ( Json::AsSV(message, "node/connection"), OpcServerSlug ) << "a message as the node sends it live";
		EXPECT_EQ( message.at("_time"), jvalue{ToIsoString(from)} );
		EXPECT_EQ( message.at("sourceTime"), jvalue{ToIsoString(from)} );
	}
}