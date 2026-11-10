#pragma once
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/settings.h>
#include <jde/ql/ql.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/calendar/client/Calendars.h>
#include "HistGroupFixture.h"
#include "../../src/oee/OeeHost.h"
#include "../../../../libs/historian/tests/ManualClock.h"

//OeeTests' fixture, and OeeSubscribeTests':  the host on a manual clock each test makes anew, pump1's group and the machine over
//it, a shift's records, a calendar on the AppServer and the tables read back.
namespace Jde::Opc::Gateway::Tests{
	using Hist::Tests::ManualClock;
	using Oee::ERole;
	struct OeeIntervalRow final{ TimePoint Start; uint32 Ms, Run, Stop, Unknown; double Total, Good, Reject; };

	struct OeeFixture : HistGroupFixture{
	protected:
		Ω SetUpTestSuite()ε->void{
			HistGroupFixture::SetUpTestSuite();
			_base = floor<minutes>( Clock::now() )-4h;//the backfills' windows, each case's later than the last's and every one an hour or more in the past.
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

		//A calendar on the AppServer, in UTC, its one shift every day from `start`'s minute for `length`, once the gateway's client
		//holds it;  one an earlier run left is purged first.  `breaks`:  the shift's, each { start, minutes } from its start.
		Ω ShiftCalendar( sv slug, TimePoint start, uint length, jarray breaks={} )ε->uint32{
			namespace Calendars = Calendar::Client::Calendars;
			for( let& v : AppClient()->QuerySync<jarray>(Ƒ(R"(calendars( slug:"{}" ){{ id }})", slug), {}) )
				PurgeCalendar( QL::AsId<uint32>(v) );
			let id = QL::AsId<uint32>( AppClient()->QuerySync<jobject>(Ƒ(R"(createCalendar( name:"{0}", slug:"{0}", timeZone:"UTC", dayStart:0 ){{ id }})", slug), {}) );
			sp<const Calendar::Schedule> held;
			THROW_IF( !WaitUntil([&]{ held = Calendars::Find( id ); return held && held->Patterns().size()==1; }, 15s), "The gateway never held calendar {}.", id );
			let ofDay = duration_cast<minutes>( start-floor<days>(start) ).count();
			AppClient()->QuerySync<jobject>( Ƒ(R"(createCalendarShift( calendarPatternId:{}, name:"Written", days:127, start:{}, minutes:{}{} ){{ id }})", held->Patterns().front().Id, ofDay, length, breaks.size() ? Ƒ(", breaks:{}", serialize(breaks)) : string{}), {} );
			THROW_IF( !WaitUntil([&]{ held = Calendars::Find( id ); return held && held->Patterns().size()==1 && held->Patterns().front().Shifts.size()==1; }, 15s), "The gateway never held calendar {}'s shift.", id );
			return id;
		}
		Ω PurgeCalendar( uint32 id )ι->void{
			try{
				AppClient()->QuerySync<jvalue>( Ƒ("purgeCalendar( id:{} )", id), {} );
			}
			catch( const std::exception& e ){
				ADD_FAILURE() << "purgeCalendar(" << id << "):  " << e.what();
			}
		}
		Ω Number( const jobject& o, sv key )ε->double{ return Json::AsNumber<double>( o, key ); }
		Ω At( const jobject& o, sv key )ε->TimePoint{ return Chrono::ToTimePoint( string{Json::AsSV(o, key)} ); }
		//A reason with no code, purged as the returned cleanup goes, its codings first.
		Ω Reason( sv slug, sv category )ε->uint32{
			return HistGroupFixture::Id( Mutate("createOeeReason( name:$name, slug:$slug, category:$category ){ id }", {{"name", slug}, {"slug", slug}, {"category", category}}) );
		}
		Ω PurgeReason( uint32 id )ι->void{
			try{
				DS()->ExecuteSync( {Ƒ("delete from {} where oee_reason_id=?", GetViewPtr(OeeTable::Codings)->SqlName()), {DB::Value{(uint)id}}} );
				Mutate( "purgeOeeReason( id:$id )", {{"id", id}} );
			}
			catch( const std::exception& e ){
				ADD_FAILURE() << "purgeOeeReason(" << id << "):  " << e.what();
			}
		}

		inline static sp<ManualClock> _clock;
		inline static TimePoint _base;
		inline static uint _windows{};
	};
}