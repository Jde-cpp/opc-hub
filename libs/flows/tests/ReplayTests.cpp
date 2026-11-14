//The replay (docs/flows/spec.md *Replay*):  a graph run over scripted records on the stream's clock, which takes no
//wall-clock time, so a week is replayed here;  the same records giving the same trace twice, the timed nodes and a body's
//timer firing at stream times in order, the sources that tick and those that are silent, a cancel mid-run, a page that
//cannot be read, the captures, and a run's records apart from the live trace's in the flow's files.
#include <jde/flows/FileTrace.h>
#include <jde/flows/Replay.h>
#include "helpers.h"
#include "../../fwk/tests/testsDir.h"

namespace Jde::Opc::Flows::Tests{
	//A run's host, as a gateway's:  an `opc in`'s and a `group in`'s records scripted by the node's id, `Page` at a time;  a
	//`timer` that ticks every `every` seconds of its props on the run's clock;  and sinks that capture the payload they
	//would have sent, kept here as `node@seconds payload`, the seconds the run's clock since `From`.
	struct ReplayHost final : IReplayHost{
		struct Scripted final : IStream{
			Scripted( vector<StreamRecord> records, uint page, optional<uint> failAt )ι:_records{ move(records) }, _page{ page }, _failAt{ failAt }{}
			α Next()ε->vector<StreamRecord> override{
				THROW_IF( _failAt && _pages++==*_failAt, "The group's file could not be read." );
				vector<StreamRecord> y;
				for( ; _next<_records.size() && y.size()<_page; ++_next )
					y.push_back( move(_records[_next]) );
				return y;
			}
		private:
			vector<StreamRecord> _records;
			const uint _page;
			const optional<uint> _failAt;
			uint _next{}, _pages{};
		};
		struct Ticker final : ISource{
			α Start()ε->void override{ Arm(); }
		private:
			α Arm()ι->void{
				Schedule( Duration{seconds{Json::FindNumber<uint>(Def().Props, "every").value_or(1)}}, [this]{
					auto m = Make();
					m.Json["payload"] = "tick";
					Send( move(m) );
					Arm();
				} );
			}
		};
		struct Capturer final : ISink{
			Capturer( ReplayHost& host )ι:_host{ host }{}
			α Receive( Message& m )ε->void override{
				Capture( m.Id(), m.Payload() );
				lg _{ _host._mutex };
				_host._took.push_back( Ƒ("{}@{} {}", Def().Id, duration_cast<seconds>(Now()-_host.From).count(), serialize(m.Payload())) );
			}
		private:
			ReplayHost& _host;
		};
		α Stream( const NodeDef& node )ε->up<IStream> override{
			let records = Records.find( node.Id );
			THROW_IF( records==Records.end(), "'ns=2;s=Count' on 'plc1' is in no group:  a replay has no records of it." );
			return mu<Scripted>( records->second, Page, FailAt );
		}
		α Source( const NodeDef& node )ε->up<ISource> override{
			Made.push_back( string{ToString(node.Type)} );
			return mu<Ticker>();
		}
		α Sink( const NodeDef& )ε->up<ISink> override{ return mu<Capturer>( *this ); }
		//A record of `payload`, `after` From.
		α Add( sv node, Duration after, jvalue payload )ι->void{ Records[string{node}].push_back( {From+after, jobject{{"payload", move(payload)}, {"topic", "ns=2;s=Count"}}} ); }
		α Took()Ι->vector<string>{ lg _{ _mutex }; return _took; }

		TimePoint From{ At(6) };
		flat_map<string,vector<StreamRecord>> Records;
		uint Page{ 3 };
		optional<uint> FailAt;//the page a stream throws at.
		vector<string> Made;//the types Source was asked for.
	private:
		mutable mutex _mutex;
		vector<string> _took;
	};

	//A record whole, as text:  what two runs of a stream are compared by.
	Ξ Text( const TraceRecord& r )ι->string{
		string ports;
		for( let port : r.Ports )
			ports += Ƒ( "{},", port );
		return Ƒ( "{} {} {} {} {} [{}] {} {} {}", ToIsoString(r.Time), r.Id, r.Parent, r.Node, ToString(r.Kind), ports, r.Json.value_or("-"), r.Error, r.Status ? Ƒ("{}/{}/{}", r.Status->Fill, r.Status->Shape, r.Status->Text) : "-" );
	}

	struct ReplayTests : ::testing::Test{
		struct Ending final{ ERunEnd End; string Why; };
		//The graph run over the host's records from From for `span`, to its end.
		α Replay( const jvalue& graph, Duration span, ETraceLevel level=ETraceLevel::Hops, sp<ITrace> trace={}, sp<StreamClock> clock={} )->Ending{
			auto run = Begin( graph, span, level, move(trace), move(clock) );
			EXPECT_TRUE( run->Start() );
			return Wait();
		}
		α Begin( const jvalue& graph, Duration span, ETraceLevel level=ETraceLevel::Hops, sp<ITrace> trace={}, sp<StreamClock> clock={} )->sp<Flows::Run>{
			auto read = Graph::Read( graph );
			if( !read ){
				ADD_FAILURE() << read.error().NodeId << ":  " << read.error().Message;
				read = Graph{};
			}
			Clock = clock ? move( clock ) : ms<StreamClock>( Host->From );
			Trace = ms<MemoryTrace>( level, Clock );
			_ended = std::promise<Ending>{};
			Running = ms<Flows::Run>( move(*read), Flow::Config{.Name="test", .Trace=trace ? move(trace) : sp<ITrace>{Trace}, .Queue=Queue},
				Flows::Run::Config{ .Clock=Clock, .Host=Host, .From=Host->From, .To=Host->From+span, .Ended=[this]( ERunEnd end, string why ){ _ended.set_value( {end, move(why)} ); }, .Pace=Pace } );
			return Running;
		}
		α Wait()->Ending{
			auto ended = _ended.get_future();
			if( ended.wait_for(30s)!=std::future_status::ready ){
				ADD_FAILURE() << "The run did not end.";
				return { ERunEnd::Failed, "it did not end" };
			}
			return ended.get();
		}
		//`src`, an `opc in`, into `nodes`, each into a log of its own, `out<n>`.
		Ω Fanned( std::initializer_list<std::pair<sv,sv>> nodes )->jvalue{
			jarray y, wires;
			uint i{};
			for( let& [type, props] : nodes ){
				let id = Ƒ( "n{}", i ), out = Ƒ( "out{}", i++ );
				y.push_back( Node(id, type, props, {{out}}) );
				y.push_back( Node(out, "log") );
				wires.emplace_back( id );
			}
			auto src = Node( "src", "opc in" );
			src["wires"] = jarray{ move(wires) };
			y.push_back( move(src) );
			return jobject{ {"nodes", move(y)} };
		}

		sp<ReplayHost> Host{ ms<ReplayHost>() };
		sp<StreamClock> Clock;
		sp<MemoryTrace> Trace;
		sp<Flows::Run> Running;
		function<void( absl::AnyInvocable<void()> )> Pace;
		uint Queue{ 1000 };
	private:
		std::promise<Ending> _ended;
	};

	TEST( StreamClockTests, TimersRunInDueOrderAtTheirOwnTime ){
		StreamClock clock{ At(6) };
		vector<string> ran;
		let note = [&]( sv name ){ ran.push_back( Ƒ("{}@{}", name, duration_cast<seconds>(clock.Now()-At(6)).count()) ); };
		clock.Schedule( At(6, 0, 3), [&]{ note( "c" ); } );
		clock.Schedule( Duration{1s}, [&]{
			note( "a" );
			clock.Schedule( Duration{1s}, [&]{ note( "inside" ); } );
		} );
		clock.Schedule( At(6, 0, 1), [&]{ note( "b" ); } );
		let cancelled = clock.Schedule( At(6, 0, 2), [&]{ note( "cancelled" ); } );
		EXPECT_TRUE( clock.Cancel(cancelled) );
		EXPECT_FALSE( clock.Cancel(cancelled) );
		EXPECT_EQ( clock.Now(), At(6) ) << "it stands until it is moved";
		EXPECT_FALSE( clock.AdvanceTo(At(6, 0, 5), 1) ) << "more are due";
		EXPECT_EQ( ran, vector<string>{"a@1"} );
		EXPECT_EQ( clock.Now(), At(6, 0, 1) ) << "at the last it ran";
		EXPECT_TRUE( clock.AdvanceTo(At(6, 0, 5)) );
		EXPECT_EQ( ran, (vector<string>{"a@1", "b@1", "inside@2", "c@3"}) ) << "in due order, a tie in the order scheduled, one a callback schedules among them";
		EXPECT_EQ( clock.Now(), At(6, 0, 5) );
		EXPECT_EQ( clock.Pending(), 0u );
		clock.Schedule( At(6), [&]{ note( "late" ); } );
		EXPECT_TRUE( clock.AdvanceTo(At(6, 0, 4)) ) << "it never goes back";
		EXPECT_EQ( ran.back(), "late@5" );
		EXPECT_EQ( clock.Now(), At(6, 0, 5) );
	}

	//A delay, a window's close and a body's timer fire at the stream's times, in the order they would have live.
	TEST_F( ReplayTests, TheTimedNodesFireAtStreamTimes ){
		Host->Add( "src", 0s, 1 );
		Host->Add( "src", 10s, 2 );
		Host->Add( "src", 130s, 3 );
		let ended = Replay( Fanned({
			{"delay", R"({"duration":"PT1M"})"},
			{"window", R"({"duration":"PT1M"})"},
			{"function", R"({"code":"await new Promise( wake => setTimeout( wake, 30000 ) ); msg.payload = `${msg.payload} at ${new Date().toISOString()}`; return msg;"})"}
		}), 5min );
		EXPECT_EQ( ended.End, ERunEnd::Completed ) << ended.Why;
		vector<string> timed, windows;
		for( let& line : Host->Took() )
			( line.starts_with("out1@") ? windows : timed ).push_back( line );
		EXPECT_EQ( timed, (vector<string>{
			R"(out2@30 "1 at 2026-03-09T06:00:30.000Z")", R"(out2@40 "2 at 2026-03-09T06:00:40.000Z")", "out0@60 1", "out0@70 2", R"(out2@160 "3 at 2026-03-09T06:02:40.000Z")", "out0@190 3"
		}) );
		ASSERT_EQ( windows.size(), 2u ) << "a window at each minute that held a value";
		EXPECT_TRUE( windows[0].starts_with("out1@60 ") && Has(windows[0], R"("count":2)") ) << windows[0];
		EXPECT_TRUE( windows[1].starts_with("out1@180 ") && Has(windows[1], R"("count":1)") ) << windows[1];
		EXPECT_EQ( Running->Delivered(), 3u );
		EXPECT_EQ( Running->Pages(), 1u );
		EXPECT_EQ( Clock->Now(), At(6, 5) ) << "the clock runs on to the range's end";
		for( let& record : Trace->Records() ){
			if( record.Kind==ERecordKind::Birth && record.Node=="src" )
				EXPECT_EQ( parse(*record.Json).at("_time"), Iso(record.Time) ) << "a message's time is its record's";
		}
	}

	TEST_F( ReplayTests, TheSameStreamGivesTheSameTraceTwice ){
		for( uint i=0; i<40; ++i )
			Host->Add( "src", seconds{7*i}, i );
		Host->Page = 7;
		let graph = Fanned({
			{"function", R"({"code":"const n = ( context.get( 'n' ) || 0 ) + 1; context.set( 'n', n ); setTimeout( () => node.send( { payload: [ n, Date.now(), msg._id ] } ), 2500 ); if( n % 5 == 0 ) throw new Error( 'fifth' ); return msg;"})"},
			{"window", R"({"duration":"PT30S"})"},
			{"delay", R"({"mode":"rate","count":1,"per":"PT10S"})"},
			{"switch", R"({"rules":[{"t":"gt","v":20},{"t":"else"}]})"}
		});
		let first = Replay( graph, 10min );
		vector<string> records;
		for( let& record : Trace->Records() )
			records.push_back( Text(record) );
		let took = Host->Took();
		let minutes = Trace->Minutes().size();
		EXPECT_EQ( first.End, ERunEnd::Completed ) << first.Why;
		EXPECT_EQ( Running->Pages(), 6u ) << "40 records, 7 a page";
		ASSERT_GT( records.size(), 400u );
		ASSERT_GT( took.size(), 100u );

		Host = ms<ReplayHost>();
		for( uint i=0; i<40; ++i )
			Host->Add( "src", seconds{7*i}, i );
		Host->Page = 40;
		let second = Replay( graph, 10min );
		EXPECT_EQ( second.End, ERunEnd::Completed ) << second.Why;
		vector<string> again;
		for( let& record : Trace->Records() )
			again.push_back( Text(record) );
		EXPECT_EQ( again, records ) << "each record, its time, its ids and its json, whatever the pages";
		EXPECT_EQ( Host->Took(), took );
		EXPECT_EQ( Trace->Minutes().size(), minutes );
		EXPECT_TRUE( std::ranges::any_of(records, []( let& r ){ return Has( r, " error " ); }) ) << "the body's error among them";
	}

	//A timer ticks within the range, its last at the range's end;  a connection, an inject and a link in send nothing.
	TEST_F( ReplayTests, ATimerTicksAndTheLiveSourcesAreSilent ){
		let ended = Replay( Nodes({
			Node( "t", "timer", R"({"every":20})", {{"out"}} ), Node( "c", "connection", "{}", {{"out"}} ),
			Node( "i", "inject", "{}", {{"out"}} ), Node( "l", "link in", R"({"name":"x"})", {{"out"}} ), Node( "out", "log" )
		}), 1min );
		EXPECT_EQ( ended.End, ERunEnd::Completed ) << ended.Why;
		EXPECT_EQ( Host->Took(), (vector<string>{R"(out@20 "tick")", R"(out@40 "tick")", R"(out@60 "tick")"}) );
		EXPECT_EQ( Host->Made, vector<string>{"timer"} ) << "the host makes no connection node for a run";
		EXPECT_EQ( Running->Delivered(), 0u );
	}

	//More timers than a step runs before the strand's other work has its turn:  the steps after run the rest, in order.
	TEST_F( ReplayTests, TimersPastAStepsShareRunInTheStepsAfter ){
		Host->Add( "src", 2s, "early" );
		Host->Add( "src", 300s, "late" );
		let ended = Replay( Nodes({Node("t", "timer", R"({"every":1})", {{"out"}}), Node("src", "opc in", "{}", {{"out"}}), Node("out", "log")}), 10min, ETraceLevel::Off );
		EXPECT_EQ( ended.End, ERunEnd::Completed ) << ended.Why;
		vector<string> expected;
		for( uint second=1; second<=600; ++second ){
			expected.push_back( Ƒ("out@{} \"tick\"", second) );
			if( second==2 || second==300 )
				expected.push_back( Ƒ("out@{} \"{}\"", second, second==2 ? "early" : "late") );//a timer due at a record's time runs before it.
		}
		EXPECT_EQ( Host->Took(), expected ) << "before a record, between two and after the last";
	}

	//Two sources' records are one stream, by time, a tie the node's first in the graph.
	TEST_F( ReplayTests, TheSourcesRecordsAreMergedByTime ){
		Host->Add( "a", 5s, "a5" );
		Host->Add( "a", 20s, "a20" );
		Host->Add( "a", 90s, "past the range" );
		Host->Add( "b", 5s, "b5" );
		Host->Add( "b", 10s, "b10" );
		Host->Page = 1;
		let ended = Replay( Nodes({Node("b", "group in", "{}", {{"out"}}), Node("a", "opc in", "{}", {{"out"}}), Node("out", "log")}), 1min );
		EXPECT_EQ( ended.End, ERunEnd::Completed ) << ended.Why;
		EXPECT_EQ( Host->Took(), (vector<string>{R"(out@5 "b5")", R"(out@5 "a5")", R"(out@10 "b10")", R"(out@20 "a20")"}) );
		EXPECT_EQ( Running->Delivered(), 4u ) << "a record past the range's end is its stream's end";
	}

	//A record is through the flow before the clock moves to the next, however many messages it makes:  more than a turn of
	//the strand takes, here.
	TEST_F( ReplayTests, ARecordIsThroughTheFlowBeforeTheClockMoves ){
		jarray parts;
		for( uint i=0; i<2500; ++i )
			parts.emplace_back( i );
		Host->Add( "src", 0s, move(parts) );
		Host->Add( "src", 10s, jarray{"last"} );
		Queue = 5000;
		let ended = Replay( Nodes({Node("src", "opc in", "{}", {{"parts"}}), Node("parts", "split", "{}", {{"out"}}), Node("out", "log")}), 1min, ETraceLevel::Counts );
		EXPECT_EQ( ended.End, ERunEnd::Completed ) << ended.Why;
		let took = Host->Took();
		ASSERT_EQ( took.size(), 2501u );
		EXPECT_EQ( std::ranges::count_if(took, []( let& line ){ return line.starts_with( "out@0 " ); }), 2500 ) << "each part at its record's time";
		EXPECT_EQ( took.back(), R"(out@10 "last")" );
	}

	//A week, each record an hour's window, and an hourly timer:  on the stream's clock, no wall-clock time.
	TEST_F( ReplayTests, AWeekTakesNoTime ){
		for( uint i=0; i<28; ++i )
			Host->Add( "src", hours{6*i}+minutes{5}, i );
		let start = steady_clock::now();
		let ended = Replay( Nodes({
			Node( "src", "group in", "{}", {{"w"}} ), Node( "w", "window", R"({"duration":"PT1H"})", {{"out"}} ),
			Node( "t", "timer", R"({"every":3600})", {{"ticks"}} ), Node( "out", "log" ), Node( "ticks", "log" )
		}), days{7} );
		EXPECT_EQ( ended.End, ERunEnd::Completed ) << ended.Why;
		EXPECT_LT( steady_clock::now()-start, 20s );
		let took = Host->Took();
		EXPECT_EQ( std::ranges::count_if(took, []( let& line ){ return line.starts_with( "ticks@" ); }), 7*24 );
		vector<string> windows;
		for( let& line : took ){
			if( line.starts_with("out@") )
				windows.push_back( line.substr(0, line.find(' ')) );
		}
		ASSERT_EQ( windows.size(), 28u );
		EXPECT_EQ( windows.front(), "out@3600" );
		EXPECT_EQ( windows.back(), Ƒ("out@{}", (6*27+1)*3600) ) << "each at the hour its record's window ends";
	}

	TEST_F( ReplayTests, ACancelEndsTheRunMidStream ){
		for( uint i=0; i<100; ++i )
			Host->Add( "src", seconds{i}, i );
		std::promise<void> reached;
		absl::AnyInvocable<void()> held;
		uint steps{};
		Pace = [&]( absl::AnyInvocable<void()> next ){
			if( ++steps==4 ){
				held = std::move( next );
				reached.set_value();
			}
			else
				next();
		};
		auto run = Begin( Nodes({Node("src", "opc in", "{}", {{"d"}}), Node("d", "delay", R"({"duration":"PT1H"})", {{"out"}}), Node("out", "log")}), 1h );
		ASSERT_TRUE( run->Start() );
		ASSERT_EQ( reached.get_future().wait_for(10s), std::future_status::ready );
		EXPECT_EQ( run->Delivered(), 4u ) << "held between two records";
		run->Cancel();
		let ended = Wait();
		EXPECT_EQ( ended.End, ERunEnd::Cancelled ) << ended.Why;
		held();
		run->Cancel();
		EXPECT_EQ( run->Delivered(), 4u ) << "nothing more is delivered";
		EXPECT_TRUE( Host->Took().empty() );
		uint drops{};
		for( let& counts : run->Counts() )
			drops += counts.Drops;
		EXPECT_EQ( drops, 4u ) << "what the delay held is dropped and counted";
	}

	TEST_F( ReplayTests, APageThatCannotBeReadFailsTheRun ){
		for( uint i=0; i<10; ++i )
			Host->Add( "src", seconds{i}, i );
		Host->FailAt = 1;
		let ended = Replay( Nodes({Node("src", "group in", "{}", {{"out"}}), Node("out", "log")}), 1min );
		EXPECT_EQ( ended.End, ERunEnd::Failed );
		EXPECT_EQ( ended.Why, "The group's file could not be read." );
		EXPECT_EQ( Host->Took().size(), 3u ) << "the first page's";
	}

	TEST_F( ReplayTests, ASourceWithNoRecordsRefusesTheRun ){
		auto run = Begin( Nodes({Node("src", "opc in", "{}", {{"out"}}), Node("out", "log")}), 1min );
		EXPECT_FALSE( run->Start() );
		let ended = Wait();
		EXPECT_EQ( ended.End, ERunEnd::Failed );
		EXPECT_EQ( ended.Why, "Node 'src':  'ns=2;s=Count' on 'plc1' is in no group:  a replay has no records of it." );
		EXPECT_EQ( run->Fault()->NodeId, "src" );
	}

	//A capture is a record of its own at `hops` and at `errors`, and none below.
	TEST_F( ReplayTests, ACaptureIsARecordOfItsOwn ){
		let graph = Nodes( {Node("src", "opc in", "{}", {{"out"}}), Node("out", "log")} );
		Host->Add( "src", 1s, jobject{{"rpm", 1450}} );
		Replay( graph, 1min );
		EXPECT_EQ( Trace->Lines(), (vector<string>{"birth src [0]*", "captured out", "hop out []"}) );
		let captured = Trace->Records()[1];
		EXPECT_EQ( captured.Json, R"({"rpm":1450})" ) << "what the sink would have sent";
		EXPECT_EQ( captured.Id, Trace->Records()[0].Id );
		EXPECT_EQ( captured.Time, At(6, 0, 1) );
		Replay( graph, 1min, ETraceLevel::Errors );
		EXPECT_EQ( Trace->Lines(), vector<string>{"captured out"} );
		Replay( graph, 1min, ETraceLevel::Counts );
		EXPECT_TRUE( Trace->Lines().empty() );
		EXPECT_EQ( Trace->Minutes().size(), 1u );
		EXPECT_EQ( ToRecordKind("captured"), ERecordKind::Captured );
		EXPECT_EQ( ToString(ERecordKind::Captured), "captured" );
	}

	//A run's records carry its id into the flow's files, at the stream's times, apart from the live trace's:  each read is
	//of one or of the other.
	TEST_F( ReplayTests, ARunsRecordsAreApartFromTheLiveTraces ){
		let dir = Jde::Tests::TestsDir( "flowReplay", true );
		fs::remove_all( dir );
		auto hostClock = ms<ManualClock>( At(12) );
		auto store = ms<TraceStore>( TraceSettings{.Root=dir, .Delay=1min, .Zone=std::chrono::locate_zone("UTC")}, hostClock );
		let flush = [&]{
			std::promise<void> done;
			store->Flush( "flow", [&]{ done.set_value(); } );
			ASSERT_EQ( done.get_future().wait_for(10s), std::future_status::ready );
		};
		//The live flow's, at 06:00:01, with the id the run's first message has.
		auto live = store->Trace( "flow", ETraceLevel::Hops, ms<ManualClock>(At(6, 0, 1)) );
		const NodeDef src{ .Id="src", .Type=ENodeType::OpcIn };
		let id = MessageIds{ At(6) }.Next();
		Message m{ jobject{{"_id", id}, {"payload", "live"}} };
		live->Birth( src, m );
		live->Complete( src, id );
		live->Minute( {At(6), {NodeCounts{"src", 0, {9}}}} );

		Host->Add( "src", 1s, 1 );
		Host->Add( "src", 61s, 2 );
		auto clock = ms<StreamClock>( At(6) );
		let ended = Replay( Nodes({Node("src", "opc in", "{}", {{"out"}}), Node("out", "log")}), 2min, ETraceLevel::Hops, store->Trace("flow", ETraceLevel::Hops, clock, {}, 7), clock );
		EXPECT_EQ( ended.End, ERunEnd::Completed ) << ended.Why;
		EXPECT_GT( store->Buffered("flow"), 0u );
		flush();
		EXPECT_EQ( store->Buffered("flow"), 0u );

		let liveRecords = store->Read( "flow", {.Start=At(0)} ).Records;
		ASSERT_EQ( Lines(liveRecords), vector<string>{"birth src []*"} ) << "a read with no run is the live trace's";
		EXPECT_EQ( liveRecords[0].Run, 0u );
		let run = store->Read( "flow", {.Start=At(0), .Run=7} ).Records;
		EXPECT_EQ( Lines(run), (vector<string>{"birth src [0]*", "captured out", "hop out []", "birth src [0]*", "captured out", "hop out []"}) );
		EXPECT_TRUE( std::ranges::all_of(run, []( let& r ){ return r.Run==7; }) );
		ASSERT_EQ( run.size(), 6u );
		EXPECT_EQ( run[0].Time, At(6, 0, 1) ) << "at the stream's times";
		EXPECT_EQ( run[3].Time, At(6, 1, 1) );
		EXPECT_EQ( run[0].Id, id ) << "a run's ids count from its range's start, so they are its own alone";
		EXPECT_TRUE( store->Read("flow", {.Start=At(0), .Run=8}).Records.empty() );
		EXPECT_EQ( store->Read("flow", {.Start=At(0), .Kind=ERecordKind::Captured, .Run=7}).Records.size(), 2u );

		EXPECT_EQ( Lines(store->Lineage("flow", id, nullopt, nullopt, 100).Records), vector<string>{"birth src []*"} ) << "the live message's";
		EXPECT_EQ( Lines(store->Lineage("flow", id, nullopt, nullopt, 100, 7).Records), (vector<string>{"birth src [0]*", "captured out", "hop out []"}) ) << "the run's, of the same id";

		let liveCounts = store->Counts( "flow", At(6), At(7), 1h ).Rows;
		ASSERT_EQ( liveCounts.size(), 1u );
		EXPECT_EQ( liveCounts[0].Counts.Sent, vector<uint>{9} );
		let runCounts = store->Counts( "flow", At(6), At(7), 1h, 0, {}, 7 ).Rows;
		ASSERT_EQ( runCounts.size(), 2u ) << "the run's two nodes";
		EXPECT_EQ( runCounts[0].Counts.Node, "src" );
		EXPECT_EQ( runCounts[0].Counts.Sent, vector<uint>{2} );
		EXPECT_EQ( runCounts[1].Counts.Received, 2u );
		store->Stop();
		fs::remove_all( dir );
	}

	//Quiet waits for the queues alone, where Idle waits for what a node holds too.
	struct QuietTests : FlowTest{};
	TEST_F( QuietTests, QuietWaitsForTheQueuesAlone ){
		Run( Nodes({Node("in", "inject", "{}", {{"w"}}), Node("w", "opc out", "{}", {{"out"}}), Node("out", "log")}) );
		vector<string> ran;
		Inject( "hold" );
		OnStrand( [&]{
			jobject message{ {"payload", 1} };
			Deployed->Inject( "in", &message );
			Deployed->Quiet( [&]{ ran.push_back( Ƒ("quiet after {}", Took()) ); } );
		} );
		EXPECT_EQ( ran, vector<string>{"quiet after 1"} ) << "once what was queued is through, with a message held";
		Sync();
		EXPECT_EQ( ran.size(), 1u ) << "once";
		Host->Takers["w"]->Answer();
		Sync();
	}
}