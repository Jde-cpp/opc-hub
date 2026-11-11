//OEE phase 8 (#408):  lines (docs/oee/spec.md *Lines*, *Reads*, *Live*, *Authorization*) - the two tables and their rules, a
//line over two machines, pump1's and pump2's, read against figures computed by hand, an area over the line, the rights over
//every machine reached, and OeeSubscribe{ line }, whose status is computed once per change for every subscription of the line.
#include "utils/OeeFixture.h"
#include "utils/GatewayClientSocket.h"
#include "../src/oee/OeeLineFeed.h"

namespace Jde::Opc::Gateway::Tests{
	using Web::Client::ClientSocketAwait;
	//What the socket delivered for one line's subscription after its ack, in order.
	struct LineHeard final : IOeeListener{
		α OnStatus( const FromServer::OeeStatus& status )ι->void override{ lg _{ _mutex }; _statuses.push_back( status ); }
		α OnError( string what )ι->void override{ lg _{ _mutex }; _errors.push_back( move(what) ); }
		α Count()Ι->uint{ lg _{ _mutex }; return _statuses.size(); }
		α Last()Ι->FromServer::OeeStatus{ lg _{ _mutex }; return _statuses.empty() ? FromServer::OeeStatus{} : _statuses.back(); }
		α Errors()Ι->vector<string>{ lg _{ _mutex }; return _errors; }
	private:
		mutable std::mutex _mutex;
		vector<FromServer::OeeStatus> _statuses;
		vector<string> _errors;
	};

	struct OeeLinesTests : OeeFixture{
		//The lines a test makes, purged as it goes, the last made first:  a line's purge takes its memberships.
		struct Lines final{
			~Lines(){
				for( let id : Ids | std::views::reverse ){
					try{
						Mutate( "purgeOeeLine( id:$id )", {{"id", id}} );
					}
					catch( const std::exception& e ){
						ADD_FAILURE() << "purgeOeeLine(" << id << "):  " << e.what();
					}
				}
			}
			α Create( sv slug, sv level, jobject args={} )ε->uint32{
				string ql{ "createOeeLine( name:$name, slug:$slug, level:$level" };
				jobject vars{ {"name", slug}, {"slug", slug}, {"level", level} };
				for( let& [key,value] : args ){
					ql += Ƒ( ", {}:${}", key, key );
					vars[key] = value;
				}
				Ids.push_back( HistGroupFixture::Id(Mutate(ql+" ){ id }", move(vars))) );
				return Ids.back();
			}
			vector<uint32> Ids;
		};
		Ω AddMachine( uint32 line, uint32 machine, uint32 sort=0 )ε->uint32{
			return Id( Mutate("createOeeLineMember( oeeLineId:$line, oeeMachineId:$machine, sort:$sort ){ id }", {{"line", line}, {"machine", machine}, {"sort", sort}}) );
		}
		Ω AddLine( uint32 line, uint32 member, uint32 sort=0 )ε->uint32{
			return Id( Mutate("createOeeLineMember( oeeLineId:$line, memberLineId:$member, sort:$sort ){ id }", {{"line", line}, {"member", member}, {"sort", sort}}) );
		}
		Ω Memberships( uint32 line )ε->uint{
			let rows = DS()->Select( {Ƒ("select count(*) from {} where oee_line_id=? or member_line_id=?", GetViewPtr(OeeTable::LineMembers)->SqlName()), {DB::Value{(uint)line}, DB::Value{(uint)line}}} );
			return rows.front().Get<uint>( 0 );
		}
		Ω Subscribe( uint32 line, sp<LineHeard> heard )ε->FromServer::OeeStatus{
			return BlockAwait<ClientSocketAwait<FromServer::OeeStatus>,FromServer::OeeStatus>( Socket().OeeSubscribeLine(line, move(heard)) );
		}
		Ω Unsubscribe( RequestId subscription )ε->bool{ return BlockAwait<ClientSocketAwait<bool>,bool>( Socket().OeeUnsubscribe(subscription) ); }
		Ω Time( const google::protobuf::Timestamp& t )ι->TimePoint{ return UADateTime{ t }.Time(); }
	};

	//The two tables synced, and each refusal of the rules a BadRequest:  a level that is none of the four, a member that is both
	//or neither, one already there, a line that would reach itself, a machine reached twice, a member line above its parent's
	//level, by its create and by either's level edited, a calendar the gateway doesn't hold, and a member deleted.  A line's
	//purge takes its memberships both ways, and a machine's its own.
	TEST_F( OeeLinesTests, TheRulesOfLinesAndTheirMembers ){
		for( sv table : {"oeeLines", "oeeLineMembers"} )
			EXPECT_NO_THROW( Mutate(Ƒ("{}{{ id }}", table)) ) << table;
		let from = floor<seconds>( Now() )-5s;
		Pump1Machine pump1{ "oeeLineRules1", from-2s };
		Pump1Machine pump2{ "oeeLineRules2", from-2s, true, 100, 5, Pump2Nodes };
		let m1 = pump1.Create( from, {} );
		let m2 = pump2.Create( from, {} );
		Lines lines;
		EXPECT_NE( Refused([&]{ lines.Create( "oeeRulesPlant", "Plant" ); }).find("Site, Area, Line or Cell"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "createOeeLine( name:$name, slug:$name ){ id }", {{"name", "oeeRulesNone"}} ); }).find("level"), string::npos );
		EXPECT_NE( Refused([&]{ lines.Create( "oeeRulesCalendar", "Line", {{"calendarId", 999'999}} ); }).find("calendar"), string::npos );
		let area = lines.Create( "oeeRulesArea", "Area" );
		let a = lines.Create( "oeeRulesA", "Line" );
		let b = lines.Create( "oeeRulesB", "Line" );
		EXPECT_TRUE( lines.Ids.size()==3 ) << "a refused create made no line";

		EXPECT_NE( Refused([&]{ Mutate( "createOeeLineMember( oeeLineId:$line ){ id }", {{"line", a}} ); }).find("one of the two"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "createOeeLineMember( oeeLineId:$line, oeeMachineId:$machine, memberLineId:$member ){ id }", {{"line", a}, {"machine", m1}, {"member", b}} ); }).find("one of the two"), string::npos );
		EXPECT_EQ( Status([&]{ AddMachine( a, 999'999 ); }), EHttpStatus::NotFound );
		EXPECT_EQ( Status([&]{ AddLine( a, 999'999 ); }), EHttpStatus::NotFound );
		EXPECT_EQ( Status([&]{ AddMachine( 999'999, m1 ); }), EHttpStatus::NotFound );

		let first = AddMachine( a, m1, 1 );
		EXPECT_NE( Refused([&]{ AddMachine( a, m1, 2 ); }).find("already"), string::npos );
		EXPECT_NE( Refused([&]{ AddLine( a, a ); }).find("reach itself"), string::npos );
		let nested = AddLine( a, b, 2 );//a line of lines.
		EXPECT_NE( Refused([&]{ AddLine( b, a ); }).find("reach itself"), string::npos ) << "a cycle";
		EXPECT_NE( Refused([&]{ AddMachine( b, m1 ); }).find("two paths"), string::npos ) << "a machine reached twice";
		AddMachine( b, m2 );
		EXPECT_NE( Refused([&]{ AddLine( a, area ); }).find("no higher"), string::npos ) << "an area in a line";
		AddLine( area, a );
		EXPECT_NE( Refused([&]{ Mutate( "updateOeeLine( id:$id, level:$level )", {{"id", a}, {"level", "Site"}} ); }).find("no higher"), string::npos ) << "above the area it is in";
		EXPECT_NE( Refused([&]{ Mutate( "updateOeeLine( id:$id, level:$level )", {{"id", a}, {"level", "Cell"}} ); }).find("no higher"), string::npos ) << "below the line in it";
		EXPECT_NO_THROW( Mutate("updateOeeLine( id:$id, level:$level )", {{"id", area}, {"level", "Site"}}) );
		EXPECT_NE( Refused([&]{ Mutate( "updateOeeLine( id:$id, name:$name )", {{"id", a}, {"name", ""}} ); }).find("name is required"), string::npos ) << "an update's name, as a create's";
		EXPECT_NE( Refused([&]{ Mutate( "deleteOeeLineMember( id:$id )", {{"id", first}} ); }).find("purge"), string::npos );
		EXPECT_NO_THROW( Mutate("updateOeeLineMember( id:$id, sort:$sort )", {{"id", first}, {"sort", 5}}) );

		{//the host's graph is the tables':  the area reaches both machines, b's first now that a's own sorts after its line.
			let reached = Oee().Reach( area );
			ASSERT_TRUE( reached );
			ASSERT_EQ( reached->Machines.size(), 2u );
			EXPECT_EQ( reached->Machines[0].Row.Id, m2 );
			EXPECT_EQ( reached->Machines[1].Row.Id, m1 );
		}
		EXPECT_NO_THROW( Mutate("purgeOeeLineMember( id:$id )", {{"id", nested}}) );
		EXPECT_EQ( Oee().Reach(area)->Machines.size(), 1u );
		AddLine( a, b );

		//A machine's purge takes its memberships, and a line's its own, as a parent and as a member.
		Mutate( "purgeOeeMachine( id:$id )", {{"id", m2}} );
		pump2.Id = 0;
		EXPECT_EQ( Memberships(b), 1u ) << "a's membership of it alone";
		EXPECT_EQ( Memberships(a), 3u );
		Mutate( "purgeOeeLine( id:$id )", {{"id", a}} );
		std::erase( lines.Ids, a );
		EXPECT_EQ( Memberships(a), 0u );
		EXPECT_EQ( Memberships(b), 0u );
		EXPECT_EQ( Memberships(area), 0u );
		EXPECT_FALSE( Oee().Reach(a) );
		EXPECT_TRUE( Oee().Reach(area)->Machines.empty() );

		//A line that reaches no machine has no group to gate it:  the lines' own table does.
		constexpr sv statusQL{ "oeeLineStatus( line:$id ){ line{ name } calendarId }" };
		EXPECT_NO_THROW( Mutate(statusQL, {{"id", area}}) ) << "the table unenforced, as it ships";
		EnforceResource( "gateway", "oeeLines", true );
		absl::Cleanup open = []{ try{ EnforceResource( "gateway", "oeeLines", false ); }catch( const std::exception& e ){ ADD_FAILURE() << "oeeLines wasn't opened again:  " << e.what(); } };
		EXPECT_EQ( Status([&]{ Mutate( statusQL, {{"id", area}} ); }), EHttpStatus::Forbidden );
		EXPECT_EQ( Status([&]{ Mutate( "oee( line:$id, start:$start, end:$end ){ oee }", {{"id", area}, {"start", Iso(from)}, {"end", Iso(from+1min)}} ); }), EHttpStatus::Forbidden );
	}

	//Two machines' shifts as backfills, pump1's of OeeTests and pump2 stopped two of its five minutes, under one calendar, and a
	//line over the two:  its bucket their components summed, each factor over the machines that measure it, its members' buckets,
	//its stops, losses and timeline over both, its status, an area over the line, and the refusals - neither or both of machine
	//and line, a line with no calendar asked by shift, a line that isn't there, and a machine the caller may not read.
	TEST_F( OeeLinesTests, ALineOverTwoPumpsAgainstHandFigures ){
		let t0 = Window();
		Pump1Machine pump1{ "oeeLine1", t0-1min };
		Pump1Machine pump2{ "oeeLine2", t0-1min, true, 200, 0, Pump2Nodes };
		WriteShift( t0 );
		WriteCount( Total2, 220, t0+30s );
		WriteBool( Status2, false, t0+60s );
		WriteBool( Status2, true, t0+180s );
		WriteCount( Total2, 230, t0+200s );
		WaitStored( pump1.GroupHost, pump1.Members.at(ERole::Total), t0, 3 );
		WaitStored( pump1.GroupHost, pump1.Members.at(ERole::State), t0, 2 );
		WaitStored( pump1.GroupHost, pump1.Members.at(ERole::Reject), t0, 1 );
		WaitStored( pump2.GroupHost, pump2.Members.at(ERole::Total), t0, 2 );
		WaitStored( pump2.GroupHost, pump2.Members.at(ERole::State), t0, 2 );
		let calendar = ShiftCalendar( "oeeLine", t0, 5 );
		absl::Cleanup purgeCalendar = [=]{ PurgeCalendar( calendar ); };
		let m1 = pump1.Create( t0, {ERole::State, ERole::Total, ERole::Reject, ERole::Reason}, {{"smallStop", 60000}, {"idealCycle", 3000.0}, {"calendarId", calendar}} );
		let m2 = pump2.Create( t0, {ERole::State, ERole::Total}, {{"smallStop", 60000}, {"idealCycle", 6000.0}, {"calendarId", calendar}} );
		WaitLive( m1 );
		WaitLive( m2 );
		Lines lines;
		let line = lines.Create( "oeeLine", "Line", {{"calendarId", calendar}} );
		AddMachine( line, m1, 1 );
		AddMachine( line, m2, 2 );
		let area = lines.Create( "oeeLineArea", "Area" );
		AddLine( area, line );

		let read = [&]( sv ql, uint32 id, jobject more={} ){
			jobject vars{ {"id", id}, {"start", Iso(t0)}, {"end", Iso(t0+8min)} };
			for( let& [key,value] : more )
				vars[key] = value;
			return Mutate( ql, move(vars) );
		};
		constexpr sv figuresQL{ "oee( line:$id, start:$start, end:$end, by:$by, members:$members ){ start end shift planned run unplanned total ideal availability performance quality oee measured partial machines coverage{ performance quality } members{ shift planned run oee machines machine{ id name } line{ id name level } } }" };
		//pump1:  planned 300 s, a 90 s stop, 35 units of which 33 good at 3 s.  pump2:  a 120 s stop, 30 units at 6 s, no quality.
		let availability = 390.0/600, performance = ( 105.0+180 )/390, quality = 33.0/35;
		{
			let shifts = read( figuresQL, line, {{"by", "shift"}, {"members", true}} ).as_array();
			ASSERT_EQ( shifts.size(), 1u ) << serialize( shifts );
			let& b = shifts[0].as_object();
			EXPECT_EQ( Json::AsSV(b, "shift"), "Written" ) << "the line's own calendar's";
			EXPECT_EQ( At(b, "start"), t0 );
			EXPECT_EQ( At(b, "end"), t0+5min );
			EXPECT_EQ( Number(b, "planned"), 600000 );
			EXPECT_EQ( Number(b, "unplanned"), 210000 );
			EXPECT_EQ( Number(b, "run"), 390000 );
			EXPECT_EQ( Number(b, "total"), 65 ) << "summed as they are";
			EXPECT_EQ( Number(b, "ideal"), 285000 );
			EXPECT_NEAR( Number(b, "availability"), availability, 1e-9 ) << "across both";
			EXPECT_NEAR( Number(b, "performance"), performance, 1e-9 ) << "ideal over run, across both";
			EXPECT_NEAR( Number(b, "quality"), quality, 1e-9 ) << "pump1's alone, which measures it";
			EXPECT_NEAR( Number(b, "oee"), availability*performance*quality, 1e-9 ) << "the product of the three as computed";
			EXPECT_EQ( b.at("measured"), (jarray{"availability", "performance"}) ) << "what every machine measured";
			EXPECT_FALSE( Json::AsBool(b, "partial") );
			EXPECT_EQ( Number(b, "machines"), 2 );
			EXPECT_EQ( Number(Json::AsObject(b, "coverage"), "performance"), 2 );
			EXPECT_EQ( Number(Json::AsObject(b, "coverage"), "quality"), 1 );
			let& members = Json::AsArray( b, "members" );
			ASSERT_EQ( members.size(), 2u ) << serialize( members );
			let& first = members[0].as_object();
			EXPECT_EQ( Json::AsNumber<uint32>(Json::AsObject(first, "machine"), "id"), m1 ) << "in member order";
			EXPECT_TRUE( first.at("line").is_null() );
			EXPECT_EQ( Json::AsSV(first, "shift"), "Written" ) << "a member's bucket is the line's, labelled as it is";
			EXPECT_EQ( Number(first, "planned"), 300000 );
			EXPECT_NEAR( Number(first, "oee"), 0.7*0.5*33/35, 1e-9 );
			let& second = members[1].as_object();
			EXPECT_EQ( Json::AsNumber<uint32>(Json::AsObject(second, "machine"), "id"), m2 );
			EXPECT_EQ( Number(second, "run"), 180000 );
			EXPECT_NEAR( Number(second, "oee"), 0.6, 1e-9 ) << "availability times a performance of one";
		}
		{//nested:  the area's bucket is the line's, and its one member the line.
			let whole = read( figuresQL, area, {{"by", nullptr}, {"members", true}, {"end", Iso(t0+5min)}} ).as_array();
			ASSERT_EQ( whole.size(), 1u ) << serialize( whole );
			let& b = whole[0].as_object();
			EXPECT_TRUE( b.at("shift").is_null() );
			EXPECT_EQ( Number(b, "planned"), 600000 ) << "each machine's planned time is its own calendar's";
			EXPECT_EQ( Number(b, "run"), 390000 );
			EXPECT_NEAR( Number(b, "oee"), availability*performance*quality, 1e-9 );
			EXPECT_EQ( Number(b, "machines"), 2 );
			let& members = Json::AsArray( b, "members" );
			ASSERT_EQ( members.size(), 1u );
			let& member = members[0].as_object();
			EXPECT_EQ( Json::AsNumber<uint32>(Json::AsObject(member, "line"), "id"), line );
			EXPECT_EQ( Json::AsSV(Json::AsObject(member, "line"), "level"), "Line" );
			EXPECT_TRUE( member.at("machine").is_null() );
			EXPECT_EQ( Number(member, "machines"), 2 );
			EXPECT_NEAR( Number(member, "oee"), availability*performance*quality, 1e-9 );
			EXPECT_EQ( read(figuresQL, area, {{"by", "PT1M"}, {"members", nullptr}, {"end", Iso(t0+5min)}}).as_array().size(), 5u );
			EXPECT_NE( Refused([&]{ read( figuresQL, area, {{"by", "shift"}, {"members", nullptr}} ); }).find("no calendar"), string::npos ) << "a calendar-less line has no shifts";
		}
		EXPECT_NE( Refused([&]{ Mutate( "oee( start:$start, end:$end ){ oee }", {{"start", Iso(t0)}, {"end", Iso(t0+8min)}} ); }).find("one of the two"), string::npos );
		EXPECT_NE( Refused([&]{ Mutate( "oee( machine:$m, line:$l, start:$start, end:$end ){ oee }", {{"m", m1}, {"l", line}, {"start", Iso(t0)}, {"end", Iso(t0+8min)}} ); }).find("one of the two"), string::npos );
		EXPECT_EQ( Status([&]{ read( "oeeLosses( line:$id, start:$start, end:$end ){ ms }", 999'999 ); }), EHttpStatus::NotFound );

		constexpr sv stopsQL{ "oeeStops( line:$id, start:$start, end:$end, limit:$limit, continuation:$continuation ){ continuation stops{ start end ms machine{ id name } } }" };
		let newest = read( stopsQL, line, {{"limit", 1}, {"continuation", nullptr}} ).as_object();
		ASSERT_EQ( Json::AsArray(newest, "stops").size(), 1u ) << serialize( newest );
		{
			let& stop = Json::AsArray( newest, "stops" )[0].as_object();
			EXPECT_EQ( At(stop, "start"), t0+75s ) << "newest first, across the machines";
			EXPECT_EQ( Json::AsNumber<uint32>(Json::AsObject(stop, "machine"), "id"), m1 );
		}
		ASSERT_TRUE( newest.at("continuation").is_string() ) << serialize( newest );
		let older = read( stopsQL, line, {{"limit", 1}, {"continuation", newest.at("continuation")}} ).as_object();
		ASSERT_EQ( Json::AsArray(older, "stops").size(), 1u ) << serialize( older );
		{
			let& stop = Json::AsArray( older, "stops" )[0].as_object();
			EXPECT_EQ( At(stop, "start"), t0+60s );
			EXPECT_EQ( Number(stop, "ms"), 120000 );
			EXPECT_EQ( Json::AsNumber<uint32>(Json::AsObject(stop, "machine"), "id"), m2 );
		}
		EXPECT_TRUE( older.at("continuation").is_null() ) << "the last page";
		let all = read( stopsQL, area, {{"limit", nullptr}, {"continuation", nullptr}} );
		EXPECT_EQ( Json::AsArray(all.as_object(), "stops").size(), 2u );
		EXPECT_NE( Refused([&]{ read( stopsQL, area, {{"limit", 1}, {"continuation", newest.at("continuation")}} ); }).find("continuation"), string::npos ) << "another read's";

		{//both stops are uncoded, no reason having pump1's code:  one loss row, the two summed.
			let losses = read( "oeeLosses( line:$id, start:$start, end:$end ){ kind ms stops reason{ id } }", line ).as_array();
			ASSERT_EQ( losses.size(), 1u ) << serialize( losses );
			EXPECT_EQ( Json::AsSV(losses[0].as_object(), "kind"), "uncoded" );
			EXPECT_EQ( Number(losses[0].as_object(), "ms"), 210000 );
			EXPECT_EQ( Number(losses[0].as_object(), "stops"), 2 );
		}
		{//a timeline per machine reached, in member order.
			let drawn = read( "oeeTimeline( line:$id, start:$start, end:$end ){ machine{ id } planned{ start end } segments{ start end state } }", area, {{"end", Iso(t0+5min)}} ).as_array();
			ASSERT_EQ( drawn.size(), 2u ) << serialize( drawn );
			EXPECT_EQ( Json::AsNumber<uint32>(Json::AsObject(drawn[0].as_object(), "machine"), "id"), m1 );
			EXPECT_EQ( Json::AsNumber<uint32>(Json::AsObject(drawn[1].as_object(), "machine"), "id"), m2 );
			let& segments = Json::AsArray( drawn[1].as_object(), "segments" );
			ASSERT_EQ( segments.size(), 3u ) << serialize( segments );
			EXPECT_EQ( Json::AsSV(segments[1].as_object(), "state"), "stopped" );
			EXPECT_EQ( At(segments[1].as_object(), "start"), t0+60s );
			EXPECT_EQ( At(segments[1].as_object(), "end"), t0+180s );
			EXPECT_EQ( Json::AsArray(drawn[1].as_object(), "planned").size(), 1u ) << "pump2's own calendar's";
		}
		{
			let status = Mutate( "oeeLineStatus( line:$id ){ line{ id name level } enabled calendar calendarId deleted machines{ machine{ id } engine state } }", {{"id", line}} ).as_object();
			EXPECT_EQ( Json::AsSV(Json::AsObject(status, "line"), "level"), "Line" );
			EXPECT_TRUE( Json::AsBool(status, "enabled") );
			EXPECT_EQ( Json::AsSV(status, "calendar"), "found" );
			EXPECT_EQ( Number(status, "calendarId"), calendar );
			EXPECT_FALSE( Json::AsBool(status, "deleted") );
			let& machines = Json::AsArray( status, "machines" );
			ASSERT_EQ( machines.size(), 2u ) << serialize( status );
			EXPECT_EQ( Json::AsSV(machines[0].as_object(), "engine"), "live" );
			EXPECT_EQ( Json::AsSV(machines[1].as_object(), "state"), "running" );
			let areas = Mutate( "oeeLineStatus( line:$id ){ calendar }", {{"id", area}} );
			EXPECT_EQ( Json::AsSV(areas.as_object(), "calendar"), "none" );
			EXPECT_NE( Refused([&]{ Mutate( "oeeLineStatus( machine:$id ){ calendar }", {{"id", m1}} ); }).find("needs line"), string::npos );
		}
		{//pump2's group grants the suite's user no Read:  a figure over the machines a user may see is another figure, so the line's reads are refused, the area's too.
			auto enforced = EnforcedHistGroups();
			ScopeHistGroup( pump2.Group, Access::ERights::None );
			absl::Cleanup unscope = [&]{ try{ UnscopeHistGroup( pump2.Group ); }catch( const std::exception& e ){ ADD_FAILURE() << "the group's resource wasn't deleted:  " << e.what(); } };
			EXPECT_EQ( Status([&]{ read( figuresQL, line, {{"by", nullptr}, {"members", nullptr}} ); }), EHttpStatus::Forbidden );
			EXPECT_EQ( Status([&]{ read( stopsQL, area, {{"limit", nullptr}, {"continuation", nullptr}} ); }), EHttpStatus::Forbidden );
			EXPECT_EQ( Status([&]{ Mutate( "oeeLineStatus( line:$id ){ calendar }", {{"id", line}} ); }), EHttpStatus::Forbidden );
			EXPECT_NO_THROW( Mutate("oee( machine:$id, start:$start, end:$end ){ oee }", {{"id", m1}, {"start", Iso(t0)}, {"end", Iso(t0+8min)}}) ) << "pump1's own stays readable";
		}
		//A deleted machine is out of its lines' figures until its restore, and a deleted member line out of its parent's.
		Mutate( "deleteOeeMachine( id:$id )", {{"id", m2}} );
		{
			let shifts = read( figuresQL, line, {{"by", "shift"}, {"members", true}} );
			let& b = shifts.as_array()[0].as_object();
			EXPECT_EQ( Number(b, "machines"), 1 );
			EXPECT_EQ( Number(b, "planned"), 300000 );
			EXPECT_EQ( Json::AsArray(b, "members").size(), 1u );
		}
		Mutate( "deleteOeeLine( id:$id )", {{"id", line}} );
		{
			let whole = read( figuresQL, area, {{"by", nullptr}, {"members", true}} );
			let& b = whole.as_array()[0].as_object();
			EXPECT_EQ( Number(b, "machines"), 0 );
			EXPECT_TRUE( b.at("oee").is_null() );
			EXPECT_TRUE( Json::AsArray(b, "members").empty() );
		}
		let own = read( figuresQL, line, {{"by", nullptr}, {"members", nullptr}} );
		EXPECT_EQ( Number(own.as_array()[0].as_object(), "machines"), 1 ) << "a deleted line's own rows are still read";
	}

	//OeeSubscribe{ line }:  the ack's counts, shift and combined figures;  a second subscription of the line, whose ack is a read
	//of its own;  then pump1 stopped, which both hear from one computation;  the refusals;  and the line deleted, which ends
	//what is left with an exception under its id.
	TEST_F( OeeLinesTests, TwoSubscriptionsOfALineReadItsRowsOncePerChange ){
		EarlyInTheMinute();
		let from = floor<seconds>( Now() )-5s;
		Pump1Machine pump1{ "oeeLineLive1", from-2s };
		Pump1Machine pump2{ "oeeLineLive2", from-2s, true, 200, 0, Pump2Nodes };
		let shiftStart = floor<minutes>( from )-10min;
		let calendar = ShiftCalendar( "oeeLineLive", shiftStart, 120 );
		absl::Cleanup purgeCalendar = [=]{ PurgeCalendar( calendar ); };
		let m1 = pump1.Create( from, {ERole::State, ERole::Total, ERole::Reject, ERole::Reason}, {{"idealCycle", 3000.0}, {"calendarId", calendar}} );
		let m2 = pump2.Create( from, {ERole::State, ERole::Total}, {{"idealCycle", 6000.0}, {"calendarId", calendar}} );
		auto engine = WaitLive( m1 );
		WaitLive( m2 );
		Lines lines;
		let line = lines.Create( "oeeLineLive", "Line", {{"calendarId", calendar}} );
		AddMachine( line, m1, 1 );
		AddMachine( line, m2, 2 );

		EXPECT_EQ( Status([&]{ Subscribe( 999'999, ms<LineHeard>() ); }), EHttpStatus::NotFound );
		EXPECT_EQ( Status([&]{ BlockAwait<ClientSocketAwait<FromServer::OeeStatus>,FromServer::OeeStatus>( Socket().OeeSubscribe(0, ms<LineHeard>()) ); }), EHttpStatus::BadRequest ) << "neither a machine nor a line";
		let solo = lines.Create( "oeeLineLiveSolo", "Line", {{"calendarId", calendar}} );
		AddMachine( solo, m1 );
		auto heardSolo = ms<LineHeard>();
		EXPECT_EQ( Subscribe(solo, heardSolo).machines().running(), 1u );
		let soloFeed = Oee().LineFeed( solo );
		ASSERT_TRUE( soloFeed );
		{//Read on the group of every machine it reaches, and a machine added to a line is gated for its subscriptions as a new one is.
			auto enforced = EnforcedHistGroups();
			ScopeHistGroup( pump2.Group, Access::ERights::None );
			absl::Cleanup unscope = [&]{ try{ UnscopeHistGroup( pump2.Group ); }catch( const std::exception& e ){ ADD_FAILURE() << "the group's resource wasn't deleted:  " << e.what(); } };
			EXPECT_EQ( Status([&]{ Subscribe( line, ms<LineHeard>() ); }), EHttpStatus::Forbidden );
			AddMachine( solo, m2 );
			_clock->Advance( Oee().Live() );
			ASSERT_TRUE( WaitUntil([&]{ return !heardSolo->Errors().empty(); }, 15s) ) << "pump2's figures pushed to a subscription that may not read them";
			EXPECT_EQ( soloFeed->Subscribers(), 0u );
		}
		let feed = Oee().LineFeed( line );
		ASSERT_TRUE( feed );
		EXPECT_EQ( feed->Subscribers(), 0u ) << "none joined past a refusal";
		EXPECT_EQ( feed->Reads(), 0u ) << "nothing is computed for no subscriber";

		auto heardA = ms<LineHeard>();
		let ack = Subscribe( line, heardA );
		let subscriptionA = Socket().LastOeeSubscription();
		EXPECT_EQ( ack.machines().running(), 2u );
		EXPECT_EQ( ack.machines().stopped(), 0u );
		EXPECT_EQ( ack.machines().unknown(), 0u );
		EXPECT_EQ( ack.state(), FromServer::OeeUnknown ) << "a line's state is its counts";
		EXPECT_FALSE( ack.has_stop() );
		EXPECT_TRUE( ack.planned() );
		EXPECT_EQ( ack.shift().name(), "Written" );
		EXPECT_EQ( Time(ack.shift().start()), shiftStart );
		ASSERT_TRUE( ack.has_figures() );
		EXPECT_EQ( Time(ack.figures().start()), shiftStart );
		EXPECT_EQ( ack.figures().machines(), 2u );
		EXPECT_EQ( ack.figures().performance_coverage(), 2u );
		EXPECT_EQ( ack.figures().quality_coverage(), 1u );
		EXPECT_TRUE( ack.figures().partial() ) << "the machines' rows start inside the shift";
		EXPECT_EQ( feed->Reads(), 1u );
		auto heardB = ms<LineHeard>();
		let ackB = Subscribe( line, heardB );
		let subscriptionB = Socket().LastOeeSubscription();
		EXPECT_EQ( ackB.machines().running(), 2u );
		EXPECT_EQ( feed->Subscribers(), 2u );
		EXPECT_EQ( feed->Reads(), 2u ) << "a new subscription's ack is a read of its own";

		//pump1 stopped:  one computation, `live` after the last, which both subscriptions hear.
		let reads = feed->Reads(), pushed = feed->Pushed(), countA = heardA->Count(), countB = heardB->Count();
		let stopped = Now();
		WriteBool( Status1, false, stopped );
		ASSERT_TRUE( WaitUntil([&]{ auto s = engine->State(); return s && s->State==Oee::EState::Stopped; }, 15s) );
		_clock->Advance( Oee().Live() );
		for( let& heard : {heardA, heardB} ){
			ASSERT_TRUE( WaitUntil([&]{ return heard->Count() && heard->Last().machines().stopped()==1; }, 15s) ) << "no push of the stop";
			EXPECT_EQ( heard->Last().machines().running(), 1u );
			EXPECT_TRUE( heard->Last().has_figures() );
		}
		//Each change since - the stop, and an interval's close should a minute have ended - was read once and heard by both.
		EXPECT_TRUE( WaitUntil([&]{ let n = feed->Pushed()-pushed; return feed->Reads()-reads==n && heardA->Count()-countA==n && heardB->Count()-countB==n; }, 15s) )
			<< feed->Reads()-reads << " reads for " << feed->Pushed()-pushed << " pushes, of which A heard " << heardA->Count()-countA << " and B " << heardB->Count()-countB;
		EXPECT_GE( feed->Pushed()-pushed, 1u );

		//Unsubscribed, B hears no more;  the line deleted, A's subscription ends under its id, and a deleted line takes none.
		EXPECT_TRUE( Unsubscribe(subscriptionB) );
		EXPECT_EQ( feed->Subscribers(), 1u );
		Mutate( "deleteOeeLine( id:$id )", {{"id", line}} );
		ASSERT_TRUE( WaitUntil([&]{ return !heardA->Errors().empty(); }, 15s) );
		EXPECT_NE( heardA->Errors()[0].find("deleted"), string::npos ) << heardA->Errors()[0];
		EXPECT_TRUE( heardB->Errors().empty() );
		EXPECT_EQ( feed->Subscribers(), 0u );
		EXPECT_THROW( Unsubscribe(subscriptionA), GatewayErrorResponse ) << "the id is free";
		EXPECT_EQ( Status([&]{ Subscribe( line, ms<LineHeard>() ); }), EHttpStatus::NotFound );
		Mutate( "restoreOeeLine( id:$id )", {{"id", line}} );
		auto again = ms<LineHeard>();
		EXPECT_EQ( Subscribe(line, again).machines().stopped(), 1u ) << "restored, it is subscribed again";
		EXPECT_TRUE( Unsubscribe(Socket().LastOeeSubscription()) );
		EXPECT_EQ( Oee().ActiveLineFeeds(), 0u ) << "no line's feed has a subscriber left";
	}
}