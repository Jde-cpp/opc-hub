//OEE phase 2 (#400):  the six tables, OeeHost and the write path from the stream (docs/oee/spec.md *Machines*, *The engine*,
//*Stops*, *Intervals*), over a group of the embedded OpcServer's pump1 members - status, totalCount, rejectCount and
//reasonCode - with a machine over it made through the QL;  and phase 3 (#401), the rebuilds:  a history edit, a late record,
//a marker, the idle detection and recomputeOee, each making the rows it touches again.  A shift's cases run as a backfill:  the
//records written with past source times before the machine is made, so the replay, where the stream is the clock, closes every
//interval at once;  the clock's cases run on the manual clock the suite hands the host, which each test makes anew at the wall clock's time.
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/settings.h>
#include <jde/opc/uatypes/DateTime.h>
#include "utils/HistGroupFixture.h"
#include "../src/oee/OeeHost.h"
#include "../../../libs/historian/tests/ManualClock.h"

namespace Jde::Opc::Gateway::Tests{
	using Hist::Tests::ManualClock;
	using Oee::ERole;
	struct OeeIntervalRow final{ TimePoint Start; uint32 Ms, Run, Stop, Unknown; double Total, Good, Reject; };

	struct OeeTests : HistGroupFixture{
	protected:
		Ω SetUpTestSuite()ε->void{
			HistGroupFixture::SetUpTestSuite();
			_base = floor<minutes>( Clock::now() )-3h;//the backfills' windows, each case's later than the last's and every one in the past.
		}
		Ω TearDownTestSuite()ι->void{
			OeeHost::Instance().Stop();
			OeeHost::Instance().Start();//on the system clock again, for the suites after this one.
			HistGroupFixture::TearDownTestSuite();
		}
		α SetUp()->void override{
			HistGroupFixture::SetUp();
			if( IsSkipped() )
				return;
			if( !Settings::FindObject("/gateway/oee") )
				GTEST_SKIP() << "The gateway computes no OEE:  /gateway/oee.";
			OeeHost::Instance().Stop();
			_clock = ms<ManualClock>( Clock::now() );
			OeeHost::Instance().Start( _clock );
		}
		Ω Window()ι->TimePoint{ return _base+(_windows++)*15min; }
		Ω Settle()ι->Duration{ return OeeHost::Instance().Settle(); }
		Ω Iso( TimePoint t )ι->string{ return ToIsoString<seconds>( t ); }
		//The manual clock at the wall clock:  a live record's arrival is stamped by it, and the engine's offset is arrival less
		//source, so a clock the settle advance left ahead of the wall waits for it, a few seconds at most.
		Ω Now()ι->TimePoint{
			for( let until = std::chrono::steady_clock::now()+5s; Clock::now()<_clock->Now() && std::chrono::steady_clock::now()<until; )
				std::this_thread::sleep_for( 10ms );
			_clock->AdvanceTo( Clock::now() );
			return Ms( Clock::now() );
		}
		//The engine's source offset:  its timers are due on the wall clock that much later than the engine's time.
		Ω Offset( const sp<OeeMachineHost>& engine )ι->Duration{ let s = engine->State(); return s ? s->Offset : Duration::zero(); }
		Ω Oee()ι->OeeHost&{ return OeeHost::Instance(); }
		//What f's refusal said, or empty when it didn't throw;  a status other than BadRequest fails the test.
		Ω Refused( function<void()> f, SL sl=SRCE_CUR )->string{
			try{
				f();
			}
			catch( const Exception& e ){
				EXPECT_EQ( e.HttpStatus(), EHttpStatus::BadRequest ) << "at " << sl.line() << ":  " << e.what();
				return e.what();
			}
			return {};
		}
		Ω Status( function<void()> f )->EHttpStatus{
			try{
				f();
			}
			catch( const Exception& e ){
				return e.HttpStatus();
			}
			return EHttpStatus::None;
		}
		Ω WriteTotal( uint32_t value, TimePoint source )ε->void{ UA_UInt32 v = value; Write( Total1, &v, UA_TYPES[UA_TYPES_UINT32], source ); }
		Ω WriteReject( uint32_t value, TimePoint source )ε->void{ UA_UInt32 v = value; Write( Reject1, &v, UA_TYPES[UA_TYPES_UINT32], source ); }
		Ω WriteReason( int32_t value, TimePoint source )ε->void{ UA_Int32 v = value; Write( Reason1, &v, UA_TYPES[UA_TYPES_INT32], source ); }
		//pump1's four members at `t`, before a group joins them:  each item's first value then carries it, the bound the engine opens with.
		Ω Baseline( TimePoint t, bool status=true, uint32_t total=100, uint32_t reject=5 )ε->void{
			WriteStatus( status, t );
			WriteTotal( total, t );
			WriteReject( reject, t );
			WriteReason( 0, t );
		}
		Ω Intervals( uint32 machine )ε->vector<OeeIntervalRow>{
			vector<OeeIntervalRow> y;
			for( let& r : DS()->Select({Ƒ("select start, ms, run, stop, unknown, total, good, reject from {} where oee_machine_id=? order by start", GetViewPtr(OeeTable::Intervals)->SqlName()), {DB::Value{(uint)machine}}}) )
				y.push_back( {Ticks(r, 0), r.Get<uint32>(1), r.Get<uint32>(2), r.Get<uint32>(3), r.Get<uint32>(4), r.Get<double>(5), r.Get<double>(6), r.Get<double>(7)} );
			return y;
		}
		Ω Stops( uint32 machine )ε->vector<Oee::Stop>{
			vector<Oee::Stop> y;
			for( let& r : DS()->Select({Ƒ("select start, finish, code, unknown, oee_stop_id from {} where oee_machine_id=? order by start", GetViewPtr(OeeTable::Stops)->SqlName()), {DB::Value{(uint)machine}}}) )
				y.push_back( OeeSql::ToStop(r) );
			return y;
		}
		Ω Processed( uint32 machine )ε->optional<TimePoint>{
			auto rows = BlockAwait( SelectOeeMachines({.Key=DB::Key{(uint)machine}, .IncludeDeleted=true}) );
			THROW_IF( rows.empty(), "Machine {} not found.", machine );
			return rows.front().Processed;
		}
		Ω Ticks( const DB::Row& r, uint i )ι->TimePoint{ return UADateTime{ UA_DateTime{r.Get<int64_t>(i)} }.Time(); }
		Ω Row( const vector<OeeIntervalRow>& rows, TimePoint start )ε->const OeeIntervalRow&{
			let p = std::ranges::find( rows, start, &OeeIntervalRow::Start );
			THROW_IF( p==rows.end(), "No interval row at {} among {}.", Iso(start), rows.size() );
			return *p;
		}
		//The machine's engine once the snapshot is replayed, it runs on the clock and every row of the replay is written.
		Ω WaitLive( uint32 machine )ε->sp<OeeMachineHost>{
			sp<OeeMachineHost> engine;
			THROW_IF( !WaitUntil([&]{ engine = Oee().Engine( machine ); return engine && engine->Live() && engine->Idle(); }, 20s), "Machine {}'s engine never went live.", machine );
			return engine;
		}
		Ω WaitIdle( const sp<OeeMachineHost>& engine )ε->void{ THROW_IF( !WaitUntil([&]{ return engine->Idle(); }, 15s), "The machine's writes never settled." ); }
		//A rebuild asked for and run in place:  the engine has asked for `count` and nothing is queued or running.
		Ω WaitRebuilt( const sp<OeeMachineHost>& engine, uint count=1 )ε->void{ THROW_IF( !WaitUntil([&]{ return engine->Rebuilds()>=count && engine->Idle(); }, 20s), "The machine's rebuild never ran:  {} asked.", engine->Rebuilds() ); }
		//A rebuild that was a restart:  another engine over the machine, live and idle.
		Ω WaitReplaced( uint32 machine, const sp<OeeMachineHost>& old )ε->sp<OeeMachineHost>{
			sp<OeeMachineHost> engine;
			THROW_IF( !WaitUntil([&]{ engine = Oee().Engine( machine ); return engine && engine!=old && engine->Live() && engine->Idle(); }, 20s), "Machine {}'s engine was never replaced.", machine );
			return engine;
		}
		Ω ExpectNoOverlap( const vector<OeeIntervalRow>& rows )->void{
			for( uint i=1; i<rows.size(); ++i )
				EXPECT_GE( rows[i].Start, rows[i-1].Start+milliseconds{rows[i-1].Ms} ) << "the row at " << Iso(rows[i].Start) << " overlaps the one before";
		}
		Ω ExpectSame( const vector<OeeIntervalRow>& rows, const vector<OeeIntervalRow>& expected )->void{
			ASSERT_EQ( rows.size(), expected.size() );
			for( uint i=0; i<rows.size(); ++i ){
				EXPECT_EQ( rows[i].Start, expected[i].Start ) << i;
				EXPECT_EQ( rows[i].Ms, expected[i].Ms ) << i;
				EXPECT_EQ( rows[i].Run, expected[i].Run ) << i;
				EXPECT_EQ( rows[i].Stop, expected[i].Stop ) << i;
				EXPECT_EQ( rows[i].Unknown, expected[i].Unknown ) << i;
				EXPECT_EQ( rows[i].Total, expected[i].Total ) << i;
				EXPECT_EQ( rows[i].Good, expected[i].Good ) << i;
				EXPECT_EQ( rows[i].Reject, expected[i].Reject ) << i;
			}
		}
		//The wall clock early in its minute, so a live record stamped now falls well before the minute's end.
		Ω EarlyInTheMinute()ι->void{
			while( (Clock::now()-floor<minutes>(Clock::now()))>35s )
				std::this_thread::sleep_for( 500ms );
		}
		//A shift's records on pump1, with past source times, from `t0`:  ten units in its first minute, a stop from 1:15 to 2:45 with
		//reason 7 set just before it, twenty units and two rejects in its fourth minute, and a counter reset in its fifth.
		Ω WriteShift( TimePoint t0 )ε->void{
			WriteTotal( 110, t0+30s );
			WriteReason( 7, t0+70s );
			WriteStatus( false, t0+75s );
			WriteStatus( true, t0+165s );
			WriteReason( 0, t0+170s );
			WriteTotal( 130, t0+200s );
			WriteReject( 7, t0+205s );
			WriteTotal( 5, t0+250s );
		}
		Ω ExpectRow( const vector<OeeIntervalRow>& rows, TimePoint start, uint32 run, uint32 stop, double total, double good, double reject, SL sl=SRCE_CUR )->void{
			let& row = Row( rows, start );
			EXPECT_EQ( row.Ms, 60000u ) << Iso(start) << " at " << sl.line();
			EXPECT_EQ( row.Run, run ) << Iso(start) << " at " << sl.line();
			EXPECT_EQ( row.Stop, stop ) << Iso(start) << " at " << sl.line();
			EXPECT_EQ( row.Unknown, 0u ) << Iso(start) << " at " << sl.line();
			EXPECT_EQ( row.Total, total ) << Iso(start) << " at " << sl.line();
			EXPECT_EQ( row.Good, good ) << Iso(start) << " at " << sl.line();
			EXPECT_EQ( row.Reject, reject ) << Iso(start) << " at " << sl.line();
		}
		Ω ExpectShift( const vector<OeeIntervalRow>& rows, const vector<Oee::Stop>& stops, TimePoint t0 )->void{
			ASSERT_GE( rows.size(), 5u );
			let expect = [&]( uint minute, uint32 run, uint32 stop, double total, double good, double reject ){ ExpectRow( rows, t0+minutes{minute}, run, stop, total, good, reject ); };
			expect( 0, 60000, 0, 10, 10, 0 );
			expect( 1, 15000, 45000, 0, 0, 0 );
			expect( 2, 15000, 45000, 0, 0, 0 );
			expect( 3, 60000, 0, 20, 18, 2 );
			expect( 4, 60000, 0, 5, 5, 0 );//the reset counts whole.
			ASSERT_EQ( stops.size(), 1u );
			EXPECT_EQ( stops[0].Start, t0+75s );
			ASSERT_TRUE( stops[0].End );
			EXPECT_EQ( *stops[0].End, t0+165s );
			EXPECT_EQ( stops[0].Code, optional<int64_t>{7} );
			EXPECT_FALSE( stops[0].Unknown );
		}

		//A group over pump1's four members, subscribed with each first value in, and the machine a test makes over it:  purged as
		//it goes, machine first, as `Pump1Machine pump{ "slug", t0 };`.
		struct Pump1Machine final{
			Pump1Machine( sv slug, TimePoint baseline, bool status=true, uint32_t total=100, uint32_t reject=5 )ε:Slug{ slug }{
				ItemsBefore = Items();
				Baseline( baseline, status, total, reject );
				Group = CreateGroup( slug );
				try{
					for( let [role, node] : {std::pair{ERole::State, Status1}, std::pair{ERole::Total, Total1}, std::pair{ERole::Reject, Reject1}, std::pair{ERole::Reason, Reason1}} )
						Members.emplace( role, AddNode(Group, node) );
					GroupHost = HistGroupFixture::Host( Group );
					WaitSubscribed( GroupHost, 4 );
					for( let& [_,member] : Members )
						WaitStored( GroupHost, member, Joined, 1 );
				}
				catch( ... ){
					Purge( Group, ItemsBefore );
					throw;
				}
			}
			Pump1Machine( const Pump1Machine& )=delete;
			~Pump1Machine(){
				GroupHost = nullptr;
				try{
					if( Id )
						Mutate( "purgeOeeMachine( id:$id )", {{"id", Id}} );
				}
				catch( const std::exception& e ){
					ADD_FAILURE() << "purgeOeeMachine(" << Id << "):  " << e.what();
				}
				Purge( Group, ItemsBefore );
			}
			//The machine, from `from`, with a signal per role over pump1's member for it, then the start its last edit asks for.
			α Create( TimePoint from, vector<ERole> roles={ERole::State, ERole::Total, ERole::Reject, ERole::Reason}, jobject args={} )ε->uint32{
				Id = CreateMachine( from, move(args) );
				for( let role : roles )
					Signals.emplace( role, AddSignal(role) );
				_clock->Advance( Settle() );
				return Id;
			}
			α CreateMachine( optional<TimePoint> from, jobject args={} )ε->uint32{
				string ql{ "createOeeMachine( name:$name, slug:$slug, histGroupId:$group" };
				jobject vars{ {"name", Ƒ("Machine {}", Slug)}, {"slug", Slug}, {"group", Group} };
				if( from ){
					ql += ", from:$from";
					vars["from"] = Iso( *from );
				}
				for( let& [key,value] : args ){
					ql += Ƒ( ", {}:${}", key, key );
					vars[key] = value;
				}
				Id = HistGroupFixture::Id( Mutate(ql+" ){ id }", move(vars)) );
				return Id;
			}
			α AddSignal( ERole role, jobject args={} )ε->uint32{ return AddSignal( role, Members.at(role), move(args) ); }
			α AddSignal( ERole role, uint32 member, jobject args={} )ε->uint32{
				string ql{ "createOeeMachineSignal( oeeMachineId:$machine, role:$role, histGroupNodeId:$node" };
				jobject vars{ {"machine", Id}, {"role", Oee::ToString(role)}, {"node", member} };
				for( let& [key,value] : args ){
					ql += Ƒ( ", {}:${}", key, key );
					vars[key] = value;
				}
				return HistGroupFixture::Id( Mutate(ql+" ){ id }", move(vars)) );
			}
			string Slug;
			uint32 Group{};
			uint32 Id{};
			flat_map<ERole,uint32> Members;//pump1's hist_group_nodes row per role.
			flat_map<ERole,uint32> Signals;
			sp<HistGroupHost> GroupHost;
			uint ItemsBefore{};
		};

		inline static sp<ManualClock> _clock;
		inline static TimePoint _base;
		inline static uint _windows{};
	};

	//The six tables synced, each readable, the settings reaching the host and the host enabled.
	TEST_F( OeeTests, TheTablesSyncAndTheSettingsReachTheHost ){
		for( sv table : {"oeeMachines", "oeeMachineSignals", "oeeReasons"} )
			EXPECT_NO_THROW( Mutate(Ƒ("{}{{ id }}", table)) ) << table;
		for( let table : {OeeTable::Stops, OeeTable::Intervals, OeeTable::Codings} )
			EXPECT_NO_THROW( DS()->Select({Ƒ("select count(*) from {}", GetViewPtr(table)->SqlName())}) ) << table;
		EXPECT_EQ( Settle(), 2s ) << "Opc.Tests.jsonnet's oee.settle";
		EXPECT_TRUE( Oee().Enabled() );
		EXPECT_NO_THROW( Oee().TestEnabled() );
	}

	//A template member one of whose members a machine's signal names:  refused before any group's member goes.
	TEST_F( OeeTests, ATemplateMemberAMachineUsesStays ){
		let itemsBefore = Items();
		let templateId = Id( Mutate("createHistTemplate( name:$name, slug:$slug ){ id }", {{"name", "OEE template"}, {"slug", "oeeTemplate"}}) );
		absl::Cleanup purgeTemplate = [=]{ try{ Mutate( "purgeHistTemplate( id:$id )", {{"id", templateId}} ); }catch( const std::exception& e ){ ADD_FAILURE() << e.what(); } };
		let member = Id( Mutate("createHistTemplateNode( histTemplateId:$t, path:$path ){ id }", {{"t", templateId}, {"path", serialize(jarray{jobject{{"nsu", PumpsUri}, {"name", "status"}}})}}) );
		let group = [&]( sv slug, UA_UInt32 root ){
			return Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection, histTemplateId:$template, root:$root ){ id }", {{"name", slug}, {"slug", slug}, {"connection", _connection}, {"template", templateId}, {"root", serialize(Ex(root))}}) );
		};
		let first = group( "oeeTemplateFirst", Pump3 );
		absl::Cleanup purgeFirst = [=]{ Purge( first, itemsBefore ); };
		let second = group( "oeeTemplateSecond", Pump4 );
		absl::Cleanup purgeSecond = [=]{ try{ Mutate( "purgeHistGroup( id:$id )", {{"id", second}} ); }catch( const std::exception& e ){ ADD_FAILURE() << e.what(); } };
		let nodes = [&]( uint32 id ){ return Mutate( "histGroupNodes( histGroupId:$group ){ id }", {{"group", id}} ).as_array(); };
		ASSERT_EQ( nodes(first).size(), 1u );
		ASSERT_EQ( nodes(second).size(), 1u );
		let machine = Id( Mutate("createOeeMachine( name:$name, slug:$slug, histGroupId:$group ){ id }", {{"name", "OEE template machine"}, {"slug", "oeeTemplateMachine"}, {"group", second}}) );
		absl::Cleanup purgeMachine = [=]{ try{ Mutate( "purgeOeeMachine( id:$id )", {{"id", machine}} ); }catch( const std::exception& e ){ ADD_FAILURE() << e.what(); } };
		Mutate( "createOeeMachineSignal( oeeMachineId:$machine, role:\"State\", histGroupNodeId:$node ){ id }", {{"machine", machine}, {"node", Id(nodes(second)[0])}} );

		EXPECT_NE( Refused([&]{ Mutate( "purgeHistTemplateNode( id:$id )", {{"id", member}} ); }).find("purge the signal"), string::npos );
		EXPECT_EQ( nodes(first).size(), 1u ) << "refused before the first group's member went";
		EXPECT_EQ( nodes(second).size(), 1u );
	}

	//A machine over the pumps group with its signals, made through the QL:  its `processed` is the `from` its create named, its
	//rows hold the group, and its engine starts `settle` after its last signal, not before.
	TEST_F( OeeTests, AMachineOverThePumpsGroup ){
		let t0 = Window();
		Pump1Machine pump{ "oeeMachine", t0-1min };
		let id = pump.CreateMachine( t0, {{"idealCycle", 1000.0}, {"countUnit", "units"}} );
		EXPECT_EQ( Processed(id), optional<TimePoint>{t0} );
		auto machine = Oee().Find( id );
		ASSERT_TRUE( machine );
		EXPECT_EQ( machine->Row.Group, pump.Group );
		EXPECT_TRUE( Oee().HolderOf(pump.Group) );
		EXPECT_FALSE( Oee().Engine(id) ) << "no signals yet";
		for( let role : {ERole::State, ERole::Total, ERole::Reject, ERole::Reason} )
			pump.Signals.emplace( role, pump.AddSignal(role) );
		EXPECT_TRUE( Oee().UserOf(pump.Members.at(ERole::Total)) );
		EXPECT_FALSE( Oee().Engine(id) ) << "the start waits settle for the form's other signals";
		_clock->Advance( Settle() );
		auto engine = WaitLive( id );
		ASSERT_TRUE( engine->State() );
		EXPECT_EQ( engine->State()->State, Oee::EState::Running );
		EXPECT_GE( *Processed(id), t0 );
		let rows = Mutate( "oeeMachines( id:$id ){ id name processed width smallStop }", {{"id", id}} );
		ASSERT_TRUE( rows.is_array() && rows.get_array().size() ) << serialize( rows );
		EXPECT_EQ( Json::AsNumber<uint32>(rows.get_array()[0].as_object(), "width"), 60000u );
	}

	//Each refusal is a BadRequest, before anything is written:  the machine's, the signals', the reasons', the engine's three tables'
	//generic mutations, and the group's delete and purge and a member's purge while the machine stands, the group still in the
	//historian after.
	TEST_F( OeeTests, EachRefusalIsABadRequest ){
		let t0 = Window();
		Pump1Machine pump{ "oeeRefusals", t0-1min };
		let create = [&]( jobject args ){ return [&, args]{ pump.CreateMachine( t0, args ); }; };
		EXPECT_NE( Refused([&]{ Mutate( "createOeeMachine( name:\"x\", slug:\"x\" ){ id }" ); }).find("histGroupId"), string::npos );
		EXPECT_EQ( Status([&]{ Mutate( "createOeeMachine( name:\"x\", slug:\"x\", histGroupId:999999 ){ id }" ); }), EHttpStatus::NotFound );
		EXPECT_NE( Refused(create({{"width", 420000}})).find("width"), string::npos ) << "seven minutes divide no hour";
		EXPECT_NE( Refused(create({{"idealCycle", 0}})).find("idealCycle"), string::npos );
		EXPECT_NE( Refused(create({{"idleTimeout", 0}})).find("idleTimeout"), string::npos );
		EXPECT_NE( Refused(create({{"calendarId", 999999}})).find("calendarId"), string::npos );
		EXPECT_NE( Refused([&]{ pump.CreateMachine( Clock::now()+1h ); }).find("after now"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "createOeeMachine( name:$name, slug:$slug, histGroupId:$group, from:\"yesterday\" ){ id }", {{"name", "x"}, {"slug", "x"}, {"group", pump.Group}} ); }).find("from"), string::npos );
		EXPECT_TRUE( Mutate("oeeMachines( slug:\"x\" ){ id }").as_array().empty() ) << "nothing was written";

		let id = pump.CreateMachine( t0 );
		EXPECT_NE( Refused([&]{ Mutate( "createOeeMachine( name:\"y\", slug:\"y\", histGroupId:$group ){ id }", {{"group", pump.Group}} ); }).find("already has a machine"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "updateOeeMachine( id:$id, from:$from )", {{"id", id}, {"from", Iso(t0)}} ); }).find("from"), string::npos );
		let recompute = [&]( uint32 machine, string start, string end ){ return [=]{ Mutate( "recomputeOee( machine:$machine, start:$start, end:$end ){ machine }", {{"machine", machine}, {"start", start}, {"end", end}} ); }; };
		EXPECT_EQ( Status(recompute(999999, Iso(t0), Iso(t0+1h))), EHttpStatus::NotFound );
		EXPECT_NE( Refused(recompute(id, "yesterday", Iso(t0+1h))).find("start"), string::npos );
		EXPECT_NE( Refused(recompute(id, Iso(t0+1h), Iso(t0))).find("before end"), string::npos );
		EXPECT_NE( Refused(recompute(id, Iso(t0), Iso(t0+1h))).find("isn't running"), string::npos ) << "no signals yet";
		EXPECT_NE( Refused([&]{ pump.AddSignal( ERole::State, pump.Members.at(ERole::State), {{"role", "Speed"}} ); }).find("role"), string::npos );
		EXPECT_NE( Refused([&]{ pump.AddSignal( ERole::Reason ); }).find("neither"), string::npos ) << "a Reason alone is no machine";
		EXPECT_NE( Refused([&]{ pump.AddSignal( ERole::Total ); }).find("idleTimeout"), string::npos ) << "a count alone needs the timeout";
		EXPECT_NE( Refused([&]{ pump.AddSignal( ERole::State, pump.Members.at(ERole::State), {{"running", "[]"}} ); }).find("running"), string::npos );
		{
			let itemsBefore = Items();
			let other = CreateGroup( "oeeOtherGroup" );
			absl::Cleanup purge = [=]{ Purge( other, itemsBefore ); };
			let node = AddNode( other, Status2 );
			EXPECT_NE( Refused([&]{ pump.AddSignal( ERole::State, node ); }).find("not the machine's"), string::npos );
		}
		pump.Signals.emplace( ERole::State, pump.AddSignal(ERole::State) );
		EXPECT_NE( Refused([&]{ pump.AddSignal( ERole::State ); }).find("two State"), string::npos );
		pump.Signals.emplace( ERole::Total, pump.AddSignal(ERole::Total) );
		EXPECT_NE( Refused([&]{ pump.AddSignal( ERole::Rate, pump.Members.at(ERole::Reject) ); }).find("Total and a Rate"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "deleteOeeMachineSignal( id:$id )", {{"id", pump.Signals.at(ERole::Total)}} ); }).find("purge"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "updateOeeMachine( id:$id, width:$width )", {{"id", id}, {"width", 1}} ); }).find("width"), string::npos );

		for( sv command : {"createOeeStop( oeeMachineId:1, start:0 ){ id }", "createOeeInterval( oeeMachineId:1, start:0, ms:1, run:1, stop:0, unknown:0, total:0, good:0, reject:0 ){ id }", "updateOeeInterval( id:1, run:2 )", "purgeOeeStop( id:1 )", "createOeeCoding( oeeMachineId:1, start:0, finish:1, oeeReasonId:1, identityId:0, userName:\"x\" ){ id }", "deleteOeeCoding( id:1 )"} )
			EXPECT_NE( Refused([&]{ Mutate( command ); }).find("refused"), string::npos ) << command;

		EXPECT_NE( Refused([&]{ Mutate( "createOeeReason( name:\"r\", slug:\"r\" ){ id }" ); }).find("category"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "createOeeReason( name:\"r\", slug:\"r\", category:\"Sometimes\" ){ id }" ); }).find("category"), string::npos );
		let jam = HistGroupFixture::Id( Mutate("createOeeReason( name:\"Jam\", slug:\"oeeJam\", category:\"Unplanned\", code:7 ){ id }") );
		absl::Cleanup purgeJam = [=]{ try{ Mutate( "purgeOeeReason( id:$id )", {{"id", jam}} ); }catch( const std::exception& e ){ ADD_FAILURE() << e.what(); } };
		EXPECT_NE( Refused([&]{ Mutate( "createOeeReason( name:\"Jam2\", slug:\"oeeJam2\", category:\"Planned\", code:7 ){ id }" ); }).find("code 7"), string::npos );
		EXPECT_NO_THROW( Mutate("updateOeeReason( id:$id, code:7 )", {{"id", jam}}) ) << "its own code";
		Mutate( "deleteOeeReason( id:$id )", {{"id", jam}} );
		EXPECT_NE( Refused([&]{ Mutate( "createOeeReason( name:\"Jam3\", slug:\"oeeJam3\", category:\"Planned\", code:7 ){ id }" ); }).find("deleted"), string::npos ) << "a deleted reason keeps its code";
		Mutate( "restoreOeeReason( id:$id )", {{"id", jam}} );
		DS()->ExecuteSync( {Ƒ("insert into {}( oee_machine_id, start, finish, oee_reason_id, identity_id, user_name, created ) values( ?, ?, ?, ?, ?, ?, {} )", GetViewPtr(OeeTable::Codings)->SqlName(), DS()->Syntax().UtcNow()), {DB::Value{(uint)id}, OeeTicks(t0), OeeTicks(t0+1min), DB::Value{(uint)jam}, DB::Value{(uint)0}, DB::Value{"tests"}}} );
		EXPECT_NE( Refused([&]{ Mutate( "purgeOeeReason( id:$id )", {{"id", jam}} ); }).find("coding"), string::npos );
		DS()->ExecuteSync( {Ƒ("delete from {} where oee_machine_id=?", GetViewPtr(OeeTable::Codings)->SqlName()), {DB::Value{(uint)id}}} );

		EXPECT_NE( Refused([&]{ Mutate( "deleteHistGroup( id:$id )", {{"id", pump.Group}} ); }).find("purge the machine"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "deleteHistGroup( slug:$slug )", {{"slug", pump.Slug}} ); }).find("purge the machine"), string::npos ) << "keyed by its slug";
		EXPECT_NE( Refused([&]{ Mutate( "purgeHistGroup( id:$id )", {{"id", pump.Group}} ); }).find("purge the machine"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "purgeHistGroupNode( id:$id )", {{"id", pump.Members.at(ERole::Total)}} ); }).find("purge the signal"), string::npos );
		EXPECT_TRUE( HistHost::Instance().Find(pump.Group) ) << "the group is still in the historian";
		EXPECT_EQ( pump.GroupHost->Members().size(), 4u );
		Mutate( "deleteOeeMachine( id:$id )", {{"id", id}} );
		EXPECT_NE( Refused([&]{ Mutate( "purgeHistGroup( id:$id )", {{"id", pump.Group}} ); }).find("deleted"), string::npos ) << "a deleted machine holds its group too";
		Mutate( "restoreOeeMachine( id:$id )", {{"id", id}} );
		EXPECT_NO_THROW( Mutate("purgeHistGroupNode( id:$id )", {{"id", pump.Members.at(ERole::Reason)}}) ) << "no signal names it";
		pump.Members.erase( ERole::Reason );
	}

	//A shift as a backfill:  status and the counts written through the UA client with past source times, the machine made with `from` at
	//the shift's start, and the replay closes every interval at once - the stop with the written times and its code, each interval's
	//times and counts, the reset counted whole, and `processed` past them.
	TEST_F( OeeTests, AShiftAsABackfill ){
		let t0 = Window();
		Pump1Machine pump{ "oeeShift", t0-1min };
		WriteShift( t0 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), t0, 3 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::State), t0, 2 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Reason), t0, 2 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Reject), t0, 1 );
		let id = pump.Create( t0 );
		auto engine = WaitLive( id );
		let rows = Intervals( id );
		ExpectShift( rows, Stops(id), t0 );
		EXPECT_EQ( rows.front().Start, t0 ) << "the first row starts where the machine did";
		let processed = Processed( id );
		ASSERT_TRUE( processed );
		EXPECT_GE( *processed, t0+5min );
		EXPECT_EQ( *processed, rows.back().Start+milliseconds{rows.back().Ms} ) << "the end of the last row written";
		EXPECT_EQ( engine->Rebuilds(), 0u ) << "a plain shift asks for no rebuild";
	}

	//A restart from `processed`:  the host stopped and started reads the machine's rows again and double-counts nothing;  and with
	//`processed` set back to a boundary inside the shift, as a crash leaves it after rows were written past it, the restart
	//deletes those rows, clears the finish of the stop that spans it, and the replay writes them again - the same rows, once.
	TEST_F( OeeTests, ARestartFromProcessedDoubleCountsNothing ){
		let t0 = Window();
		Pump1Machine pump{ "oeeRestart", t0-1min };
		WriteShift( t0 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), t0, 3 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::State), t0, 2 );
		let id = pump.Create( t0 );
		WaitLive( id );
		let before = Intervals( id );
		let stopsBefore = Stops( id );
		ExpectShift( before, stopsBefore, t0 );

		Oee().Stop();
		Oee().Start( _clock );//the system clock's start:  no settle.
		WaitLive( id );
		let after = Intervals( id );
		ASSERT_EQ( after.size(), before.size() );
		for( uint i=0; i<after.size(); ++i ){
			EXPECT_EQ( after[i].Start, before[i].Start ) << i;
			EXPECT_EQ( after[i].Total, before[i].Total ) << i;
			EXPECT_EQ( after[i].Run, before[i].Run ) << i;
		}
		EXPECT_EQ( Stops(id).size(), 1u );

		//A crash after the rows of minutes 2, 3 and 4 were written, before `processed` moved:  the stop of 1:15 to 2:45 spans it.
		Oee().Stop();
		DS()->ExecuteSync( OeeSql::SetProcessed(id, t0+2min) );
		Oee().Start( _clock );
		WaitLive( id );
		let reconciled = Intervals( id );
		ASSERT_EQ( reconciled.size(), before.size() ) << "the rows past processed were deleted and written again, once";
		for( uint i=0; i<reconciled.size(); ++i ){
			EXPECT_EQ( reconciled[i].Start, before[i].Start ) << i;
			EXPECT_EQ( reconciled[i].Run, before[i].Run ) << i;
			EXPECT_EQ( reconciled[i].Stop, before[i].Stop ) << i;
			EXPECT_EQ( reconciled[i].Total, before[i].Total ) << i;
			EXPECT_EQ( reconciled[i].Reject, before[i].Reject ) << i;
		}
		let stops = Stops( id );
		ExpectShift( reconciled, stops, t0 );
		EXPECT_EQ( stops[0].Id, stopsBefore[0].Id ) << "the spanning stop's row was kept and ended again, not replaced";
	}

	//The clock's cases.  An interval's row is written once the clock passes its end by settle, not before;  with no State signal the
	//count is the state, and the idle timeout, found by the clock, opens a stop at the last count's time;  the stop's row is written
	//when it opens, and ended by the next count.  The timeout is ninety seconds, longer than the rest of any minute, so the row
	//holding the count is written before the clock finds the stop, and the stop begins in a written row.
	TEST_F( OeeTests, SettleAndTheIdleTimeoutRunOnTheClock ){
		let from = floor<seconds>( Now() )-5s;
		Pump1Machine pump{ "oeeClock", from-2s, true, 100, 0 };//a count two seconds before the opening:  running, its timeout still to run.
		let id = pump.Create( from, {ERole::Total}, {{"idleTimeout", 90000}} );
		auto engine = WaitLive( id );
		ASSERT_TRUE( engine->State() );
		EXPECT_EQ( engine->State()->State, Oee::EState::Running ) << "a count the bound holds runs it";
		let counted = Now();
		WriteTotal( 101, counted );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), counted, 1 );
		ASSERT_TRUE( WaitUntil([&]{ auto s = engine->State(); return s && s->LastCount && *s->LastCount==counted; }, 15s) );
		let end = floor<minutes>( counted )+1min;
		let rowStart = floor<minutes>( counted )<from ? from : floor<minutes>( counted );//the first row is ragged, from the opening.
		//The interval the count falls in is written settle after its end, and no sooner.
		let offset = Offset( engine );
		_clock->AdvanceTo( end+Settle()+offset-100ms );
		WaitIdle( engine );
		EXPECT_TRUE( std::ranges::none_of(Intervals(id), [&]( const OeeIntervalRow& r ){ return r.Start==rowStart; }) ) << "not yet";
		_clock->AdvanceTo( end+Settle()+offset+100ms );
		WaitIdle( engine );
		EXPECT_TRUE( std::ranges::any_of(Intervals(id), [&]( const OeeIntervalRow& r ){ return r.Start==rowStart && r.Total==1; }) ) << "the row and its count";
		//Idle:  ninety seconds after the count with no other, the clock finds the machine stopped since it.  The stop began in a
		//written row, so the engine asks for a rebuild over it, which on a machine whose count is its state is a restart from
		//that row's start:  the replay finds the stop again from the records' times, and the next engine runs on and writes the
		//row again with the stop time.
		_clock->AdvanceTo( counted+91s+offset );
		auto replaced = WaitReplaced( id, engine );
		EXPECT_EQ( engine->Rebuilds(), 1u ) << "the idle stop's";
		EXPECT_EQ( replaced->Rebuilds(), 0u ) << "the replay reproduces it and asks for nothing";
		let stops = Stops( id );
		ASSERT_EQ( stops.size(), 1u );
		EXPECT_EQ( stops[0].Start, counted ) << "the timeout is the detection delay, not stop time";
		EXPECT_FALSE( stops[0].End );
		EXPECT_FALSE( stops[0].Unknown );
		ASSERT_TRUE( replaced->State() );
		EXPECT_EQ( replaced->State()->State, Oee::EState::Stopped );
		EXPECT_EQ( replaced->State()->Since, counted );
		let rows = Intervals( id );
		let& row = Row( rows, rowStart );
		EXPECT_EQ( row.Total, 1 );
		EXPECT_EQ( row.Run, (uint32)duration_cast<milliseconds>(counted-rowStart).count() ) << "running until the count";
		EXPECT_EQ( row.Stop, (uint32)duration_cast<milliseconds>(end-counted).count() ) << "stopped from it";
		ExpectNoOverlap( rows );
		EXPECT_GE( *Processed(id), end );
	}

	//Unknown time over a lost connection on a count-only machine:  the collector's connection drops while a value is written, the
	//group marks the gap with Bad_DataLost before the value, and the engine writes the span as a row with `unknown` set, from the
	//marker to the value, and counts the value's delta across it.
	TEST_F( OeeTests, UnknownTimeOverALostConnection ){
		let now = Now();
		let t0 = floor<minutes>( now )-1min;
		Pump1Machine pump{ "oeeUnknown", t0-30s, true, 100, 0 };
		let id = pump.Create( t0, {ERole::Total}, {{"idleTimeout", 600000}} );
		auto engine = WaitLive( id );
		let t1 = Now();
		WriteTotal( 110, t1 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), t1, 1 );
		auto collector = Collector();
		ASSERT_TRUE( collector );
		let broke = Now();
		UAClient::ConnectionLost( sp<UAClient>{collector} );
		std::this_thread::sleep_for( 50ms );
		let missed = Now();
		WriteTotal( 125, missed );
		ASSERT_NO_THROW( WaitStored(pump.GroupHost, pump.Members.at(ERole::Total), t1, 3) ) << "the marker and the value";
		ASSERT_TRUE( WaitUntil([&]{ auto s = engine->State(); return s && s->LastCount && *s->LastCount==missed; }, 15s) );
		let stops = Stops( id );
		ASSERT_EQ( stops.size(), 1u );
		EXPECT_TRUE( stops[0].Unknown );
		EXPECT_GE( stops[0].Start, broke );
		EXPECT_LT( stops[0].Start, missed );
		ASSERT_TRUE( stops[0].End );
		EXPECT_EQ( *stops[0].End, missed );
		EXPECT_EQ( engine->State()->State, Oee::EState::Running );
		//The interval the value falls in, once written:  its count is the delta across the gap, and its unknown time the span's.
		let end = floor<minutes>( missed )+1min;
		_clock->AdvanceTo( std::max(end, floor<minutes>(t1)+1min)+Settle()+Offset(engine)+100ms );
		WaitIdle( engine );
		let rows = Intervals( id );
		double total{}; uint32 unknown{};
		for( let& r : rows ){
			total += r.Total;
			unknown += r.Unknown;
		}
		EXPECT_EQ( total, 25 ) << "10 in the first write, 15 across the gap";
		let span = duration_cast<milliseconds>( *stops[0].End-stops[0].Start ).count();
		EXPECT_GE( unknown+1, (uint32)span );
		EXPECT_LE( unknown, (uint32)span+1 );
	}

	//A signal's create answers the warnings the page shows:  a counter whose member has a deviation band, and a member whose
	//MinTimeInterval is past settle.  A width change reaches the running engine without a restart;  a deleted machine's engine
	//ends and its rows stay, and its restore starts it again.
	TEST_F( OeeTests, WarningsTheWidthAndADelete ){
		let t0 = Window();
		Pump1Machine pump{ "oeeWarn", t0-1min };
		let id = pump.CreateMachine( t0 );
		pump.Signals.emplace( ERole::State, pump.AddSignal(ERole::State) );
		Mutate( "updateHistGroupNode( id:$id, exceptionDeviation:2, exceptionDeviationFormat:0, minTimeInterval:5000 )", {{"id", pump.Members.at(ERole::Total)}} );
		let result = Mutate( "createOeeMachineSignal( oeeMachineId:$machine, role:\"Total\", histGroupNodeId:$node ){ id warnings }", {{"machine", id}, {"node", pump.Members.at(ERole::Total)}} );
		let& row = Json::AsObject( result );
		let warnings = row.if_contains( "warnings" );
		ASSERT_TRUE( warnings && warnings->is_array() ) << serialize( result );
		EXPECT_EQ( warnings->get_array().size(), 2u ) << serialize( *warnings );
		pump.Signals.emplace( ERole::Total, HistGroupFixture::Id(result) );
		_clock->Advance( Settle() );
		auto engine = WaitLive( id );
		Mutate( "updateOeeMachine( id:$id, width:300000 )", {{"id", id}} );
		EXPECT_EQ( Oee().Engine(id), engine ) << "a width change is no restart";
		Mutate( "updateOeeMachine( id:$id, smallStop:120000 )", {{"id", id}} );
		EXPECT_FALSE( Oee().Engine(id) ) << "an engine input changed:  the engine ends, and another starts settle on";
		_clock->Advance( Settle() );
		auto restarted = WaitLive( id );
		EXPECT_NE( restarted, engine );
		let rows = Intervals( id ).size();
		Mutate( "deleteOeeMachine( id:$id )", {{"id", id}} );
		EXPECT_FALSE( Oee().Engine(id) );
		EXPECT_EQ( Intervals(id).size(), rows ) << "a deletion keeps its rows";
		Mutate( "restoreOeeMachine( id:$id )", {{"id", id}} );
		_clock->Advance( Settle() );
		EXPECT_TRUE( WaitLive(id) );
	}

	//A history edit reaches the report:  updateHistory replaces the status record that began the shift's stop with running, the
	//Modification lands in written rows, and the engine asks for a rebuild over them - to the status's next record - which the
	//host runs in place:  the two rows are made again from the group's history, the stop is gone, the other rows and the engine stand.
	TEST_F( OeeTests, AHistoryEditRebuildsTheInterval ){
		let t0 = Window();
		Pump1Machine pump{ "oeeEdit", t0-1min };
		WriteShift( t0 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), t0, 3 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::State), t0, 2 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Reason), t0, 2 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Reject), t0, 1 );
		let id = pump.Create( t0 );
		auto engine = WaitLive( id );
		let before = Intervals( id );
		ExpectShift( before, Stops(id), t0 );
		let processed = Processed( id );

		auto open = EnforcedHistGroups();//a history edit needs a grant on an enforced resource.
		EXPECT_EQ( Update("updateHistory", pump.Group, {{Status1, t0+75s, true}}).Values, vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED} );
		WaitRebuilt( engine );
		EXPECT_EQ( Oee().Engine(id), engine ) << "rebuilt in place:  the span ends at the status's next record, inside the written rows";
		let rows = Intervals( id );
		ASSERT_EQ( rows.size(), before.size() );
		ExpectRow( rows, t0, 60000, 0, 10, 10, 0 );
		ExpectRow( rows, t0+1min, 60000, 0, 0, 0, 0 );
		ExpectRow( rows, t0+2min, 60000, 0, 0, 0, 0 );
		ExpectRow( rows, t0+3min, 60000, 0, 20, 18, 2 );
		ExpectRow( rows, t0+4min, 60000, 0, 5, 5, 0 );
		EXPECT_TRUE( Stops(id).empty() ) << "the stop's record was edited away";
		ExpectNoOverlap( rows );
		EXPECT_EQ( Processed(id), processed ) << "a rebuild in place moves nothing";
	}

	//A late counter record:  a total written with a source time inside the shift's first row, after the rows are written.  The
	//rebuild runs to the counter's next record, over four rows, with the stop between deleted and made again:  the first row gains
	//the five units and the fourth loses them, since its delta is now against the late value.
	TEST_F( OeeTests, ALateCounterRecordRebuildsItsRows ){
		let t0 = Window();
		Pump1Machine pump{ "oeeLate", t0-1min };
		WriteShift( t0 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), t0, 3 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::State), t0, 2 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Reason), t0, 2 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Reject), t0, 1 );
		let id = pump.Create( t0 );
		auto engine = WaitLive( id );
		let before = Intervals( id );
		let stopsBefore = Stops( id );
		ExpectShift( before, stopsBefore, t0 );

		WriteTotal( 115, t0+40s );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), t0, 4 );
		WaitRebuilt( engine );
		EXPECT_EQ( Oee().Engine(id), engine );
		let rows = Intervals( id );
		ASSERT_EQ( rows.size(), before.size() );
		ExpectRow( rows, t0, 60000, 0, 15, 15, 0 );
		ExpectRow( rows, t0+1min, 15000, 45000, 0, 0, 0 );
		ExpectRow( rows, t0+2min, 15000, 45000, 0, 0, 0 );
		ExpectRow( rows, t0+3min, 60000, 0, 15, 13, 2 );
		ExpectRow( rows, t0+4min, 60000, 0, 5, 5, 0 );
		let stops = Stops( id );
		ASSERT_EQ( stops.size(), 1u );
		EXPECT_EQ( stops[0].Start, stopsBefore[0].Start );
		EXPECT_EQ( stops[0].End, stopsBefore[0].End );
		EXPECT_EQ( stops[0].Code, optional<int64_t>{7} );
		ExpectNoOverlap( rows );
	}

	//A Bad_DataLost marker in a written row:  on a count-only machine the collector's connection drops after the row holding the
	//last count is written, so the marker the group stores falls before `processed`.  The rebuild it asks for is a restart from that
	//row's start - the count is the state - and the next engine writes the unknown span from the marker to the value after it, and
	//counts that value's delta across the gap.
	TEST_F( OeeTests, AMarkerInAWrittenRowRebuilds ){
		EarlyInTheMinute();
		let now = Now();
		let t0 = floor<minutes>( now )-1min;
		Pump1Machine pump{ "oeeMarker", t0-30s, true, 100, 0 };
		let id = pump.Create( t0, {ERole::Total}, {{"idleTimeout", 600000}} );
		auto engine = WaitLive( id );
		let t1 = Now();
		WriteTotal( 110, t1 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), t1, 1 );
		ASSERT_TRUE( WaitUntil([&]{ auto s = engine->State(); return s && s->LastCount && *s->LastCount==t1; }, 15s) );
		let end = floor<minutes>( t1 )+1min;
		_clock->AdvanceTo( end+Settle()+Offset(engine)+100ms );
		WaitIdle( engine );
		ASSERT_EQ( Processed(id), optional<TimePoint>{end} ) << "the row holding the count is written";

		auto collector = Collector();
		ASSERT_TRUE( collector );
		let broke = Ms( Clock::now() );
		UAClient::ConnectionLost( sp<UAClient>{collector} );
		std::this_thread::sleep_for( 50ms );
		let missed = Ms( Clock::now() );
		WriteTotal( 125, missed );
		ASSERT_NO_THROW( WaitStored(pump.GroupHost, pump.Members.at(ERole::Total), t1, 3) ) << "the marker and the value";
		ASSERT_LT( missed, end ) << "the gap falls inside the written row";
		auto replaced = WaitReplaced( id, engine );
		EXPECT_EQ( engine->Rebuilds(), 1u );
		ASSERT_TRUE( WaitUntil([&]{ auto s = replaced->State(); return s && s->LastCount && *s->LastCount==missed; }, 15s) );
		let stops = Stops( id );
		ASSERT_EQ( stops.size(), 1u );
		EXPECT_TRUE( stops[0].Unknown );
		EXPECT_GE( stops[0].Start, broke );
		EXPECT_LT( stops[0].Start, missed );
		ASSERT_TRUE( stops[0].End );
		EXPECT_EQ( *stops[0].End, missed );
		EXPECT_EQ( replaced->State()->State, Oee::EState::Running );
		WaitIdle( replaced );
		let rows = Intervals( id );
		double total{}; uint32 unknown{};
		for( let& r : rows ){
			total += r.Total;
			unknown += r.Unknown;
		}
		EXPECT_EQ( total, 25 ) << "10 in the first write, 15 across the gap";
		let span = duration_cast<milliseconds>( *stops[0].End-stops[0].Start ).count();
		EXPECT_GE( unknown+1, (uint32)span );
		EXPECT_LE( unknown, (uint32)span+1 );
		ExpectNoOverlap( rows );
	}

	//The width changed to five minutes mid-shift:  the change takes effect at the next five-minute boundary after the open interval,
	//the rows before it keep the minute, and a recompute across the change makes the same rows again, each at the width it had, with no
	//overlap between them.
	TEST_F( OeeTests, TheWidthChangedMidShiftAndARebuildAcrossIt ){
		let t0 = Window();
		Pump1Machine pump{ "oeeWidth", t0-1min };
		WriteShift( t0 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), t0, 3 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::State), t0, 2 );
		let id = pump.Create( t0 );
		auto engine = WaitLive( id );
		ASSERT_TRUE( engine->State() );
		//The change lands at the first five-minute boundary at or after the open interval's end.
		auto boundary = floor<minutes>( engine->State()->At )+1min;
		while( boundary.time_since_epoch()%5min!=Duration::zero() )
			boundary += 1min;
		Mutate( "updateOeeMachine( id:$id, width:300000 )", {{"id", id}} );
		ASSERT_EQ( Oee().Engine(id), engine );
		_clock->AdvanceTo( boundary+5min+Settle()+Offset(engine)+100ms );
		WaitIdle( engine );
		let mixed = Intervals( id );
		let& wide = Row( mixed, boundary );
		EXPECT_EQ( wide.Ms, 300000u );
		EXPECT_EQ( Row(mixed, boundary-1min).Ms, 60000u );
		ExpectNoOverlap( mixed );
		EXPECT_EQ( Processed(id), optional<TimePoint>{boundary+5min} );

		Mutate( "recomputeOee( machine:$id, start:$start, end:$end ){ machine }", {{"id", id}, {"start", Iso(boundary-2min)}, {"end", Iso(boundary+1min)}} );
		WaitRebuilt( engine );
		EXPECT_EQ( Oee().Engine(id), engine ) << "in place:  the span ends inside the written rows, on running time";
		let rows = Intervals( id );
		ExpectSame( rows, mixed );
		ExpectNoOverlap( rows );
		EXPECT_EQ( Processed(id), optional<TimePoint>{boundary+5min} );
	}

	//recomputeOee after a `running` change:  the signal's update says a recompute is due, the engine starts again from `processed`
	//with the new list, leaving the rows before as they were, and the recompute makes them again - here a restart from the span's
	//start, since under the new list the machine is stopped to this day, and a rebuild can't end inside a stop the rows didn't have.
	//The coding over the shift's stop is untouched.
	TEST_F( OeeTests, RecomputeAfterARunningChange ){
		let t0 = Window();
		Pump1Machine pump{ "oeeRunning", t0-1min };
		WriteShift( t0 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Total), t0, 3 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::State), t0, 2 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Reason), t0, 2 );
		WaitStored( pump.GroupHost, pump.Members.at(ERole::Reject), t0, 1 );
		let id = pump.Create( t0 );
		auto engine = WaitLive( id );
		let before = Intervals( id );
		ExpectShift( before, Stops(id), t0 );
		let jam = HistGroupFixture::Id( Mutate("createOeeReason( name:\"Jam\", slug:\"oeeRunningJam\", category:\"Unplanned\" ){ id }") );
		absl::Cleanup purgeJam = [=]{ try{ DS()->ExecuteSync( {Ƒ("delete from {} where oee_reason_id=?", GetViewPtr(OeeTable::Codings)->SqlName()), {DB::Value{(uint)jam}}} ); Mutate( "purgeOeeReason( id:$id )", {{"id", jam}} ); }catch( const std::exception& e ){ ADD_FAILURE() << e.what(); } };
		DS()->ExecuteSync( {Ƒ("insert into {}( oee_machine_id, start, finish, oee_reason_id, identity_id, user_name, created ) values( ?, ?, ?, ?, ?, ?, {} )", GetViewPtr(OeeTable::Codings)->SqlName(), DS()->Syntax().UtcNow()), {DB::Value{(uint)id}, OeeTicks(t0+75s), OeeTicks(t0+165s), DB::Value{(uint)jam}, DB::Value{(uint)0}, DB::Value{"tests"}}} );
		let codings = [&]{ return DS()->Select({Ƒ("select start, finish from {} where oee_machine_id=?", GetViewPtr(OeeTable::Codings)->SqlName()), {DB::Value{(uint)id}}}).size(); };
		ASSERT_EQ( codings(), 1u );

		let result = Mutate( "updateOeeMachineSignal( id:$id, running:$running ){ recompute warnings }", {{"id", pump.Signals.at(ERole::State)}, {"running", "[false]"}} );
		let& row = Json::AsObject( result );
		EXPECT_EQ( row.if_contains("recompute") && row.at("recompute").is_bool() && row.at("recompute").get_bool(), true ) << serialize( result );
		ASSERT_TRUE( row.if_contains("warnings") && row.at("warnings").is_array() ) << serialize( result );
		EXPECT_NE( serialize(row.at("warnings")).find("recomputeOee"), string::npos ) << serialize( result );
		EXPECT_FALSE( Oee().Engine(id) ) << "an engine input changed";
		_clock->Advance( Settle() );
		auto restarted = WaitLive( id );
		EXPECT_NE( restarted, engine );
		ExpectSame( Intervals(id), before );//from `processed`:  the rows before stand.
		EXPECT_EQ( Stops(id).size(), 2u ) << "the shift's, and one open from `processed`, where true now reads stopped";

		Mutate( "recomputeOee( machine:$id, start:$start, end:$end ){ machine start end }", {{"id", id}, {"start", Iso(t0)}, {"end", Iso(t0+5min)}} );
		auto replaced = WaitReplaced( id, restarted );
		EXPECT_EQ( restarted->Rebuilds(), 1u );
		let rows = Intervals( id );
		ASSERT_EQ( rows.size(), before.size() );
		ExpectRow( rows, t0, 0, 60000, 10, 10, 0 );
		ExpectRow( rows, t0+1min, 45000, 15000, 0, 0, 0 );
		ExpectRow( rows, t0+2min, 45000, 15000, 0, 0, 0 );
		ExpectRow( rows, t0+3min, 0, 60000, 20, 18, 2 );
		ExpectRow( rows, t0+4min, 0, 60000, 5, 5, 0 );
		ExpectNoOverlap( rows );
		let stops = Stops( id );
		ASSERT_EQ( stops.size(), 2u );
		EXPECT_EQ( stops[0].Start, t0 );
		EXPECT_EQ( stops[0].End, optional<TimePoint>{t0+75s} );
		EXPECT_EQ( stops[0].Code, optional<int64_t>{7} ) << "the reason set just before the stop's end";
		EXPECT_EQ( stops[1].Start, t0+165s );
		EXPECT_FALSE( stops[1].End );
		EXPECT_EQ( stops[1].Code, optional<int64_t>{7} );
		EXPECT_EQ( codings(), 1u ) << "a recompute never touches a coding";
		EXPECT_EQ( replaced->State()->State, Oee::EState::Stopped );
	}
}