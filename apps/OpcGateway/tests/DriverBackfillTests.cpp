#include <jde/fwk/process/execution.h>
#include <jde/tests/Capture.h>
#include <jde/opc/openProtocol/logTags.h>
#include "utils/DriverResults.h"
#include "../src/drivers/DriverServer.h"
#include "../src/drivers/openProtocol/OpenProtocolDriver.h"

#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using OpenProtocol::Controller;

	//docs/open-protocol/plan.md Phase 6 (#468):  the results a lost link kept back (drivers/openProtocol/OpenProtocolDriver.h) -
	//fetched from the library's test controller by MID 0064 when the link returns, written to `Result` ahead of the results
	//pushed meanwhile, and read as a web session's client and a group's collector read the row's server.
	struct DriverBackfillTests : DriverResultFixture{
	protected:
		using Counts = array<optional<uint>,3>;
		//The link down until Mend, as through a network's fault, while the controller's stations go on:  the controller drops its
		//client and refuses each MID 0001 after.
		Ω Sever( Controller& station, const DriverRow& row )ι->bool{
			station.Edit( []( Controller::Script& script ){ script.Refuses[1] = 16; } );
			station.Drop();
			return WaitConnected( row, false );
		}
		Ω Mend( Controller& station )ι->void{ station.Edit( []( Controller::Script& script ){ script.Refuses.erase( 1 ); } ); }
		//The tightening ids the controller was asked for by MID 0064, from its frame `from` on.
		Ω Asked( const Controller& station, uint from=0 )ι->vector<uint32_t>{
			vector<uint32_t> y;
			let frames = station.Frames();
			for( uint i=from; i<frames.size(); ++i ){
				if( frames[i].Head.Mid!=OpenProtocol::Mid0064::Id )
					continue;
				let message = OpenProtocol::Decode( frames[i].Frame );
				if( let asked = message ? get_if<OpenProtocol::Mid0064>(&message->Data) : nullptr )
					y.push_back( asked->TighteningId.value_or(0) );
			}
			return y;
		}
		Ω Local( TimePoint t )ι->LocalTime{ return LocalTime{ floor<seconds>(std::chrono::locate_zone("America/New_York")->to_local(t)) }; }//on a controller's clock in the rows' zone.
		Ω WaitLost( const DriverRow& row, uint count )ι->bool{ return WaitOp( row, "LostResults", [=]( let& v ){ return OpCount( v )==count; } ); }
		Ω ShortWalks( const Logging::Capture& warnings, const DriverRow& row )ι->vector<string>{
			vector<string> y;
			for( let& entry : warnings.Find([&]( const Logging::Entry& e ){ return e.Message().contains( "ended short" ) && e.Message().contains( row.Slug ); }) )
				y.emplace_back( entry.Message() );
			return y;
		}
	};

	//The link drops, and both of the controller's stations make results under one run of ids that is not consecutive.  When it
	//returns the driver subscribes first, holds what is pushed, and walks back from the held result's id less one, each MID 0064
	//asking for the id it was answered less one, until the last id it wrote:  the row's own results are written oldest first,
	//each at its own time, then the held one, and the other station's are dropped.  A found result is a MID 0065, with no limits:
	//`TorqueMax` turns Bad_NotSupported at the first one's time, and the held result, a MID 0061, restores it.  A group over the
	//row records them so.
	TEST_F( DriverBackfillTests, AnOutagesResultsAreFilledInOrder ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-fill", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const ResultGroup group{ row, "driver-fill", {{"Result.TighteningId", {}}, {"Result.Torque", {}}, {"Result.TorqueMax", {}}, {"Counters.Total", {}}} };
		enum : uint{ TighteningId, Torque, TorqueMax, Total };
		const TimePoint t0{ ceil<seconds>(Clock::now()) };//ahead of every member's first value, the driver's start.
		let rundown = [&]( uint32_t id, seconds after, double torque ){
			auto y = Rundown( id, Local(t0+after) );
			y.Torque = torque;
			return y;
		};
		station.Push( rundown(100, 0s, 10) );
		ASSERT_TRUE( WaitResults(row, 1) );

		ASSERT_TRUE( Sever(station, row) );
		station.Push( rundown(102, 10s, 10.5) );
		station.Keep( rundown(103, 20s, 99), 8, "Station 8" );
		station.Push( rundown(105, 30s, 11) );
		station.Keep( rundown(106, 40s, 99), 8, "Station 8" );
		//Back, the clock's reply held, so a result is pushed after the subscription and before the first MID 0064.
		let before = station.Frames().size();
		Mend( station );
		station.Delay( 300ms, 3 );//MID 0002, MID 0060's MID 0005 and MID 0081.
		ASSERT_TRUE( WaitUntil([&]{ return RevisionsOf( station, 80, before ).size()>0; }) ) << "the link never came back";
		station.Push( rundown(108, 50s, 11.5) );
		ASSERT_TRUE( WaitResults(row, 4) ) << OpCount( Op(row, "Results") ).value_or( 0 ) << " result(s)";

		EXPECT_EQ( Asked(station, before), (vector<uint32_t>{107, 105, 104, 102, 101}) ) << "from the held id less one, then each id answered less one:  106 and 103 are the other station's, and 100 the last written";
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{0} );
		EXPECT_EQ( OpCount(Op(row, "Reconnects")), optional<uint>{1} );
		let read = Variables( row );
		EXPECT_EQ( ResultId(read), optional<uint>{108} );
		EXPECT_EQ( Counters(read), (Counts{4, 4, 0}) ) << "the station's own, each once";
		let& restored = Of( read, "Result.TorqueMax" );
		EXPECT_EQ( restored.status, UA_STATUSCODE_GOOD ) << UAException::Message( restored.status );
		EXPECT_EQ( SourceTime(restored), optional<TimePoint>{t0+50s} ) << "the held result's";

		let from = t0+1s;//past the result before the outage.
		ASSERT_NO_THROW( WaitStored(group.Host, group.Members[Total], from, 3) );
		ASSERT_NO_THROW( WaitStored(group.Host, group.Members[TorqueMax], from, 2) );
		let ids = Records( group, TighteningId, from );
		let torques = Records( group, Torque, from );
		let totals = Records( group, Total, from );
		ASSERT_EQ( ids.size(), 3u );
		ASSERT_EQ( torques.size(), 3u );
		ASSERT_EQ( totals.size(), 3u );
		const array<std::tuple<uint32_t,double,seconds>,3> written{{ {102, 10.5, 10s}, {105, 11, 30s}, {108, 11.5, 50s} }};
		for( uint i=0; i<written.size(); ++i ){
			SCOPED_TRACE( i );
			let [id, torque, after] = written[i];
			ASSERT_TRUE( Is(ids[i], UA_TYPES_UINT32) && Is(torques[i], UA_TYPES_DOUBLE) && Is(totals[i], UA_TYPES_UINT64) );
			EXPECT_EQ( ids[i].Get<UA_UInt32>(0), id ) << "oldest first, the held one last";
			EXPECT_EQ( torques[i].Get<UA_Double>(0), torque );
			EXPECT_EQ( totals[i].Get<UA_UInt64>(0), i+2 );
			for( let* member : {&ids, &torques, &totals} )
				EXPECT_EQ( SourceTime((*member)[i]), optional<TimePoint>{t0+after} ) << "its own time";
		}
		let limits = Records( group, TorqueMax, from );
		ASSERT_EQ( limits.size(), 2u ) << "one Bad record at the fill's first result, and the held result's";
		EXPECT_EQ( limits[0].status, NotSupported ) << UAException::Message( limits[0].status );
		EXPECT_EQ( SourceTime(limits[0]), optional<TimePoint>{t0+10s} ) << "the first found result's";
		EXPECT_EQ( limits[1].status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( Double(limits[1]), optional<double>{14} );
		EXPECT_EQ( SourceTime(limits[1]), optional<TimePoint>{t0+50s} );
	}

	//What a found result gives `Result`:  each variable MID 0065 carries at the revision it came at, with the result's value
	//and time, `Angle` in whole degrees, and each it does not at Bad_NotSupported from the first result that lacked it.  At
	//revision 1 it is eleven fields and no station, so the controller's other station's result is the row's too.  The next
	//pushed result writes every variable Good again.
	TEST_F( DriverBackfillTests, AFoundResultHoldsWhatMid0065Carries ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-found", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const TimePoint t0{ ceil<seconds>(Clock::now()) };
		let rundown = [&]( uint32_t id, seconds after ){ return Rundown( id, Local(t0+after) ); };
		let storedSince = []( sv name ){ return std::ranges::find( TighteningResult::Variables(), name, &TighteningResult::Variable::Name )->StoredSince; };
		station.Push( rundown(500, 0s) );
		ASSERT_TRUE( WaitResults(row, 1) );

		ASSERT_TRUE( Sever(station, row) );
		station.Push( rundown(502, 10s) );
		station.Push( rundown(503, 20s) );
		Mend( station );
		ASSERT_TRUE( WaitResults(row, 3) );
		{
			let read = Variables( row );
			EXPECT_EQ( ResultId(read), optional<uint>{503} );
			uint lacked{};
			for( let& field : Fields() ){
				let& v = Of( read, Path(field.Name) );
				if( !storedSince(field.Name) ){
					++lacked;
					EXPECT_EQ( v.status, NotSupported ) << field.Name << ": " << UAException::Message( v.status );
					EXPECT_EQ( SourceTime(v), optional<TimePoint>{t0+10s} ) << field.Name << ":  from the first result that lacked it";
					continue;
				}
				ASSERT_EQ( v.status, UA_STATUSCODE_GOOD ) << field.Name << ": " << UAException::Message( v.status );
				EXPECT_EQ( serialize(v.ValueJson(nullptr)), serialize(field.Value) ) << field.Name;
				EXPECT_EQ( SourceTime(v), optional<TimePoint>{t0+20s} ) << field.Name;
			}
			EXPECT_EQ( lacked, 16u ) << "the limits, the parameter set's name and the compensated angle";
			EXPECT_EQ( Time(Of(read, "Result.Time")), optional<TimePoint>{t0+20s} );
			EXPECT_EQ( Of(read, "Result.PsetChangeTime").status, NotSupported );
			EXPECT_EQ( Counters(read), (Counts{3, 3, 0}) );
		}

		station.Edit( []( Controller::Script& script ){ script.Revisions[64] = 1; } );
		ASSERT_TRUE( Sever(station, row) );
		station.Push( rundown(505, 30s) );
		station.Keep( rundown(506, 40s), 8, "Station 8" );
		Mend( station );
		ASSERT_TRUE( WaitResults(row, 5) );
		{
			let read = Variables( row );
			EXPECT_EQ( ResultId(read), optional<uint>{506} ) << "no station id below revision 7:  every result is the connection's";
			for( let& field : Fields() ){
				let& v = Of( read, Path(field.Name) );
				let since = storedSince( field.Name );
				EXPECT_EQ( v.status, since==1 ? UA_STATUSCODE_GOOD : NotSupported ) << field.Name << ": " << UAException::Message( v.status );
				EXPECT_EQ( SourceTime(v), optional<TimePoint>{t0+(since==1 ? 40s : since ? 30s : 10s)} ) << field.Name;
			}
			EXPECT_TRUE( OpText(Op(row, "Revisions")).contains("0064:1") ) << OpText( Op(row, "Revisions") );
		}

		station.Push( rundown(508, 50s) );
		ASSERT_TRUE( WaitResults(row, 6) );
		let pushed = Variables( row );
		for( let& field : Fields() ){
			let& v = Of( pushed, Path(field.Name) );
			EXPECT_EQ( v.status, UA_STATUSCODE_GOOD ) << field.Name << ": " << UAException::Message( v.status );
			EXPECT_EQ( SourceTime(v), optional<TimePoint>{t0+50s} ) << field.Name;
		}
		EXPECT_EQ( Counters(pushed), (Counts{6, 6, 0}) ) << "below revision 3 a result names no type";
	}

	//A result that comes both ways, found by the walk and pushed by the subscription, is written and counted once, as it was
	//pushed, which carries its limits.  Nothing was held when the walk began, so it began at id 0, the latest.
	TEST_F( DriverBackfillTests, AResultFoundAndPushedIsWrittenOnce ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-once", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const TimePoint t0{ ceil<seconds>(Clock::now()) };
		let rundown = [&]( uint32_t id, seconds after ){ return Rundown( id, Local(t0+after) ); };
		station.Push( rundown(200, 0s) );
		ASSERT_TRUE( WaitResults(row, 1) );

		ASSERT_TRUE( Sever(station, row) );
		station.Push( rundown(202, 10s) );
		station.Push( rundown(204, 20s) );
		let before = station.Frames().size();
		Mend( station );
		station.Delay( 300ms, 4 );//MID 0002, MID 0060's MID 0005, MID 0081 and the first MID 0065, which is the latest's.
		ASSERT_TRUE( WaitUntil([&]{ return Asked( station, before ).size()>0; }) ) << "no MID 0064";
		station.Push( rundown(204, 20s) );//pushed while its MID 0065 is held:  a rundown at the reconnect.
		ASSERT_TRUE( WaitResults(row, 3) ) << OpCount( Op(row, "Results") ).value_or( 0 ) << " result(s)";

		EXPECT_EQ( Asked(station, before), (vector<uint32_t>{0, 203, 201}) );
		let read = Variables( row );
		EXPECT_EQ( ResultId(read), optional<uint>{204} );
		EXPECT_EQ( Counters(read), (Counts{3, 3, 0}) ) << "200, 202 and 204, each once";
		let& limit = Of( read, "Result.TorqueMax" );
		EXPECT_EQ( limit.status, UA_STATUSCODE_GOOD ) << "as it was pushed:  " << UAException::Message( limit.status );
		EXPECT_EQ( SourceTime(limit), optional<TimePoint>{t0+20s} );
		EXPECT_EQ( OpCount(Op(row, "Results")), optional<uint>{3} );
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{0} );
	}

	//The suite's block lets a recovery send six MID 0064s.  Eight results made while the link is down are two more than that:
	//the six latest are written, oldest first, `_Op.LostResults` counts the gap as one, and a warning names the last result
	//written and the oldest the walk reached.  The next outage's walk reaches the last id, and loses nothing, its one result
	//made in the last one's second.  The server's deepest item queue is asked at twice the bound, which the library leaves at
	//open62541's 100 where it is less.
	TEST_F( DriverBackfillTests, AWalkThatRunsOutCountsLostResults ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-short", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		{
			auto settings = DriverServer::Settings( ServerCnnctn::KindOpenProtocol, row.Slug, DriverHost::Find(row.Slug)->Port() );
			EXPECT_FALSE( settings.Limit.MaxQueueSize );
			OpenProtocolDriver::Make( RowOf(row.Id), SRCE_CUR )->Configure( settings );
			EXPECT_EQ( settings.Limit.MaxQueueSize, optional<uint>{12} ) << "twice /gateway/drivers/openProtocol/maxBackfill, the suite's 6";
		}
		const ResultGroup group{ row, "driver-short", {{"Result.TighteningId", {}}} };
		const TimePoint t0{ ceil<seconds>(Clock::now()) };
		let rundown = [&]( uint32_t id ){ return Rundown( id, Local(t0+seconds{id-300}) ); };
		station.Push( rundown(300) );
		ASSERT_TRUE( WaitResults(row, 1) );

		const Logging::Capture warnings{ OpenProtocol::LogTag, ELogLevel::Warning };
		ASSERT_TRUE( Sever(station, row) );
		for( uint32_t id=301; id<=308; ++id )
			station.Push( rundown(id) );
		let before = station.Frames().size();
		Mend( station );
		ASSERT_TRUE( WaitResults(row, 7) ) << OpCount( Op(row, "Results") ).value_or( 0 ) << " result(s)";

		EXPECT_EQ( Asked(station, before), (vector<uint32_t>{0, 307, 306, 305, 304, 303}) ) << "six, and no seventh";
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{1} ) << "301 and 302, the gap's unknown size as one";
		let warned = ShortWalks( warnings, row );
		ASSERT_EQ( warned.size(), 1u );
		EXPECT_TRUE( warned.front().contains("result 300") && warned.front().contains("result 303") && warned.front().contains("maxBackfill") ) << warned.front();
		EXPECT_EQ( Counters(Variables(row)), (Counts{7, 7, 0}) );
		ASSERT_NO_THROW( WaitStored(group.Host, group.Members[0], t0+1s, 6) );
		let ids = Records( group, 0, t0+1s );
		ASSERT_EQ( ids.size(), 6u );
		for( uint i=0; i<ids.size(); ++i ){
			EXPECT_EQ( ids[i].Get<UA_UInt32>(0), 303+i ) << "oldest first";
			EXPECT_EQ( SourceTime(ids[i]), optional<TimePoint>{t0+seconds{3+i}} );
		}

		ASSERT_TRUE( Sever(station, row) );
		station.Push( Rundown(310, Local(t0+8s)) );//in the last result's second:  a higher id is later, whatever its time.
		Mend( station );
		ASSERT_TRUE( WaitResults(row, 8) );
		let last = Variables( row );
		EXPECT_EQ( ResultId(last), optional<uint>{310} );
		EXPECT_EQ( SourceTime(Of(last, "Result.TighteningId")), optional<TimePoint>{t0+8s+1ms} );
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{1} ) << "a walk that reached the last id";
		EXPECT_EQ( ShortWalks(warnings, row).size(), 1u );
	}

	//Six results made while the link is down, under the ids after the last written, take all six of the suite's MID 0064s.
	//The last finds the result after the last written, so the walk is whole:  nothing is asked for the last id, and nothing
	//counted lost.
	TEST_F( DriverBackfillTests, AWalkWholeAtItsLastRequestLosesNothing ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-whole", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const TimePoint t0{ ceil<seconds>(Clock::now()) };
		let rundown = [&]( uint32_t id ){ return Rundown( id, Local(t0+seconds{id-300}) ); };
		station.Push( rundown(300) );
		ASSERT_TRUE( WaitResults(row, 1) );

		const Logging::Capture warnings{ OpenProtocol::LogTag, ELogLevel::Warning };
		ASSERT_TRUE( Sever(station, row) );
		for( uint32_t id=301; id<=306; ++id )
			station.Push( rundown(id) );
		let before = station.Frames().size();
		Mend( station );
		ASSERT_TRUE( WaitResults(row, 7) ) << OpCount( Op(row, "Results") ).value_or( 0 ) << " result(s)";

		EXPECT_EQ( Asked(station, before), (vector<uint32_t>{0, 305, 304, 303, 302, 301}) ) << "six, the last finding 301";
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{0} );
		EXPECT_TRUE( ShortWalks(warnings, row).empty() );
		let read = Variables( row );
		EXPECT_EQ( ResultId(read), optional<uint>{306} );
		EXPECT_EQ( Counters(read), (Counts{7, 7, 0}) );
	}

	//A result whose tightening id is 0, a batch's increment here, is no last id:  MID 0064 reads 0 as the latest, and every id
	//the walk finds is above it.  The walk ends at the tightening before it, and nothing is written twice.
	TEST_F( DriverBackfillTests, AnIdOf0IsNoLastId ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-id0", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const TimePoint t0{ ceil<seconds>(Clock::now()) };
		station.Push( Rundown(300, Local(t0)) );
		auto increment = Rundown( 0, Local(t0+5s) );
		increment.ResultType = 3;
		station.Push( move(increment) );
		ASSERT_TRUE( WaitResults(row, 2) );

		const Logging::Capture warnings{ OpenProtocol::LogTag, ELogLevel::Warning };
		ASSERT_TRUE( Sever(station, row) );
		station.Push( Rundown(302, Local(t0+10s)) );
		let before = station.Frames().size();
		Mend( station );
		ASSERT_TRUE( WaitResults(row, 3) ) << OpCount( Op(row, "Results") ).value_or( 0 ) << " result(s)";

		EXPECT_EQ( Asked(station, before), (vector<uint32_t>{0, 301}) ) << "ended at 300, the last id";
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{0} );
		EXPECT_TRUE( ShortWalks(warnings, row).empty() );
		let read = Variables( row );
		EXPECT_EQ( ResultId(read), optional<uint>{302} );
		EXPECT_EQ( Counters(read), (Counts{2, 2, 0}) ) << "300 and 302, each once, and the increment no tightening";
		EXPECT_EQ( OpCount(Op(row, "Results")), optional<uint>{3} );
	}

	//A controller that answers MID 0064 with MID 0004 ends the walk at its first request:  the refusal in `_Op.LastError`, the
	//outage's results counted lost as one, the link standing and the results that follow written.
	TEST_F( DriverBackfillTests, AControllerThatRefusesMid0064EndsTheWalk ){
		auto refusing = Station();
		refusing.Refuses[64] = 99;
		Controller station{ *Executor(), ControllerPort(), move(refusing) };
		const DriverRow row{ "driver-refused", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const TimePoint t0{ ceil<seconds>(Clock::now()) };
		station.Push( Rundown(400, Local(t0)) );
		ASSERT_TRUE( WaitResults(row, 1) );

		const Logging::Capture warnings{ OpenProtocol::LogTag, ELogLevel::Warning };
		ASSERT_TRUE( Sever(station, row) );
		station.Push( Rundown(402, Local(t0+10s)) );
		let before = station.Frames().size();
		Mend( station );
		ASSERT_TRUE( WaitLost(row, 1) ) << OpCount( Op(row, "LostResults") ).value_or( 0 );
		EXPECT_EQ( Asked(station, before), (vector<uint32_t>{0}) ) << "asked once";
		EXPECT_TRUE( OpText(Op(row, "LastError")).contains("MID 0064") ) << OpText( Op(row, "LastError") );
		EXPECT_EQ( OpFlag(Op(row, "Connected")), true ) << "the link stands";
		let warned = ShortWalks( warnings, row );
		ASSERT_EQ( warned.size(), 1u );
		EXPECT_TRUE( warned.front().contains("result 400") && warned.front().contains("reached no result") && warned.front().contains("MID 0064") ) << warned.front();
		EXPECT_EQ( OpCount(Op(row, "Results")), optional<uint>{1} ) << "nothing found";

		station.Push( Rundown(404, Local(t0+20s)) );
		ASSERT_TRUE( WaitResults(row, 2) );
		EXPECT_EQ( ResultId(Variables(row)), optional<uint>{404} );
	}

	//A row's first connection in a run fills nothing, though the controller's store holds results:  there is no last id to
	//walk back to.  Nor does a link that returns before any result is written.  Once one is, a link that returns asks for the
	//latest, which is the last written, and nothing is filled.
	TEST_F( DriverBackfillTests, AFirstConnectionFillsNothing ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		let made = Clock::now()-1h;
		for( uint32_t id=1; id<=3; ++id )
			station.Push( Rundown(id, Local(made+minutes{id})) );//kept, with no client to push to.
		const DriverRow row{ "driver-first", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		ASSERT_TRUE( WaitOp(row, "Skew", []( let& v ){ return !v.status; }) ) << "the clock's read, which a walk follows";
		EXPECT_TRUE( Asked(station).empty() );
		EXPECT_EQ( OpCount(Op(row, "Results")), optional<uint>{0} );
		EXPECT_EQ( Of(Variables(row), "Result.TighteningId").status, Waiting );

		auto before = station.Frames().size();
		station.Drop();
		ASSERT_TRUE( WaitUntil([&]{ return RevisionsOf( station, 80, before ).size()>0; }) ) << "the link never came back";
		station.Push( Rundown(4, Local(Clock::now())) );
		ASSERT_TRUE( WaitResults(row, 1) );
		EXPECT_TRUE( Asked(station).empty() ) << "nothing written before the link went";

		before = station.Frames().size();
		station.Drop();
		ASSERT_TRUE( WaitUntil([&]{ return Asked( station, before ).size()>0; }) ) << "no MID 0064";
		station.Push( Rundown(5, Local(Clock::now()+1s)) );
		ASSERT_TRUE( WaitResults(row, 2) );
		EXPECT_EQ( Asked(station, before), (vector<uint32_t>{0}) ) << "the latest is the last written";
		EXPECT_EQ( ResultId(Variables(row)), optional<uint>{5} );
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{0} );
	}

	//A link lost during the walk writes nothing of it:  what it had found, and the result it held, are the controller's still,
	//and the next connection walks again from the latest, so the held result comes back as a found one, in its place.
	TEST_F( DriverBackfillTests, ALinkLostDuringTheWalkIsWalkedAgain ){
		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-again", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const ResultGroup group{ row, "driver-again", {{"Result.TighteningId", {}}} };
		const TimePoint t0{ ceil<seconds>(Clock::now()) };
		let rundown = [&]( uint32_t id ){ return Rundown( id, Local(t0+seconds{id-800}) ); };
		station.Push( rundown(800) );
		ASSERT_TRUE( WaitResults(row, 1) );

		ASSERT_TRUE( Sever(station, row) );
		for( uint32_t id : {802u, 803u, 804u} )
			station.Push( rundown(id) );
		auto before = station.Frames().size();
		Mend( station );
		station.Delay( 300ms, 4 );//MID 0002, MID 0060's MID 0005, MID 0081 and the first MID 0065.
		ASSERT_TRUE( WaitUntil([&]{ return Asked( station, before ).size()>0; }) ) << "no MID 0064";
		station.Push( rundown(806) );//held, while the walk's first reply is.
		ASSERT_TRUE( WaitUntil([&]{ return RevisionsOf( station, 9997, before ).size()>=3; }) ) << "the pushed result never reached the driver";//MID 0060's MID 0005, MID 0081 and the push, each acknowledged at link level.
		before = station.Frames().size();
		station.Drop();
		ASSERT_TRUE( WaitResults(row, 5) ) << OpCount( Op(row, "Results") ).value_or( 0 ) << " result(s)";

		EXPECT_EQ( Asked(station, before), (vector<uint32_t>{0, 805, 803, 802, 801}) ) << "the next connection's, from the latest";
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{0} );
		let read = Variables( row );
		EXPECT_EQ( Counters(read), (Counts{5, 5, 0}) );
		EXPECT_EQ( Of(read, "Result.TorqueMax").status, NotSupported ) << "806 came back as a MID 0065";
		ASSERT_NO_THROW( WaitStored(group.Host, group.Members[0], t0+1s, 4) );
		let ids = Records( group, 0, t0+1s );
		ASSERT_EQ( ids.size(), 4u );
		const array<uint32_t,4> written{ 802, 803, 804, 806 };
		for( uint i=0; i<written.size(); ++i ){
			EXPECT_EQ( ids[i].Get<UA_UInt32>(0), written[i] ) << "in order, none written at the loss";
			EXPECT_EQ( SourceTime(ids[i]), optional<TimePoint>{t0+seconds{written[i]-800}} );
		}
	}

	//The ids run up, so the walk ends at a lower id than the last written, whichever it is:  a store that no longer holds the
	//last id loses nothing.  But a lower id timed after the last write is of a run of ids that began again, a controller whose
	//store was cleared:  its results are written by their times, and the walk ends short at id 1, with nothing before it to ask
	//for.
	TEST_F( DriverBackfillTests, ALowerIdIsFilledWhereItIsTimedAfterTheLastWrite ){
		optional<Controller> station;
		station.emplace( *Executor(), ControllerPort(), Station() );
		const DriverRow row{ "driver-ids", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		const TimePoint t0{ ceil<seconds>(Clock::now()) };
		station->Push( Rundown(700, Local(t0)) );
		ASSERT_TRUE( WaitResults(row, 1) );
		//Another store behind the same port, the link down until it holds its results.
		let replace = [&]{
			station.reset();
			EXPECT_TRUE( WaitConnected(row, false) );
			auto severed = Station();
			severed.Refuses[1] = 16;
			station.emplace( *Executor(), ControllerPort(), move(severed) );
		};

		const Logging::Capture warnings{ OpenProtocol::LogTag, ELogLevel::Warning };
		replace();
		station->Push( Rundown(650, Local(t0-1min)) );
		station->Push( Rundown(705, Local(t0+10s)) );
		Mend( *station );
		ASSERT_TRUE( WaitResults(row, 2) );
		EXPECT_EQ( Asked(*station), (vector<uint32_t>{0, 704}) ) << "650, before the last written:  the walk's end";
		EXPECT_EQ( ResultId(Variables(row)), optional<uint>{705} );
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{0} );
		EXPECT_TRUE( ShortWalks(warnings, row).empty() );

		replace();
		station->Push( Rundown(1, Local(t0+20s)) );
		station->Push( Rundown(2, Local(t0+30s)) );
		Mend( *station );
		ASSERT_TRUE( WaitResults(row, 4) );
		EXPECT_EQ( Asked(*station), (vector<uint32_t>{0, 1}) );
		let read = Variables( row );
		EXPECT_EQ( ResultId(read), optional<uint>{2} );
		EXPECT_EQ( SourceTime(Of(read, "Result.TighteningId")), optional<TimePoint>{t0+30s} );
		EXPECT_EQ( Counters(read), (Counts{4, 4, 0}) );
		EXPECT_EQ( OpCount(Op(row, "LostResults")), optional<uint>{1} ) << "what lay between 705 and the new run is not known";
		let warned = ShortWalks( warnings, row );
		ASSERT_EQ( warned.size(), 1u );
		EXPECT_TRUE( warned.front().contains("result 705") && warned.front().contains("reached result 1") && warned.front().contains("id 1") ) << warned.front();
	}

	//An hour that comes twice, New York's on 1 November 2026, in a result fetched from the store is the later pass where that
	//is not after the result made after it, however long ago both were:  the nearer pass is a pushed result's reading.  Two
	//results of an outage, the first pass's 01:30 and the second's 01:10, are read so, and neither takes the other's time.
	TEST_F( DriverBackfillTests, AStoredHourThatComesTwiceIsThePassBeforeTheNextResult ){
		let& zone = *std::chrono::locate_zone( "America/New_York" );
		let local = LocalTime{ local_days{year{2026}/11/1}+1h+30min };
		const TimePoint first{ sys_days{year{2026}/11/1}+5h+30min }, second{ first+1h };//EDT's, then EST's.
		EXPECT_EQ( TighteningResult::ToSys(local, zone, second+12*24h, true), second ) << "the later pass, long before the next result";
		EXPECT_EQ( TighteningResult::ToSys(local, zone, second, true), second ) << "in the next result's second";
		EXPECT_EQ( TighteningResult::ToSys(local, zone, second-10min, true), first ) << "the later pass would follow the next result";
		EXPECT_EQ( TighteningResult::ToSys(local, zone, second-10min), second ) << "a pushed result's, the nearer";
		EXPECT_EQ( TighteningResult::ToSys(LocalTime{local_days{year{2026}/7/4}+10h}, zone, first, true), TimePoint{sys_days{year{2026}/7/4}+14h} ) << "an hour that comes once";

		Controller station{ *Executor(), ControllerPort(), Station() };
		const DriverRow row{ "driver-stored-hour", NewYork };
		ASSERT_TRUE( WaitRevision(row, 7) );
		station.Push( Rundown(600, LocalTime{local_days{year{2026}/11/1}+50min}) );
		ASSERT_TRUE( WaitResults(row, 1) );
		ASSERT_TRUE( Sever(station, row) );
		station.Push( Rundown(602, local) );
		station.Push( Rundown(603, LocalTime{local_days{year{2026}/11/1}+1h+10min}) );
		Mend( station );
		ASSERT_TRUE( WaitResults(row, 3) );
		let read = Variables( row );
		EXPECT_EQ( ResultId(read), optional<uint>{603} );
		EXPECT_EQ( SourceTime(Of(read, "Result.TighteningId")), optional<TimePoint>{first+40min} ) << "01:10 EST, not a millisecond past a 602 read an hour late";
		EXPECT_EQ( Time(Of(read, "Result.Time")), optional<TimePoint>{first+40min} );
	}
}