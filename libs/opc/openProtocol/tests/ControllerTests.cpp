#include "Rig.h"

//The controller's side of the codec, as the later phases' suites and the emulator script it:  the result store behind
//MID 0064, what it pushes on a subscription and on a change, its clock, session control and the tool lock it refuses.
namespace Jde::Opc::OpenProtocol::Tests{
	Ω rundown( uint32_t id )ι->Mid0061{
		auto result = CaseOf<Mid0061>();
		result.TighteningId = id;
		return result;
	}
	Ω refusedWith( const Session::Reply& reply )ι->uint16_t{ return reply || reply.error().Kind!=EFailure::Refused ? 0 : reply.error().Code; }

	//§5.8.5 and Table 121:  a result by its tightening id, 0 the latest.  The ids are one run across the controller's
	//stations, so a station's own are not consecutive, and an id no result has answers the result before it, which is how
	//the PF6000 appendix's walk gets back through them.
	TEST( ControllerTests, OldResultsById ){
		Rig rig{ Unnumbered() };
		ASSERT_TRUE( rig.Start() );
		EXPECT_EQ( refusedWith(rig.Ask(Mid0064{.TighteningId=0})), 15 ) << "Tightening ID requested not found";
		rig.Station.Push( rundown(10) );
		rig.Station.Keep( rundown(11), 2, "Station 2" );
		auto latest = CaseOf<Mid0061>();
		latest.TighteningId.reset();
		latest.Vin = "LATEST";
		rig.Station.Push( latest );//takes the next of the run, 12.
		let old = [&]( uint32_t id )ι->Mid0065{
			let reply = rig.Ask( Mid0064{.TighteningId=id} );
			EXPECT_TRUE( reply ) << id << ":  " << Why( reply );
			return reply ? As<Mid0065>( *reply ) : Mid0065{};
		};
		let own = CaseOf<Mid0002>();
		EXPECT_EQ( old(0).TighteningId, 12u );
		EXPECT_EQ( old(0).Vin, "LATEST" );
		EXPECT_EQ( old(10).TighteningId, 10u );
		EXPECT_EQ( old(10).StationId, own.StationId ) << "revision 7's station, the one MID 0002 named";
		EXPECT_EQ( old(10).StationName, own.StationName );
		EXPECT_EQ( old(11).StationId, 2u ) << "another station's result, under the same run of ids";
		EXPECT_EQ( old(11).StationName, "Station 2" );
		EXPECT_EQ( old(4000).TighteningId, 12u ) << "the result before an id none has";
		EXPECT_EQ( refusedWith(rig.Ask(Mid0064{.TighteningId=9})), 15 ) << "nothing at or before it";
		let whole = old( 10 );
		let pushed = rundown( 10 );
		EXPECT_EQ( whole.Torque, pushed.Torque );
		EXPECT_EQ( whole.Time, pushed.Time );
		EXPECT_EQ( whole.TighteningErrorStatus2, pushed.TighteningErrorStatus2 ) << "revision 6's";
	}

	//A result made while no client subscribes is kept, which is what a walk recovers after a lost link.
	TEST( ControllerTests, ResultsKeptWhileNoOneSubscribes ){
		Rig rig{ Unnumbered() };
		rig.Station.Push( rundown(7) );
		ASSERT_TRUE( rig.Start() );
		ASSERT_TRUE( rig.Ask(Mid0060{}) );
		let old = rig.Ask( Mid0064{.TighteningId=0} );
		ASSERT_TRUE( old ) << Why( old );
		EXPECT_EQ( As<Mid0065>(*old).TighteningId, 7u );
		EXPECT_TRUE( rig.Told.Pushes().empty() ) << "nothing is pushed again on its own";
	}

	//What is selected and an active alarm go out as their subscription is accepted when the script says so, and on each
	//change either way.
	TEST( ControllerTests, PushesOnSubscribeWhenScripted ){
		for( let onSubscribe : {true, false} ){
			auto script = Unnumbered();
			script.PushOnSubscribe = onSubscribe;
			Rig rig{ move(script) };
			ASSERT_TRUE( rig.Start() );
			rig.Station.Select( CaseOf<Mid0015>() );
			rig.Station.Select( CaseOf<Mid0035>() );
			rig.Station.Select( CaseOf<Mid0052>() );
			rig.Station.Raise( CaseOf<Mid0071>() );
			for( let& subscribe : {Body{Mid0014{}}, Body{Mid0034{}}, Body{Mid0051{}}, Body{Mid0070{}}} )
				ASSERT_TRUE( rig.Ask(subscribe) );
			let time = rig.Ask( Mid0080{} );//what the subscriptions pushed is here before its answer.
			ASSERT_TRUE( time ) << Why( time );
			EXPECT_EQ( rig.Told.Pushes().size(), onSubscribe ? 4u : 0u );
			auto vin = CaseOf<Mid0052>();
			vin.Vin = "CHANGED";
			rig.Station.Select( vin );
			ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==(onSubscribe ? 5u : 1u); }) );
			EXPECT_EQ( As<Mid0052>(rig.Told.Pushes().back()).Vin, "CHANGED" );
		}
	}

	//A subscription asked twice and one ended twice are refused with Table 23's codes.
	TEST( ControllerTests, SubscriptionsRefusedByTheirCodes ){
		Rig rig;
		ASSERT_TRUE( rig.Start() );
		struct Topic{ Body Subscribe; Body Unsubscribe; uint16_t Exists; uint16_t Missing; };
		for( let& topic : {Topic{Mid0014{}, Mid0017{}, 13, 14}, Topic{Mid0034{}, Mid0037{}, 18, 19}, Topic{Mid0051{}, Mid0054{}, 6, 7},
			Topic{Mid0060{}, Mid0063{}, 9, 10}, Topic{Mid0070{}, Mid0073{}, 11, 12}} ){
			let mid = MidOf( topic.Subscribe );
			EXPECT_EQ( refusedWith(rig.Ask(topic.Unsubscribe)), topic.Missing ) << mid;
			EXPECT_TRUE( rig.Ask(topic.Subscribe) ) << mid;
			EXPECT_EQ( refusedWith(rig.Ask(topic.Subscribe)), topic.Exists ) << mid;
			EXPECT_TRUE( rig.Ask(topic.Unsubscribe) ) << mid;
			EXPECT_TRUE( rig.Ask(topic.Subscribe) ) << mid << " again";
		}
		rig.Station.Push( CaseOf<Mid0061>() );
		EXPECT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==1; }) );
	}

	//The commands change what the station holds, and its subscribers hear.  MID 0038 at 2 steps down to a station with 1.
	TEST( ControllerTests, Commands ){
		auto script = Unnumbered();
		script.Revisions = { {Mid0038::Id, 1} };
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		for( let& subscribe : {Body{Mid0014{}}, Body{Mid0034{}}, Body{Mid0051{}}} )
			ASSERT_TRUE( rig.Ask(subscribe) );
		EXPECT_TRUE( rig.Station.ToolEnabled() );
		ASSERT_TRUE( rig.Ask(Mid0042{}) );
		EXPECT_FALSE( rig.Station.ToolEnabled() );
		ASSERT_TRUE( rig.Ask(Mid0043{}) );
		EXPECT_TRUE( rig.Station.ToolEnabled() );
		ASSERT_TRUE( rig.Ask(Mid0018{.Pset=7}) );
		ASSERT_TRUE( rig.Ask(Mid0038{.JobId=4}) );
		ASSERT_TRUE( rig.Ask(Mid0050{.Vin="VIN0050"}) );
		ASSERT_TRUE( WaitUntil([&]{ return rig.Told.Pushes().size()==3; }) );
		let pushes = rig.Told.Pushes();
		EXPECT_EQ( As<Mid0015>(pushes[0]).Pset, 7 );
		EXPECT_EQ( As<Mid0035>(pushes[1]).JobId, 4 );
		EXPECT_EQ( As<Mid0052>(pushes[2]).Vin, "VIN0050" );
		EXPECT_EQ( Revisions(rig.Got(Mid0038::Id)), (vector<uint16_t>{2, 1}) );
		EXPECT_EQ( rig.Revision(Mid0038::Id), 1 ) << "the range a job id has from here on";
		EXPECT_EQ( refusedWith(rig.Ask(Mid0038{.JobId=100})), 0 ) << "refused by the codec at revision 1's two digits, not by the station";
		EXPECT_EQ( rig.Got(Mid0038::Id).size(), 2u );
	}

	//A MID a case names is refused with the code it gives, as a later phase's command against a station that cannot.
	TEST( ControllerTests, RefusesWhatTheScriptNames ){
		auto script = Scripted();
		script.Refuses = { {Mid0018::Id, 2}, {Mid0050::Id, 8} };
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		let reply = rig.Ask( Mid0018{.Pset=5} );
		ASSERT_FALSE( reply );
		EXPECT_EQ( reply.error().Code, 2 );
		EXPECT_EQ( reply.error().What, "MID 0004 to MID 0018:  code 2, Parameter set ID not present." );
		EXPECT_EQ( refusedWith(rig.Ask(Mid0050{.Vin="V"})), 8 );
		rig.Station.Edit( []( Controller::Script& edited ){ edited.Refuses.clear(); } );
		EXPECT_TRUE( rig.Ask(Mid0018{.Pset=5}) );
	}

	//MID 0081 is the station's clock, local to the script's zone and ahead by its skew.
	TEST( ControllerTests, ClockSkewed ){
		auto script = Scripted();
		script.Zone = locate_zone( "UTC" );
		script.Skew = 1h;
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		let before = floor<seconds>( Clock::now() );
		let reply = rig.Ask( Mid0080{} );
		ASSERT_TRUE( reply ) << Why( reply );
		let time = As<Mid0081>( *reply ).Time;
		ASSERT_TRUE( time );
		let ahead = duration_cast<seconds>( time->time_since_epoch()-before.time_since_epoch() ).count();
		EXPECT_GE( ahead, 3600 );
		EXPECT_LE( ahead, 3610 );
	}

	//§3.3.4:  a second Actor is refused with code 35, and a Viewer's commands with 92 while its subscriptions and requests stand.
	TEST( ControllerTests, SecondActorRefused ){
		auto script = Scripted();
		script.OneActor = true;
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		Heard told;
		Session second{ boost::asio::make_strand(*Executor()), "127.0.0.1", rig.Station.Port(), Quick(), told.Events() };
		second.Start();
		ASSERT_TRUE( WaitUntil([&]{ return !told.Losses().empty(); }) );
		let refused = told.Losses()[0];
		EXPECT_EQ( refused.Kind, EFailure::Refused );
		EXPECT_EQ( refused.Mid, 1 );
		EXPECT_EQ( refused.Code, 35 );
		EXPECT_EQ( refused.What, "MID 0004 to MID 0001:  code 35, Other master client already connected." );
		EXPECT_TRUE( told.Connections().empty() );
		EXPECT_TRUE( rig.Stop() );
		EXPECT_TRUE( WaitUntil([&]{ return !told.Connections().empty(); }) ) << "the Actor gone, the next attempt starts";
		EXPECT_TRUE( Stop(second) );
	}
	TEST( ControllerTests, ViewersCommandsRefused ){
		auto script = Scripted();
		script.Viewer = true;
		Rig rig{ move(script) };
		ASSERT_TRUE( rig.Start() );
		for( let& command : {Body{Mid0018{.Pset=1}}, Body{Mid0038{.JobId=1}}, Body{Mid0042{}}, Body{Mid0043{}}, Body{Mid0050{.Vin="V"}}} )
			EXPECT_EQ( refusedWith(rig.Ask(command)), 92 ) << MidOf( command );
		EXPECT_TRUE( rig.Ask(Mid0060{}) );
		EXPECT_TRUE( rig.Ask(Mid0080{}) );
		EXPECT_TRUE( rig.Station.ToolEnabled() );
		EXPECT_TRUE( rig.Told.Losses().empty() );
	}

	//MID 0001 revision 8 may ask for the tool lock at disconnection (Table 12), which would stop a line whenever the
	//gateway restarts:  the station refuses the revision as it does every one past the codec's, and says it was asked.
	TEST( ControllerTests, ToolLockRefused ){
		Controller station{ *Executor(), 0, Scripted() };
		RawClient client{ station.Port() };
		client.Send( Frame({.Mid=60, .Revision=7}, {}) );
		EXPECT_FALSE( client.Read(200ms) ) << "nothing is answered before MID 0001 (§5.2.1)";
		client.Send( Frame({.Mid=1, .Revision=8}, "0100200030000") );
		auto reply = client.Read();
		ASSERT_TRUE( reply );
		EXPECT_FALSE( station.ToolLockAsked() );
		EXPECT_TRUE( (Decode(*reply).value().Data==Body{Mid0004{.Mid=1, .Code=97}}) );
		client.Send( Frame({.Mid=1, .Revision=8}, "0100210030000") );
		reply = client.Read();
		ASSERT_TRUE( reply );
		EXPECT_TRUE( (Decode(*reply).value().Data==Body{Mid0004{.Mid=1, .Code=97}}) );
		EXPECT_TRUE( station.ToolLockAsked() );
		client.Send( Frame({.Mid=1, .Revision=6}, {}) );
		reply = client.Read();
		ASSERT_TRUE( reply );
		let started = Decode( *reply );
		ASSERT_TRUE( started ) << started.error().What;
		EXPECT_EQ( started->Head.Revision, 6 );
		EXPECT_TRUE( started->Data==Body{CaseOf<Mid0002>()} );
		client.Send( Frame({.Mid=1, .Revision=6}, {}) );
		reply = client.Read();
		ASSERT_TRUE( reply );
		EXPECT_TRUE( (Decode(*reply).value().Data==Body{Mid0004{.Mid=1, .Code=96}}) ) << "Client already connected";
		EXPECT_EQ( station.Clients(), 1u );
	}

	//§3.8:  a frame is its length's bytes however the stream cuts them, one frame in two writes and two in one.
	TEST( ControllerTests, FramesByTheirLengths ){
		Controller station{ *Executor(), 0, Unnumbered() };
		RawClient client{ station.Port() };
		let start = Frame( {.Mid=1, .Revision=6}, {} );
		client.Send( sv{start}.substr(0, 7) );
		EXPECT_FALSE( client.Read(100ms) );
		client.Send( sv{start}.substr(7) );
		auto reply = client.Read();
		ASSERT_TRUE( reply );
		EXPECT_EQ( Decode(*reply).value().Head.Mid, 2 );
		client.Send( Frame({.Mid=80}, {})+Frame({.Mid=9999}, {}) );
		reply = client.Read();
		ASSERT_TRUE( reply );
		EXPECT_EQ( Decode(*reply).value().Head.Mid, 81 );
		reply = client.Read();
		ASSERT_TRUE( reply );
		EXPECT_EQ( Decode(*reply).value().Head.Mid, 9999 );
	}

	TEST( ControllerTests, PortTaken ){
		Controller station{ *Executor(), 0, Scripted() };
		EXPECT_THROW( (Controller{*Executor(), station.Port(), Scripted()}), Exception );
	}
}