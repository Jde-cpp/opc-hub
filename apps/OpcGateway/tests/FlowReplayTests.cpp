//Flows 7 (#453):  a replay (docs/flows/spec.md *Replay*) - replayFlow over records written into a group through its edit, a
//dry run capturing an `opc out`'s values under its run's id with nothing written, its flow_runs row, a run paged past a
//small readLimit, a second run refused while one is under way, cancelFlowRun, an `opc in` taking its group's records and
//one no group holds refusing the run by its node, a caller who may not read a source's group and a member that is none
//of it refused with no row, a `to` after now refused, and the reads' `run`.  With the flow server (#454), a committed run
//writing a `variable`'s values into the history of the group that holds it, over a range the group already stored, the
//range its own after it, a value outside it refused, and an edit its group refuses counted.  The cases replay seconds
//and minutes:  the days are the library's, on the stream's clock (libs/flows/tests/ReplayTests.cpp).
#include "utils/FlowFixture.h"
#include "../src/flows/FlowNodes.h"

namespace Jde::Opc::Gateway::Tests{
	struct FlowReplayTests : FlowFixture{
	protected:
		Ω Replay( uint32 flow, TimePoint from, TimePoint to, const jvalue& mode="dry" )ε->jobject{
			return Mutate( "replayFlow( flow:$flow, from:$from, to:$to, mode:$mode ){ id flow version start finish mode started userName status }", {{"flow", flow}, {"from", ToIsoString(from)}, {"to", ToIsoString(to)}, {"mode", mode}} ).as_object();
		}
		Ω RunId( const jobject& replayed )ε->uint32{ return Json::AsNumber<uint32>( replayed, "id" ); }
		//Another user than the suite's, with no role:  Update on a flow while flows is unenforced, and no Read on an
		//enforced group.  Created the first time, as the suites share one db in a run.
		Ω Stranger()ε->UserPK{
			auto app = AppClient();
			auto user = app->QuerySync( "user( slug:$slug, deleted:null ){ id }", {{"slug", "flowReplayStranger"}} );
			if( user.empty() )
				user = app->QuerySync<jobject>( "createUser( slug:$slug, name:$name ){ id }", {{"slug", "flowReplayStranger"}, {"name", "A replay's stranger"}} );
			return UserPK{ Json::AsNumber<UserPK::Type>(user, "id") };
		}
		Ω ReplayAs( UserPK user, uint32 flow, TimePoint from, TimePoint to )ε->jvalue{
			return BlockAwait( QL::QLAwait<jvalue>{"replayFlow( flow:$flow, from:$from, to:$to ){ id }", {{"flow", flow}, {"from", ToIsoString(from)}, {"to", ToIsoString(to)}}, user, QLPtr()} );
		}
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
		//The run's captures in the range, oldest first, every page of them.
		Ω Captured( uint32 flow, uint32 run, TimePoint from, TimePoint to )ε->jarray{
			jarray y;
			jobject args{ {"flow", flow}, {"start", ToIsoString(from)}, {"end", ToIsoString(to)}, {"run", run}, {"kind", "captured"} };
			for( ;; ){
				let page = Trace( args );
				for( let& record : Json::AsArray(page, "records") )
					y.push_back( record );
				let next = page.if_contains( "continuation" );
				if( !next || !next->is_string() )
					return y;
				args["continuation"] = *next;
			}
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
		EXPECT_TRUE( WaitUntil([&]{ return !EndLine( capture, run ).empty(); }) ) << "the run's line comes behind its row";
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

		let denied = Refused( [&]{ ReplayAs( Stranger(), flow, from, to ); } );
		EXPECT_EQ( denied.Status, EHttpStatus::Forbidden ) << denied.What;
		EXPECT_TRUE( denied.What.starts_with("Node 'src':  ") && Has(denied.What, "which needs Read for who runs it") ) << denied.What;
		EXPECT_EQ( Runs(flow).size(), 1u ) << "a caller who may not read the group the run reads is refused, with no row";

		absl::Cleanup restore = []{
			try{
				Settings::Set( "/gateway/flows/readLimit", Hist::Settings::DefaultReadLimit );
				Flows().Stop();
				Flows().Start();
			}
			catch( const std::exception& e ){
				ADD_FAILURE() << "readLimit wasn't put back:  " << e.what();
			}
		};
		Settings::Set( "/gateway/flows/readLimit", 7 );
		Flows().Stop();//the host reads it as it starts, for the runs' pages and the trace's.
		Flows().Start();
		let paged = RunId( Replay(flow, from, to) );
		EXPECT_EQ( Ended(flow, paged).at("status"), "completed" );
		EXPECT_TRUE( WaitUntil([&]{ return !EndLine( capture, paged ).empty(); }) ) << "the run's line comes behind its row";
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
		let ahead = Refused( [&]{ Replay( flow, to, Clock::now()+1h ); } );
		EXPECT_EQ( ahead.Status, EHttpStatus::BadRequest ) << ahead.What;
		EXPECT_TRUE( Has(ahead.What, "is after now") ) << ahead.What;
		EXPECT_EQ( Runs(flow).size(), 2u ) << "no row for a refusal";
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
		let members = CreateFlow( "replay-members", false );
		absl::Cleanup purgeMembers = [=]{ PurgeFlow( members ); };
		ASSERT_EQ( Deploy(members, Graph({N("src", "group in", serialize(jobject{{"group", group}, {"members", jarray{Ex(Rpm4)}}}), {{"line"}}), Line("{{payload}}")})).at("running"), false );
		let none = Refused( [&]{ Replay( members, from, to ); } );
		EXPECT_EQ( none.Status, EHttpStatus::BadRequest ) << none.What;
		EXPECT_TRUE( none.What.starts_with("Node 'src':  ") && Has(none.What, "is no member of its group") ) << none.What;
		EXPECT_TRUE( Runs(members).empty() ) << "a stream's refusal writes no run";
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

	//A committed replay (spec *Replay*):  a derived variable over a range its group already stored, written through the
	//group's edit as the flow's deployer, read back through `history` with the replaced values as modifications.  Every
	//value is a capture as well, and the flow server hears none.
	TEST_F( FlowReplayTests, ACommittedReplayWritesAVariablesHistory ){
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		let [source, rpm, sourceHost, _] = OneNodeGroup{ "commit-source", RpmManual };
		auto open = EnforcedHistGroups();
		const TimePoint from{ floor<seconds>(Clock::now())-4h }, to{ from+59s };
		Record( source, from );
		const string slug{ "commits" };
		let flow = CreateFlow( slug, false );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let graph = Graph( {N("src", "group in", serialize(jobject{{"group", source}}), {{"f"}}), N("f", "function", R"({"code":"msg.payload = msg.payload*2;\nreturn msg;"})", {{"v", "w"}}),
			N("v", "variable", R"({"name":"doubled","dataType":"Double"})"), N("w", "opc out", On(Ex(RpmManual)))} );
		ASSERT_EQ( Deploy(flow, graph).at("running"), false ) << "a flow is replayed before it is let loose";
		let ungrouped = Refused( [&]{ Replay( flow, from, to, "commit" ); } );
		EXPECT_EQ( ungrouped.Status, EHttpStatus::BadRequest ) << ungrouped.What;
		EXPECT_TRUE( ungrouped.What.starts_with("Node 'v':  ") && Has(ungrouped.What, "is in no group this gateway historizes:  a committed run has no history to write it into") ) << ungrouped.What;
		EXPECT_TRUE( Runs(flow).empty() ) << "a refusal writes no run";
		let dry = RunId( Replay(flow, from, to) );
		EXPECT_EQ( Ended(flow, dry).at("status"), "completed" ) << "a dry run needs no group";

		auto held = GroupVariable( "commit-derived", slug, "doubled" );
		absl::Cleanup purgeGroup = [&]{ held.Host = nullptr; Purge( held.Id, Items() ); };
		let variable = Variable( slug, "doubled" );
		//What the collector stored while the flow ran live:  every sixth second of the range.
		jarray stored;
		for( uint i=0; i<60; i+=6 )
			stored.push_back( jobject{{"node", variable}, {"source", UADateTime{from+seconds{i}}.ToJson()}, {"value", -1.5}} );
		let inserted = Edit( "createHistory( group: $group, values: $values ){ values{ node source status } nodes{ node status } }", {{"group", held.Id}, {"values", move(stored)}} );
		ASSERT_EQ( inserted.Values, vector<StatusCode>(10, UA_STATUSCODE_GOODENTRYINSERTED) );

		ScopeHistGroup( held.Id, ERights::Read );//the deployer's Update on the group that holds the variable.
		let denied = Refused( [&]{ Replay( flow, from, to, "commit" ); } );
		EXPECT_EQ( denied.Status, EHttpStatus::Forbidden ) << denied.What;
		EXPECT_TRUE( denied.What.starts_with("Node 'v':  ") && Has(denied.What, "as the flow's deployer, which needs Update") ) << denied.What;
		UnscopeHistGroup( held.Id );
		EXPECT_EQ( Runs(flow).size(), 1u ) << "and no row";

		let before = Current( RpmManual ).AsNumber<double>();
		let writes = FlowServer::Instance()->Writes();
		let started = Replay( flow, from, to, "commit" );
		EXPECT_EQ( started.at("mode"), "commit" );
		let run = RunId( started );
		let row = Ended( flow, run );
		EXPECT_EQ( row.at("status"), "completed" ) << serialize( row );
		EXPECT_EQ( row.at("mode"), "commit" );
		EXPECT_TRUE( WaitUntil([&]{ return !EndLine( capture, run ).empty(); }) ) << "the run's line comes behind its row";
		EXPECT_TRUE( Has(EndLine(capture, run), "60 records in 1 pages, 60 values committed.") ) << EndLine( capture, run );
		EXPECT_FALSE( EndLine(capture, dry).empty() ) << "the dry run's line, which the check below would pass without";
		EXPECT_FALSE( Has(EndLine(capture, dry), "committed") ) << EndLine( capture, dry );

		let request = Request{ .Start=from, .End=to+1s };
		let history = ReadAll( Group(held.Id), jarray{variable}, request );
		ASSERT_EQ( history.size(), 60u ) << "its group's history over the range is the run's";
		for( uint i=0; i<history.size(); ++i ){
			EXPECT_EQ( history[i].Value.to_number<double>(), 2000.0+2*i ) << i;
			EXPECT_EQ( history[i].Source, optional<TimePoint>{from+seconds{i}} ) << "at its record's time";
			EXPECT_EQ( history[i].Status, UA_STATUSCODE_GOOD );
		}
		let modifications = ReadAll( Group(held.Id), jarray{variable}, Request{.Start=from, .End=to+1s, .Modified=true} );
		uint cleared{};
		for( let& was : modifications ){
			if( was.Value.is_number() && was.Value.to_number<double>()==-1.5 && was.Modified && was.Modified->Type=="Delete" ){//beside the Insert that stored it.
				++cleared;
				EXPECT_EQ( jvalue{was.Modified->User}, started.at("userName") ) << "by the flow's deployer";
			}
		}
		EXPECT_EQ( cleared, 10u ) << "each value the collector had stored, cleared:  " << modifications.size() << " modifications";

		let captured = Captured( flow, run, from, to );
		ASSERT_EQ( captured.size(), 120u ) << "every sink a capture, the variable among them";
		uint variables{};
		for( let& record : captured ){
			if( record.as_object().at("node")=="v" )
				EXPECT_EQ( record.as_object().at("json").to_number<double>(), 2000.0+2*variables++ );
		}
		EXPECT_EQ( variables, 60u );
		EXPECT_EQ( Current(RpmManual).AsNumber<double>(), before ) << "an `opc out` writes none, committed or not";
		EXPECT_EQ( FlowServer::Instance()->Writes(), writes ) << "and the flow server hears none of a run";

		let again = RunId( Replay(flow, from, to, "commit") );
		EXPECT_EQ( Ended(flow, again).at("status"), "completed" );
		EXPECT_EQ( ReadAll(Group(held.Id), jarray{variable}, request).size(), 60u ) << "a second run replaces the first's";

		//A version that writes one value in ten:  the range is its run's, the values of the run before between its own cleared, and
		//those after its last.
		let variableNode = N( "v", "variable", R"({"name":"doubled","dataType":"Double"})" );
		let body = [&]( string code ){ return Graph( {N("src", "group in", serialize(jobject{{"group", source}}), {{"f"}}), N("f", "function", serialize(jobject{{"code", move(code)}}), {{"v"}}), variableNode} ); };
		ASSERT_EQ( Deploy(flow, body("return msg.payload%10==0 ? msg : null;")).at("running"), false );
		let tenth = RunId( Replay(flow, from, to, "commit") );
		EXPECT_EQ( Ended(flow, tenth).at("status"), "completed" );
		let sparse = ReadAll( Group(held.Id), jarray{variable}, request );
		ASSERT_EQ( sparse.size(), 6u ) << "the range is the run's alone";
		for( uint i=0; i<sparse.size(); ++i ){
			EXPECT_EQ( sparse[i].Source, optional<TimePoint>{from+seconds{10*i}} ) << i;
			EXPECT_EQ( sparse[i].Value.to_number<double>(), 1000.0+10*i ) << i;
		}
		//A body that moves a value out of the range:  refused, which fails the run, and clears nothing.
		ASSERT_EQ( Deploy(flow, body(Ƒ("msg.sourceTime = '{}';\nreturn msg;", ToIsoString(to+1h)))).at("running"), false );
		let outside = RunId( Replay(flow, from, to, "commit") );
		let status = Json::AsString( Ended(flow, outside), "status" );
		EXPECT_TRUE( Has(status, "failed:  60 of 60 values were not committed, the first variable 'doubled':  its source time") && Has(status, "is outside the run's range") ) << status;
		EXPECT_EQ( ReadAll(Group(held.Id), jarray{variable}, request).size(), 6u ) << "the history as the run before left it";
		EXPECT_TRUE( ReadAll(Group(held.Id), jarray{variable}, Request{.Start=to+1s, .End=to+2h}).empty() ) << "and none outside it";
	}

	//An edit its group refuses, the group removed meanwhile:  its values counted as failed, the group's reason kept, and the
	//wait for them over, where the group answers on the caller's own stack.
	TEST_F( FlowReplayTests, AnEditItsGroupRefusesIsCounted ){
		auto group = mu<OneNodeGroup>( "commit-removed", RpmManual );
		auto host = group->Host;
		let member = group->Index;
		group = nullptr;//purged, the test's hold on its host kept.
		const TimePoint from{ floor<seconds>(Clock::now())-1h };
		auto commits = ms<FlowCommits>( HistHost::Instance().Writer(AppClient()->UserPK()), from, from+1min );
		commits->Hold( host, member, "removed" );
		Value value{ jvalue{1.5}, &UA_TYPES[UA_TYPES_DOUBLE] };
		value.sourceTimestamp = UADateTime{ from }.UA();
		value.hasSourceTimestamp = true;
		commits->Add( host, member, "removed", move(value) );
		std::promise<void> drained;
		auto done = drained.get_future();
		commits->Drained( [&]{ drained.set_value(); } );
		ASSERT_EQ( done.wait_for(10s), std::future_status::ready ) << "the edit was never answered";
		let result = commits->Result();
		EXPECT_EQ( result.Failed, 1u );
		EXPECT_EQ( result.Uncleared, 1u ) << "the delete before it, in the same edit";
		EXPECT_EQ( result.Written, 0u );
		EXPECT_TRUE( Has(result.First, "variable 'removed'") && Has(result.First, "was removed") ) << result.First;
	}
}