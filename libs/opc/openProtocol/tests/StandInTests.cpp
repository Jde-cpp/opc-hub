#include <jde/fwk/process/process.h>
#include <jde/opc/openProtocol/tests/StandIn.h>
#include "Rig.h"

//docs/open-protocol/plan.md Phase 10b (#473):  the bench controller's stand-in, which the bench's checks are run against
//until the controller is there - the gateway's DriverControllerTests, and the bench client through Serves below.  That it
//is the PF6000 its appendix describes is checked here, with the library's own session.
namespace Jde::Opc::OpenProtocol::Tests{
	//A session to one of its stations.
	struct Dialed{
		Dialed( uint16_t port ): Ran{ boost::asio::make_strand(*Executor()) }, Client{ Ran, "127.0.0.1", port, Quick(), Told.Events() }{}
		~Dialed(){ Stop( Client ); }
		α Start()ι->bool{
			Client.Start();
			return WaitUntil( [&]{ return !Told.Connections().empty(); } );
		}
		α Ask( Body request )ι->Session::Reply{ return Tests::Ask( Client, move(request) ); }
		α Revision( uint16_t mid )ι->uint16_t{
			std::promise<uint16_t> settled;
			boost::asio::post( Ran, [&]{ settled.set_value( Client.Revision(mid) ); } );
			return settled.get_future().get();
		}
		α Results()Ι->vector<uint32_t>{
			vector<uint32_t> ids;
			for( let& push : Told.Pushes() ){
				if( let result = get_if<Mid0061>(&push.Data) )
					ids.push_back( result->TighteningId.value_or(0) );
			}
			return ids;
		}
		Heard Told;
		Strand Ran;
		Session Client;
	};
	Ω now()ι->LocalTime{ return LocalTime{ floor<seconds>(current_zone()->to_local(Clock::now())) }; }

	//Each station answers MID 0001 at 6 with its own station id under one controller's serial number, and numbers its
	//messages.  A subscription to the selected parameter set steps down to revision 1 and one to the job info to 4, each
	//refusal code 74, and the rest are taken as the codec asks them (PF6000 appendix, Table 1).
	TEST( StandInTests, ItsStationsAreAPf6000s ){
		StandIn bench{ *Executor() };
		optional<string> serial;
		for( uint station=0; station<2; ++station ){
			SCOPED_TRACE( station );
			Dialed client{ bench.Port(station) };
			ASSERT_TRUE( client.Start() );
			let connection = client.Told.Connections().front();
			EXPECT_EQ( connection.Revision, 6 );
			EXPECT_TRUE( connection.LinkLevel );
			EXPECT_EQ( connection.Identity.SystemType, 3 ) << "a Power Focus 6000";
			EXPECT_EQ( connection.Identity.StationId, StandIn::StationIds[station] );
			EXPECT_EQ( serial.value_or(connection.Identity.ControllerSerialNumber), connection.Identity.ControllerSerialNumber );
			serial = connection.Identity.ControllerSerialNumber;
			for( let& request : {Body{Mid0014{}}, Body{Mid0034{}}, Body{Mid0051{}}, Body{Mid0060{}}, Body{Mid0070{}}} )
				EXPECT_TRUE( client.Ask(request) ) << MidOf( request );
			EXPECT_EQ( client.Revision(Mid0014::Id), 1 );
			EXPECT_EQ( client.Revision(Mid0034::Id), 4 );
			EXPECT_EQ( client.Revision(Mid0051::Id), 2 );
			EXPECT_EQ( client.Revision(Mid0060::Id), 7 );
			EXPECT_EQ( client.Revision(Mid0070::Id), 3 );
			vector<uint16_t> asked;
			for( let& frame : bench.Station(station).Frames() ){
				if( frame.Head.Mid==Mid0014::Id || frame.Head.Mid==Mid0034::Id )
					asked.push_back( frame.Head.Revision );
			}
			EXPECT_EQ( asked, (vector<uint16_t>{3, 2, 1, 6, 5, 4}) ) << "MID 0014, then MID 0034";
		}
	}

	//A rundown is pushed at the station that made it alone, and MID 0064 at either station walks back through both stations'
	//under one run of ids that is not consecutive, each MID 0065 naming the station that made it (appendix §2.4).
	TEST( StandInTests, ItsStationsShareOneRunOfIds ){
		StandIn bench{ *Executor() };
		Dialed first{ bench.Port(0) }, second{ bench.Port(1) };
		ASSERT_TRUE( first.Start() && second.Start() );
		ASSERT_TRUE( first.Ask(Mid0060{}) && second.Ask(Mid0060{}) );
		const array ids{ bench.Tighten(0, CaseOf<Mid0061>()), bench.Tighten(1, CaseOf<Mid0061>()), bench.Tighten(0, CaseOf<Mid0061>()) };
		EXPECT_GT( ids[1], ids[0]+1 );
		EXPECT_GT( ids[2], ids[1]+1 );
		ASSERT_TRUE( WaitUntil([&]{ return first.Results().size()==2 && second.Results().size()==1; }) );
		EXPECT_EQ( first.Results(), (vector<uint32_t>{ids[0], ids[2]}) );
		EXPECT_EQ( second.Results(), (vector<uint32_t>{ids[1]}) );
		uint32_t below{};
		for( let& [id, station] : {std::pair{ids[2], 0u}, std::pair{ids[1], 1u}, std::pair{ids[0], 0u}} ){
			let reply = second.Ask( Mid0064{.TighteningId=below ? below-1 : 0} );
			ASSERT_TRUE( reply ) << Why( reply );
			let old = As<Mid0065>( *reply );
			EXPECT_EQ( old.TighteningId, id );
			EXPECT_EQ( old.StationId, StandIn::StationIds[station] );
			EXPECT_EQ( old.StationName, StandIn::StationName(station) );
			below = id;
		}
	}

	//The stand-in on the two ports /testing/standIn names for as long as it says, for the bench client's own check
	//(.claude/skills/run-services/open-protocol-bench.py):  a rundown a second at its stations in turn, every fourth NOK, under a
	//parameter set whose name is not ASCII, with a set, a job and a VIN selected and, at the first station, an alarm raised
	//every twenty seconds and acknowledged ten seconds on.  Run by `--gtest_also_run_disabled_tests` and a filter that names it.
	TEST( StandInTests, DISABLED_Serves ){
		array<uint16_t,2> ports{ 14545, 14546 };
		if( let named = Settings::FindArray("/testing/standIn/ports"); named && named->size()==ports.size() ){
			for( uint i=0; i<ports.size(); ++i )
				ports[i] = named->at( i ).to_number<uint16_t>();
		}
		let serve = Settings::FindDuration( "/testing/standIn/serve" ).value_or( 5min );
		StandIn bench{ *Executor(), ports };
		for( uint station=0; station<2; ++station ){
			bench.Station( station ).Select( CaseOf<Mid0015>() );
			bench.Station( station ).Select( CaseOf<Mid0035>() );
			bench.Station( station ).Select( CaseOf<Mid0052>() );
		}
		std::cout << "The stand-in's stations are on 127.0.0.1:" << bench.Port( 0 ) << " and 127.0.0.1:" << bench.Port( 1 ) << " for " << duration_cast<seconds>( serve ).count() << " s." << std::endl;
		let end = steady_clock::now()+serve;
		for( uint tick=0; steady_clock::now()<end && !Process::ShuttingDown(); ++tick, std::this_thread::sleep_for(1s) ){
			auto result = CaseOf<Mid0061>();
			result.Time = now();
			result.Status = tick%4==3 ? 0 : 1;
			result.PsetName = "Größe M8";
			bench.Tighten( tick%2, move(result) );
			if( tick%20==10 ){
				auto alarm = CaseOf<Mid0071>();
				alarm.Time = now();
				bench.Station( 0 ).Raise( move(alarm) );
			}
			else if( tick%20==0 && tick )
				bench.Station( 0 ).AcknowledgeAlarm();
		}
	}
}