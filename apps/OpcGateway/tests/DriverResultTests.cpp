#include <jde/fwk/process/execution.h>
#include <jde/tests/Capture.h>
#include <jde/opc/openProtocol/logTags.h>
#include "utils/DriverResults.h"
#include "../src/async/Subscriptions.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using OpenProtocol::Controller;
	using OpenProtocol::LocalTime;
	using OpenProtocol::Mid0061;

	//docs/open-protocol/plan.md Phase 5 (#467):  the Open Protocol driver's results (drivers/openProtocol/OpenProtocolDriver.h) -
	//MID 0061 from the library's test controller written to `Result` and counted in `Counters`, read as a web session's client
	//reads the row's server, collected by a group and served by the gateway's endpoint.
	struct DriverResultTests : DriverResultFixture{};

	//A result of each revision of MID 0061, 1 to 7, lands in `Result`:  every field the revision carries with its value, its type
	//and the result's one source timestamp, the controller's time read in the row's zone, and every field it does not at
	//Bad_NotSupported.  Before the first, each field the revision carries reads Bad_WaitingForInitialData and the counters Good 0;
	//through a loss of the link the last result stands, and a field the next connection's revision gains has its first value
	//with that revision's first result.
	TEST_F( DriverResultTests, AResultOfEachRevisionLandsInResult ){
		auto oldest = Station();
		oldest.Revisions[60] = 1;
		Controller station{ *Executor(), ControllerPort(), move(oldest) };
		const DriverRow row{ "driver-results", NewYork };

		//Fixed, the same for every row, and built before the controller answers.
		let children = [&]( sv object ){
			vector<string> names;
			let node = Socket().QuerySync( "node( opc: $opc, id: $id ){ children{ name nodeClass } }", {{"opc", row.Slug}, {"id", DriverNode(object).ToJson()}} ).as_object();
			for( let& child : Json::AsArray(node, "children") ){
				names.push_back( Json::AsString(child.as_object(), "name") );
				EXPECT_EQ( Json::FindNumber<uint>(child.as_object(), "nodeClass"), UA_NODECLASS_VARIABLE ) << names.back();
			}
			std::ranges::sort( names );
			return names;
		};
		vector<string> expected{ "PsetChangeTime", "TighteningId", "Time" };
		for( let& field : Fields() )
			expected.emplace_back( field.Name );
		std::ranges::sort( expected );
		EXPECT_EQ( children("Result"), expected ) << "a variable per field of MID 0061 but the cell id, the channel id and the controller name";
		EXPECT_EQ( children("Counters"), (vector<string>{"Nok", "Ok", "Total"}) );
		EXPECT_EQ( TighteningResult::Variables().back().Name, "TighteningId" ) << "a result's last write";
		let attributes = Socket().QuerySync( "node( opc: $opc, id: $id ){ accessLevel userAccessLevel }", {{"opc", row.Slug}, {"id", DriverNode("Result.Torque").ToJson()}} ).as_object();
		EXPECT_EQ( Json::FindNumber<uint>(attributes, "accessLevel"), optional<uint>{UA_ACCESSLEVELMASK_READ} ) << serialize( attributes );
		EXPECT_EQ( Json::FindNumber<uint>(attributes, "userAccessLevel"), optional<uint>{UA_ACCESSLEVELMASK_READ} ) << serialize( attributes );

		ASSERT_TRUE( WaitRevision(row, 1) ) << OpText( Op(row, "Revisions") );
		EXPECT_EQ( RevisionsOf(station, 60), (vector<uint16_t>{7, 6, 5, 4, 3, 2, 1}) ) << "from 7, a step down on each code 74";
		{
			let read = Variables( row );
			for( let& field : Fields() ){
				let& v = Of( read, Path(field.Name) );
				EXPECT_EQ( v.status, field.Since>1 ? NotSupported : Waiting ) << field.Name << ": " << UAException::Message( v.status );
				EXPECT_TRUE( Is(v, field.Type) ) << field.Name << " holds no " << UA_TYPES[field.Type].typeName;
			}
			EXPECT_EQ( Of(read, "Result.TighteningId").status, Waiting );
			EXPECT_EQ( Of(read, "Result.Time").status, Waiting );
			EXPECT_EQ( Counters(read), (array<optional<uint>,3>{0, 0, 0}) );
			for( let counter : {"Counters.Total", "Counters.Ok", "Counters.Nok"} )
				EXPECT_EQ( Of(read, counter).status, UA_STATUSCODE_GOOD ) << counter;
		}

		let at = LocalTime{ local_days{year{2026}/7/4}+10h+15min+30s };//on the controller's clock:  14:15:30 UTC in New York's summer.
		const TimePoint source{ sys_days{year{2026}/7/4}+14h+15min+30s };
		for( uint16_t revision=1; revision<=7; ++revision ){
			SCOPED_TRACE( Ƒ("revision {}", revision) );
			if( revision>1 ){
				station.Edit( [=]( Controller::Script& script ){ script.Revisions[60] = revision; } );
				station.Drop();
				ASSERT_TRUE( WaitRevision(row, revision) ) << OpText( Op(row, "Revisions") );
				let read = Variables( row );
				EXPECT_EQ( ResultId(read), optional<uint>{100u+revision-1} ) << "the last result, through the loss of the link";
				for( let& field : Fields() ){
					let& v = Of( read, Path(field.Name) );
					EXPECT_EQ( v.status, field.Since>=revision ? NotSupported : UA_STATUSCODE_GOOD ) << field.Name << ": " << UAException::Message( v.status );
				}
			}
			let time = source+minutes{ revision };
			station.Push( Rundown(100+revision, at+minutes{revision}) );
			ASSERT_TRUE( WaitResults(row, revision) ) << OpCount( Op(row, "Results") ).value_or( 0 );
			let read = Variables( row );
			for( let& field : Fields() ){
				let& v = Of( read, Path(field.Name) );
				EXPECT_TRUE( Is(v, field.Type) ) << field.Name << " holds no " << UA_TYPES[field.Type].typeName;
				if( field.Since>revision ){
					EXPECT_EQ( v.status, NotSupported ) << field.Name << ": " << UAException::Message( v.status );
					continue;
				}
				ASSERT_EQ( v.status, UA_STATUSCODE_GOOD ) << field.Name << ": " << UAException::Message( v.status );
				let value = field.Name=="Angle" && revision==7 ? jvalue{ 355.25 } : field.Value;//revision 7's final angle decimal.
				EXPECT_EQ( serialize(v.ValueJson(nullptr)), serialize(value) ) << field.Name;
				EXPECT_EQ( SourceTime(v), optional<TimePoint>{time} ) << field.Name;
			}
			EXPECT_EQ( ResultId(read), optional<uint>{100u+revision} );
			EXPECT_EQ( SourceTime(Of(read, "Result.TighteningId")), optional<TimePoint>{time} );
			EXPECT_EQ( Time(Of(read, "Result.Time")), optional<TimePoint>{time} ) << "the wire's time in the row's zone";
			EXPECT_EQ( Time(Of(read, "Result.PsetChangeTime")), optional<TimePoint>{PsetChanged} ) << "under the zone's winter offset";
			EXPECT_EQ( Counters(read), (array<optional<uint>,3>{revision, revision, 0}) );
			EXPECT_EQ( SourceTime(Of(read, "Counters.Total")), optional<TimePoint>{time} );
		}
		EXPECT_EQ( OpText(Op(row, "Revisions")), "0001:6 0061:7 0064:7 0080:1" ) << "MID 0064's, of the reconnects' recoveries";
	}

	//The controller's time is whole seconds:  a result not after the last one written takes the last's time and a millisecond, so
	//two rundowns in a second, and one after the clock is set back, are distinct and in order, with one warning for a clock set
	//back past maxSkew.  `Time` holds the controller's own.  A result with no time is sourced at its receipt, and a field it leaves
	//blank reads Bad_NotSupported.  The row's zone is not the suite's, so no box's own zone reads its times right.
	TEST_F( DriverResultTests, TwoResultsInASecondAreDistinct ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-second", R"({"timeZone":"Europe/Stockholm"})" };
		ASSERT_TRUE( WaitRevision(row, 7) );
		let at = LocalTime{ local_days{year{2026}/7/4}+10h+15min+30s };
		const TimePoint source{ sys_days{year{2026}/7/4}+8h+15min+30s };//Stockholm's summer.
		uint results{};
		let push = [&]( Mid0061 rundown ){
			station.Push( move(rundown) );
			EXPECT_TRUE( WaitResults(row, ++results) );
			return Variables( row );
		};
		let sourced = [&]( let& read ){ return SourceTime( Of(read, "Result.TighteningId") ); };
		EXPECT_EQ( sourced(push(Rundown(1, at))), optional<TimePoint>{source} );
		let second = push( Rundown(2, at) );
		EXPECT_EQ( sourced(second), optional<TimePoint>{source+1ms} ) << "the same second";
		for( let path : {"Result.Vin", "Result.Torque", "Result.Time", "Counters.Total", "Counters.Ok"} )
			EXPECT_EQ( SourceTime(Of(second, path)), optional<TimePoint>{source+1ms} ) << path << ":  one result, one source timestamp";
		EXPECT_EQ( Time(Of(second, "Result.Time")), optional<TimePoint>{source} ) << "the controller's own";
		const Logging::Capture warnings{ OpenProtocol::LogTag, ELogLevel::Warning };
		let wentBack = [&]{ return warnings.Find( [&]( const Logging::Entry& e ){ return e.Message().contains( "clock went back" ) && e.Message().contains( row.Slug ); } ).size(); };
		EXPECT_EQ( sourced(push(Rundown(3, at-10s))), optional<TimePoint>{source+2ms} ) << "a clock set back";
		EXPECT_EQ( wentBack(), 0u ) << "inside maxSkew";
		EXPECT_EQ( sourced(push(Rundown(4, at+1s))), optional<TimePoint>{source+1s} ) << "a later second as it is";
		EXPECT_EQ( sourced(push(Rundown(5, at-10min))), optional<TimePoint>{source+1s+1ms} ) << "a clock set back past maxSkew";
		EXPECT_EQ( wentBack(), 1u );
		EXPECT_EQ( sourced(push(Rundown(6, at-9min))), optional<TimePoint>{source+1s+2ms} );
		EXPECT_EQ( wentBack(), 1u ) << "once, until a result is after the last";

		auto blank = Rundown( 7, nullopt );
		blank.Torque.reset();
		let before = Clock::now();
		let read = push( move(blank) );
		ASSERT_TRUE( sourced(read) );
		EXPECT_GE( *sourced(read), Ms(before) ) << "no time:  its receipt";
		EXPECT_LE( *sourced(read), Clock::now() );
		EXPECT_EQ( Of(read, "Result.Time").status, NotSupported );
		EXPECT_EQ( Of(read, "Result.Torque").status, NotSupported );
		EXPECT_EQ( SourceTime(Of(read, "Result.Torque")), sourced(read) );
		EXPECT_EQ( Of(read, "Result.TorqueMax").status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( ResultId(read), optional<uint>{7} );
	}

	//An hour that comes twice, New York's on 1 November 2026, is the pass nearer the time it is read at:  a live result comes
	//seconds after it is made.  A result made in it and read long after, as the suite's is, is the second pass's, `Time` with it.
	TEST_F( DriverResultTests, AnHourThatComesTwiceIsThePassNearerItsReceipt ){
		let& zone = *std::chrono::locate_zone( "America/New_York" );
		let local = LocalTime{ local_days{year{2026}/11/1}+1h+30min };
		const TimePoint first{ sys_days{year{2026}/11/1}+5h+30min }, second{ first+1h };//EDT's, then EST's.
		EXPECT_EQ( TighteningResult::ToSys(local, zone, first+2s), first );
		EXPECT_EQ( TighteningResult::ToSys(local, zone, second+2s), second );
		EXPECT_EQ( TighteningResult::ToSys(local, zone, first+29min), first ) << "short of halfway";
		EXPECT_EQ( TighteningResult::ToSys(local, zone, second-29min), second ) << "past halfway";
		EXPECT_EQ( TighteningResult::ToSys(LocalTime{local_days{year{2026}/7/4}+10h}, zone, first), TimePoint{sys_days{year{2026}/7/4}+14h} ) << "an hour that comes once";

		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-twice", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		station.Push( Rundown(1, local) );
		ASSERT_TRUE( WaitResults(row, 1) );
		let read = Variables( row );
		EXPECT_EQ( SourceTime(Of(read, "Result.TighteningId")), optional<TimePoint>{second} ) << "read after the fall-back:  the second pass";
		EXPECT_EQ( Time(Of(read, "Result.Time")), optional<TimePoint>{second} );
	}

	//The counters count since the driver started:  at revision 3 a tightening, result type 1, and no loosening or batch
	//increment, Ok and Nok by the tightening status;  below revision 3, where a result names no type, every result.  They keep
	//their values through a loss of the link, and `_Op.Results` counts every result written.  A field the next connection's lower
	//revision lacks turns Bad_NotSupported a millisecond past the last result, whose clock is the controller's, not the gateway's.
	TEST_F( DriverResultTests, TheCountersCountTightenings ){
		auto typed = Station();
		typed.Revisions[60] = 3;
		Controller station{ *Executor(), ControllerPort(), move(typed) };
		const DriverRow row{ "driver-counters" };//the gateway's own zone.
		ASSERT_TRUE( WaitRevision(row, 3) );
		let now = LocalTime{ floor<seconds>(std::chrono::current_zone()->to_local(Clock::now())) };
		uint results{};
		let push = [&]( uint8_t type, uint8_t status ){
			auto rundown = Rundown( 0, now+seconds{results} );
			rundown.TighteningId.reset();//the controller's next.
			rundown.ResultType = type;
			rundown.Status = status;
			station.Push( move(rundown) );
			EXPECT_TRUE( WaitResults(row, ++results) );
			return Variables( row );
		};
		using Counts = array<optional<uint>,3>;
		EXPECT_EQ( Counters(push(1, 1)), (Counts{1, 1, 0}) );
		let nok = push( 1, 0 );
		EXPECT_EQ( Counters(nok), (Counts{2, 1, 1}) );
		EXPECT_EQ( serialize(Of(nok, "Result.Status").ValueJson(nullptr)), "false" );
		let loosening = push( 2, 1 );
		EXPECT_EQ( Counters(loosening), (Counts{2, 1, 1}) ) << "a loosening";
		EXPECT_EQ( serialize(Of(loosening, "Result.ResultType").ValueJson(nullptr)), "2" ) << "written all the same";
		EXPECT_EQ( SourceTime(Of(loosening, "Counters.Total")), SourceTime(Of(nok, "Result.TighteningId")) ) << "as the last result counted left it";
		EXPECT_EQ( Counters(push(3, 1)), (Counts{2, 1, 1}) ) << "a batch increment";
		let last = push( 1, 1 );//sourced seconds ahead of the gateway's clock.
		EXPECT_EQ( Counters(last), (Counts{3, 2, 1}) );
		EXPECT_EQ( OpCount(Op(row, "Results")), optional<uint>{5} );

		station.Edit( []( Controller::Script& script ){ script.Revisions[60] = 1; } );
		station.Drop();
		ASSERT_TRUE( WaitRevision(row, 1) );
		let reconnected = Variables( row );
		EXPECT_EQ( Counters(reconnected), (Counts{3, 2, 1}) ) << "through the loss of the link";
		let lastSource = SourceTime( Of(last, "Result.TighteningId") );
		ASSERT_TRUE( lastSource );
		let& lacked = Of( reconnected, "Result.Strategy" );//revision 2's.
		EXPECT_EQ( lacked.status, NotSupported ) << UAException::Message( lacked.status );
		EXPECT_EQ( SourceTime(lacked), optional<TimePoint>{*lastSource+1ms} ) << "after the last result";
		EXPECT_EQ( Counters(push(2, 0)), (Counts{4, 2, 2}) ) << "revision 1 names no type";
		EXPECT_EQ( Counters(push(3, 1)), (Counts{5, 3, 2}) );
	}

	//A group over `Result.TighteningId`, `Result.Torque`, `Counters.Ok` and `Result.Status` with no MinTimeInterval records every
	//result of a burst of twenty inside one publishing interval, the shipped half second:  the collector asks sampling 0 and the
	//deepest queue, and a stored value at 0 is sampled on each write.  Twenty in one second of the controller's clock are twenty
	//records a millisecond apart.  An OK after an OK is no change, so `Status` stores the first alone and reads as the stepped
	//value after it.  And the gateway's endpoint serves the row's members:  its processed read agrees with the QL's `history`.
	TEST_F( DriverResultTests, AGroupRecordsEveryResultOfABurst ){
		const DurationOverride publishing{ _publishingInterval, 500ms };//the shipped one, which the row's collector asks its subscription under.
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-burst", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const ResultGroup group{ row, "driver-burst", {{"Result.TighteningId", {}}, {"Result.Torque", {}}, {"Counters.Ok", {}}, {"Result.Status", {}}} };
		enum : uint{ TighteningId, Torque, Ok, Status };

		constexpr uint burst{ 20 };
		const TimePoint t0{ ceil<seconds>(Clock::now()) };//ahead of every member's first value, the driver's start.
		let at = LocalTime{ floor<seconds>(std::chrono::locate_zone("America/New_York")->to_local(t0)) };
		for( uint i=0; i<burst; ++i ){
			auto rundown = Rundown( 501+i, at );
			rundown.Torque = 10+i*0.25;
			station.Push( move(rundown) );
		}
		ASSERT_NO_THROW( WaitStored(group.Host, group.Members[TighteningId], t0, burst) );
		ASSERT_NO_THROW( WaitStored(group.Host, group.Members[Ok], t0, burst) );//each result's last write the group takes.

		let ids = Records( group, TighteningId, t0 );
		let torques = Records( group, Torque, t0 );
		let oks = Records( group, Ok, t0 );
		ASSERT_EQ( ids.size(), burst );
		ASSERT_EQ( torques.size(), burst );
		ASSERT_EQ( oks.size(), burst );
		for( uint i=0; i<burst; ++i ){
			SCOPED_TRACE( i );
			ASSERT_TRUE( Is(ids[i], UA_TYPES_UINT32) && Is(torques[i], UA_TYPES_DOUBLE) && Is(oks[i], UA_TYPES_UINT64) ) << "each as its variable's type";
			EXPECT_EQ( ids[i].Get<UA_UInt32>(0), 501+i );
			EXPECT_EQ( torques[i].Get<UA_Double>(0), 10+i*0.25 );
			EXPECT_EQ( oks[i].Get<UA_UInt64>(0), i+1 );
			for( let* member : {&ids, &torques, &oks} )
				EXPECT_EQ( SourceTime((*member)[i]), optional<TimePoint>{t0+milliseconds{i}} ) << "distinct and increasing";
		}
		ASSERT_TRUE( ids.front().hasServerTimestamp && ids.back().hasServerTimestamp );
		let span = UADateTime{ ids.back().serverTimestamp }.Time()-UADateTime{ ids.front().serverTimestamp }.Time();
		EXPECT_LT( span, 500ms ) << "the burst outlasted a publishing interval:  " << duration_cast<milliseconds>( span ).count() << " ms";
		EXPECT_LT( abs(UADateTime{ids.front().serverTimestamp}.Time()-Clock::now()), 1min ) << "the gateway's clock, not the controller's";

		let statuses = Records( group, Status, t0 );
		ASSERT_EQ( statuses.size(), 1u ) << "an OK after an OK is no change";
		EXPECT_TRUE( Is(statuses.front(), UA_TYPES_BOOLEAN) && statuses.front().Get<UA_Boolean>(0) );
		EXPECT_EQ( SourceTime(statuses.front()), optional<TimePoint>{t0} );
		let stepped = ReadAll( Group(group.Id), {Member(row, "Result.Status")}, {.Limit=100, .Times={t0+10ms, t0+19ms}} );
		ASSERT_EQ( stepped.size(), 2u );
		for( let& value : stepped )
			EXPECT_EQ( ValueOf(value), jvalue{true} ) << serialize( value.Value ) << " status " << value.Status;

		//The QL's `history` of the group, and the endpoint's processed read of the member it serves.
		const Request range{ .Start=t0, .End=t0+1s, .Limit=100 };
		let raw = ReadAll( Group(group.Id), {Member(row, "Result.Torque")}, range );
		ASSERT_EQ( raw.size(), burst );
		EXPECT_EQ( raw[3].Value, jvalue{10.75} );
		EXPECT_EQ( raw[3].Source, optional<TimePoint>{t0+3ms} );
		WaitPath( group.Host, group.Members[Torque] );
		auto client = Client();
		optional<NodeId> served;
		ASSERT_TRUE( WaitUntil([&]{ served = Served( *client, "Result", "Torque", row.Slug, DriverHost::NamespaceUri(row.Slug, ServerCnnctn::KindOpenProtocol) ); return served.has_value(); }) ) << "the endpoint serves no Result.Torque";
		let processed = Processed( *client, *served, NodeId{0, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE}, t0, t0+20ms, 5ms );
		ASSERT_TRUE( UA_StatusCode_isGood(processed.Status) ) << UA_StatusCode_name( processed.Status );
		EXPECT_EQ( processed.Values.size(), 4u );
		EXPECT_TRUE( Agree(processed.Values, ReadAll(Group(group.Id), {Member(row, "Result.Torque")}, {.Start=t0, .End=t0+20ms, .Limit=100, .Interval=5, .Aggregate="Average"})) );
		ASSERT_FALSE( processed.Values.empty() );
		EXPECT_EQ( Double(processed.Values.front()), optional<double>{10.5} ) << "10, 10.25, 10.5, 10.75 and 11 over the first 5 ms";
	}

	//The same member with a MinTimeInterval is sampled on a clock, so it keeps the last result of each interval and not each one.
	TEST_F( DriverResultTests, AMinTimeIntervalKeepsTheLastResult ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-interval", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const ResultGroup group{ row, "driver-interval", {{"Result.TighteningId", {{"minTimeInterval", 1000}}}} };
		let zone = std::chrono::locate_zone( "America/New_York" );
		TimePoint from{ ceil<seconds>(Clock::now()) };
		for( uint32_t first : {701u, 711u} ){
			SCOPED_TRACE( first );
			let at = LocalTime{ floor<seconds>(zone->to_local(from)) };
			for( uint i=0; i<5; ++i )
				station.Push( Rundown(first+i, at) );
			vector<Value> stored;
			ASSERT_TRUE( WaitUntil([&]{ stored = Records( group, 0, from ); return stored.size() && Is( stored.back(), UA_TYPES_UINT32 ) && stored.back().Get<UA_UInt32>(0)==first+4; }, 15s) ) << stored.size() << " record(s), none the burst's last";
			EXPECT_LE( stored.size(), 2u ) << "one, or two where the sampling fell inside the burst";
			EXPECT_EQ( SourceTime(stored.back()), optional<TimePoint>{from+4ms} );
			from += 2s;
		}
	}

	//A controller that takes a subscription at 7 and pushes at its own revision 3, as the specification allows it (§1.4):  each
	//result is read by its own header, the fields past it turn Bad_NotSupported at its time, and `_Op.Revisions` follows.  Its
	//next connection, accepted at 7 again, turns none of them back, and its next result puts `_Op.Revisions` back at 3.
	TEST_F( DriverResultTests, AControllerThatAnswersLowerIsReadByItsHeader ){
		auto lower = Station();
		lower.Revisions[60] = 3;
		lower.Lowers.insert( 60 );
		Controller station{ *Executor(), ControllerPort(), move(lower) };
		const DriverRow row{ "driver-lowers", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) ) << "accepted as asked";
		EXPECT_EQ( RevisionsOf(station, 60), (vector<uint16_t>{7}) );
		EXPECT_EQ( Of(Variables(row), "Result.CustomerErrorCode").status, Waiting ) << "revision 5's, which 7 carries";

		station.Push( Rundown(1, LocalTime{local_days{year{2026}/7/4}+10h}) );
		ASSERT_TRUE( WaitResults(row, 1) );
		const TimePoint source{ sys_days{year{2026}/7/4}+14h };
		let read = Variables( row );
		for( let& field : Fields() ){
			let& v = Of( read, Path(field.Name) );
			EXPECT_EQ( v.status, field.Since>3 ? NotSupported : UA_STATUSCODE_GOOD ) << field.Name << ": " << UAException::Message( v.status );
			EXPECT_EQ( SourceTime(v), optional<TimePoint>{source} ) << field.Name;
		}
		EXPECT_EQ( serialize(Of(read, "Result.PsetName").ValueJson(nullptr)), "\"M8 bracket\"" );
		EXPECT_TRUE( WaitRevision(row, 3) ) << OpText( Op(row, "Revisions") );

		//Accepted at 7 again at the next connect, which says nothing new of what its results carry:  nothing is written.
		station.Drop();
		ASSERT_TRUE( WaitOp(row, "Reconnects", []( let& v ){ return OpCount( v )==1u; }) );
		ASSERT_TRUE( WaitRevision(row, 7) );
		let again = Of( Variables(row), "Result.CustomerErrorCode" );
		EXPECT_EQ( again.status, NotSupported );
		EXPECT_EQ( SourceTime(again), optional<TimePoint>{source} ) << "written at the connect";
		station.Push( Rundown(2, LocalTime{local_days{year{2026}/7/4}+10h+1min}) );
		ASSERT_TRUE( WaitResults(row, 2) );
		EXPECT_TRUE( WaitRevision(row, 3) ) << "the result's own:  " << OpText( Op(row, "Revisions") );
	}

	//A controller that refuses the subscription makes no results:  a warning and `_Op.LastError`, the link standing.
	TEST_F( DriverResultTests, ARefusedSubscriptionIsReported ){
		auto refusing = Station();
		refusing.Refuses[60] = 99;
		Logging::Capture warnings{ OpenProtocol::LogTag, ELogLevel::Warning };
		Controller station{ *Executor(), ControllerPort(), move(refusing) };
		const DriverRow row{ "driver-no-results" };
		ASSERT_TRUE( WaitOp(row, "LastError", []( let& v ){ return OpText( v ).contains( "MID 0060" ); }) ) << OpText( Op(row, "LastError") );
		EXPECT_EQ( OpFlag(Op(row, "Connected")), true );
		EXPECT_FALSE( OpText(Op(row, "Revisions")).contains("0061") ) << OpText( Op(row, "Revisions") );
		EXPECT_EQ( warnings.Find([&]( const Logging::Entry& e ){ return e.Message().contains( "MID 0060" ) && e.Message().contains( row.Slug ); }).size(), 1u );
		EXPECT_EQ( Of(Variables(row), "Result.Torque").status, Waiting );
	}
}