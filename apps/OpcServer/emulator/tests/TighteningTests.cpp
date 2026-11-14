//The emulator's tightening station (docs/open-protocol/plan.md *Phase 11*):  /emulator/tightening as TighteningSpec reads it,
//and a station on a loopback port the system picks under the library's Session, its clock the case's own Sample.
#include "../Tightening.h"
#include <future>
#include <jde/fwk/process/execution.h>
#include <jde/opc/openProtocol/Session.h>
#include <jde/tests/WaitUntil.h>

#define let const auto
namespace Jde::Opc::Emulator::Tests{
	using namespace OpenProtocol;
	constexpr Duration cycle{ 2s };
	constexpr sv twoSets{ R"("psets":[ { "id":1, "name":"M8 flange", "target":25, "min":23, "max":27, "batchSize":3 }, { "id":2, "name":"M10 bracket", "target":48, "min":45, "max":51 } ])" };
	Ω spec( sv json )ε->TighteningSpec{ return TighteningSpec{ boost::json::parse(string{json}).as_object() }; }
	//Two sets, the first with a batch of three, on a port the system picks:  a rundown every two seconds on the dot, none
	//NOK, unless `more` says otherwise.
	Ω block( sv more={} )ε->TighteningSpec{ return spec( Ƒ(R"({{ "port":0, "cycle":"PT2S", {}{} }})", twoSets, more) ); }

	//A session at a station:  its MID 0002 and what it was pushed, for the case's thread.
	struct Link final{
		Link( uint16_t port )ι:
			_session{ boost::asio::make_strand(*Executor()), "127.0.0.1", port, {}, {
				[this]( Session::Connection&& connection ){ lg _{ _mutex }; _identity = move( connection.Identity ); },
				[]( const Failure& ){},
				[this]( Message&& pushed ){ lg _{ _mutex }; _pushed.push_back( move(pushed) ); }
			} }
		{
			_session.Start();
		}
		Link( const Link& )=delete;
		~Link(){
			auto stopped = ms<std::promise<void>>();//the session's, which may end past this wait.
			_session.Stop( [stopped]{ stopped->set_value(); } );
			stopped->get_future().wait_for( 10s );
		}
		α Identity()Ι->optional<Mid0002>{ lg _{ _mutex }; return _identity; }
		α Ask( Body request )ι->Session::Reply{
			auto answered = ms<std::promise<Session::Reply>>();
			auto reply = answered->get_future();
			_session.Request( move(request), [answered]( Session::Reply&& y ){ answered->set_value( move(y) ); } );
			if( reply.wait_for(10s)!=std::future_status::ready )
				return std::unexpected{ Failure{EFailure::Timeout, 0, 0, "The test's own wait."} };
			return reply.get();
		}
		Τ α Pushed()Ι->vector<T>{
			lg _{ _mutex };
			vector<T> y;
			for( let& message : _pushed ){
				if( let body = get_if<T>(&message.Data) )
					y.push_back( *body );
			}
			return y;
		}
	private:
		mutable mutex _mutex;
		optional<Mid0002> _identity;
		vector<Message> _pushed;
		Session _session;
	};
	Ω refusedWith( const Session::Reply& reply )ι->uint16_t{ return reply || reply.error().Kind!=EFailure::Refused ? 0 : reply.error().Code; }

	struct Bench{
		Bench( sv more={} ): Station{ *Executor(), block(more) }, Client{ Station.Port() }{}
		//MID 0002 here, and the results and the selected set subscribed to, which pushes the set selected at the start.
		α Start()ι->bool{
			return WaitUntil( [&]{ return Client.Identity().has_value(); } ) && Client.Ask( Mid0060{} ) && Client.Ask( Mid0014{} );
		}
		//The results pushed once there are `count` of them.
		α Results( uint count )ι->vector<Mid0061>{
			WaitUntil( [&]{ return Client.Pushed<Mid0061>().size()>=count; } );
			return Client.Pushed<Mid0061>();
		}
		//MID 0064's:  the result kept under `id`, 0 the latest.
		α Kept( uint32_t id )ι->Mid0065{
			let reply = Client.Ask( Mid0064{.TighteningId=id} );
			let kept = reply ? get_if<Mid0065>( &reply->Data ) : nullptr;
			return kept ? *kept : Mid0065{};
		}
		TighteningStation Station;
		Link Client;
	};

	TEST( TighteningSpecTests, DefaultsAreTheDocumentedOnes ){
		let y = spec( R"({ "psets":[ { "id":7, "target":25, "min":23, "max":27 } ] })" );
		EXPECT_EQ( y.Port, 4545 );
		EXPECT_EQ( y.Address, "127.0.0.1" );
		EXPECT_EQ( y.Rundowns.Cycle, 1s ); EXPECT_EQ( y.Rundowns.Jitter, 0s ); EXPECT_EQ( y.Rundowns.Reject, 0 );//production's.
		EXPECT_EQ( y.Keep, 10'000u );
		EXPECT_FALSE( y.Log );
		ASSERT_EQ( y.Psets.size(), 1u );
		EXPECT_EQ( y.Psets[0].Pset, 7 );
		EXPECT_EQ( y.Psets[0].PsetName, "" );
		EXPECT_EQ( y.Psets[0].BatchSize, 0 ) << "no batch";
	}
	TEST( TighteningSpecTests, TheBlockAsItIsWritten ){
		let y = spec( Ƒ(R"({{ "port":4546, "address":"", "cycle":"PT3S", "jitter":"PT0.5S", "reject":0.1, "keep":50, "log":true, {} }})", twoSets) );
		EXPECT_EQ( y.Port, 4546 );
		EXPECT_EQ( y.Address, "" );
		EXPECT_EQ( y.Rundowns.Cycle, 3s ); EXPECT_EQ( y.Rundowns.Jitter, 500ms ); EXPECT_EQ( y.Rundowns.Reject, .1 );
		EXPECT_EQ( y.Keep, 50u );
		EXPECT_TRUE( y.Log );
		ASSERT_EQ( y.Psets.size(), 2u );
		EXPECT_EQ( y.Psets[0].PsetName, "M8 flange" );
		EXPECT_EQ( y.Psets[0].BatchSize, 3 );
		EXPECT_EQ( y.Psets[1].TorqueMin, 45 ); EXPECT_EQ( y.Psets[1].TorqueFinalTarget, 48 ); EXPECT_EQ( y.Psets[1].TorqueMax, 51 );
	}
	TEST( TighteningSpecTests, Refusals ){
		let set = []( sv keys ){ return spec( Ƒ(R"({{ "psets":[ {{ {} }} ] }})", keys) ); };
		EXPECT_NO_THROW( set(R"("id":1, "target":25, "min":23, "max":27)") );
		EXPECT_THROW( spec("{}"), Exception );//a station has a parameter set
		EXPECT_THROW( spec(R"({ "psets":[] })"), Exception );
		EXPECT_THROW( spec(R"({ "psets":{ "id":1, "target":25, "min":23, "max":27 } })"), Exception );//not a list
		EXPECT_THROW( set(R"("target":25, "min":23, "max":27)"), Exception );//no id
		EXPECT_THROW( set(R"("id":0, "target":25, "min":23, "max":27)"), Exception );//MID 0018's three digits, and 0 is none
		EXPECT_THROW( set(R"("id":1000, "target":25, "min":23, "max":27)"), Exception );
		EXPECT_THROW( set(R"("id":1, "min":23, "max":27)"), Exception );//no target
		EXPECT_THROW( set(R"("id":1, "target":25, "max":27)"), Exception );
		EXPECT_THROW( set(R"("id":1, "target":25, "min":23)"), Exception );
		EXPECT_THROW( set(R"("id":1, "target":23, "min":23, "max":27)"), Exception );//the target is inside its limits
		EXPECT_THROW( set(R"("id":1, "target":28, "min":23, "max":27)"), Exception );
		EXPECT_THROW( set(R"("id":1, "target":1, "min":-1, "max":2)"), Exception );//a torque is never under 0
		EXPECT_THROW( set(R"("id":1, "target":9000, "min":5000, "max":9999)"), Exception );//no room for a NOK above the max:  the field holds 9999.99
		EXPECT_THROW( set(R"("id":1, "target":25, "min":23, "max":27, "batchSize":100)"), Exception );//MID 0015's two digits
		EXPECT_THROW( set(R"("id":1, "target":25, "min":23, "max":27, "name":"a name of twenty-six letters")"), Exception );//its twenty-five
		EXPECT_THROW( spec(R"({ "psets":[ { "id":1, "target":25, "min":23, "max":27 }, { "id":1, "target":48, "min":45, "max":51 } ] })"), Exception );//two of one id
		EXPECT_THROW( spec(Ƒ(R"({{ "keep":0, {} }})", twoSets)), Exception );
		EXPECT_THROW( spec(Ƒ(R"({{ "cycle":"PT0S", {} }})", twoSets)), Exception );//production's own
		EXPECT_THROW( spec(Ƒ(R"({{ "reject":1.5, {} }})", twoSets)), Exception );
	}
	TEST( TighteningSpecTests, AKeyOfTheWrongKindIsRefusedNotDefaulted ){
		EXPECT_THROW( spec(Ƒ(R"({{ "port":"4545", {} }})", twoSets)), Exception );
		EXPECT_THROW( spec(Ƒ(R"({{ "port":70000, {} }})", twoSets)), Exception );
		EXPECT_THROW( spec(Ƒ(R"({{ "address":0, {} }})", twoSets)), Exception );
		EXPECT_THROW( spec(Ƒ(R"({{ "cycle":"2s", {} }})", twoSets)), Exception );
		EXPECT_THROW( spec(Ƒ(R"({{ "reject":"0.05", {} }})", twoSets)), Exception );
		EXPECT_THROW( spec(Ƒ(R"({{ "log":"true", {} }})", twoSets)), Exception );
		EXPECT_THROW( spec(R"({ "psets":[ { "id":"1", "target":25, "min":23, "max":27 } ] })"), Exception );
		EXPECT_THROW( spec(R"({ "psets":[ { "id":1, "target":"25", "min":23, "max":27 } ] })"), Exception );
		EXPECT_THROW( spec(R"({ "psets":[ { "id":1, "name":8, "target":25, "min":23, "max":27 } ] })"), Exception );
	}

	//The plan's first:  one result in a cycle, its torque inside the selected set's limits.
	TEST( TighteningTests, OneResultInACycle ){
		Bench bench;
		ASSERT_TRUE( bench.Start() );
		let identity = bench.Client.Identity();
		EXPECT_EQ( identity->ControllerName, TighteningStation::ControllerName );
		EXPECT_EQ( identity->SystemType, 3 ) << "a Power Focus 6000";
		EXPECT_EQ( identity->SequenceNumberSupport, 1 );
		bench.Station.Sample( cycle/2 );
		bench.Station.Sample( cycle/2 );
		auto results = bench.Results( 1 );
		ASSERT_EQ( results.size(), 1u );
		let& first = results[0];
		EXPECT_EQ( first.TighteningId, 1u );
		EXPECT_EQ( first.ControllerName, TighteningStation::ControllerName );
		EXPECT_EQ( first.Status, 1 );
		EXPECT_EQ( first.TorqueStatus, 1 );
		EXPECT_EQ( first.Pset, 1 );
		EXPECT_EQ( first.PsetName, "M8 flange" );
		EXPECT_EQ( first.TorqueMin, 23 ); EXPECT_EQ( first.TorqueFinalTarget, 25 ); EXPECT_EQ( first.TorqueMax, 27 );
		EXPECT_EQ( first.TorqueUnit, 1 ) << "Nm";
		EXPECT_EQ( first.ResultType, 1 ) << "a tightening, which the driver's counters count";
		EXPECT_TRUE( first.Time );
		EXPECT_TRUE( first.PsetChangeTime );
		bench.Station.Sample( cycle*200 );
		results = bench.Results( 201 );
		ASSERT_EQ( results.size(), 201u ) << "a period of two hundred cycles is two hundred rundowns";
		flat_set<double> torques;
		for( uint i=0; i<results.size(); ++i ){
			let& result = results[i];
			EXPECT_EQ( result.TighteningId, i+1 ) << "the ids increase";
			EXPECT_EQ( result.Status, 1 );
			ASSERT_TRUE( result.Torque );
			EXPECT_GE( *result.Torque, 23 );
			EXPECT_LE( *result.Torque, 27 );
			torques.insert( *result.Torque );
		}
		EXPECT_GT( torques.size(), 20u ) << "about the target, with noise";
		EXPECT_EQ( bench.Station.ToString(), "tightening[results=201 nok=0 pset=1 tool=true]" );
	}

	//The plan's second:  a disabled tool makes no results, and an enabled one makes them again.
	TEST( TighteningTests, ADisabledToolMakesNoResults ){
		Bench bench;
		ASSERT_TRUE( bench.Start() );
		ASSERT_TRUE( bench.Client.Ask(Mid0042{}) );
		bench.Station.Sample( cycle*3 );
		EXPECT_EQ( refusedWith(bench.Client.Ask(Mid0064{.TighteningId=0})), 15 ) << "no result is kept:  none was made";
		ASSERT_TRUE( bench.Client.Ask(Mid0043{}) );
		bench.Station.Sample( cycle );
		EXPECT_EQ( bench.Kept(0).TighteningId, 1u ) << "the first the station made";
		EXPECT_EQ( bench.Results(1).size(), 1u );
	}

	//The plan's third:  a SelectPset changes the set the next result names, and restarts the batch.
	TEST( TighteningTests, SelectPsetChangesTheSetTheNextResultNames ){
		Bench bench;
		ASSERT_TRUE( bench.Start() );
		bench.Station.Sample( cycle*2 );
		ASSERT_TRUE( bench.Client.Ask(Mid0018{.Pset=2}) );
		bench.Station.Sample( cycle );
		ASSERT_TRUE( bench.Client.Ask(Mid0018{.Pset=1}) );
		bench.Station.Sample( cycle );
		let results = bench.Results( 4 );
		ASSERT_EQ( results.size(), 4u );
		EXPECT_EQ( results[1].Pset, 1 );
		EXPECT_EQ( results[1].BatchCounter, 2 );
		let& second = results[2];
		EXPECT_EQ( second.Pset, 2 );
		EXPECT_EQ( second.PsetName, "M10 bracket" );
		EXPECT_EQ( second.TorqueMin, 45 ); EXPECT_EQ( second.TorqueFinalTarget, 48 ); EXPECT_EQ( second.TorqueMax, 51 );
		ASSERT_TRUE( second.Torque );
		EXPECT_GE( *second.Torque, 45 );
		EXPECT_LE( *second.Torque, 51 );
		EXPECT_EQ( second.BatchStatus, 2 ) << "the set has no batch";
		EXPECT_EQ( results[3].Pset, 1 );
		EXPECT_EQ( results[3].BatchCounter, 1 ) << "a selection starts the batch over";
		ASSERT_TRUE( WaitUntil([&]{ return bench.Client.Pushed<Mid0015>().size()==3; }) );
		let selections = bench.Client.Pushed<Mid0015>();
		EXPECT_EQ( selections[0].Pset, 1 ) << "the first of the table, pushed as its subscription is accepted";
		EXPECT_EQ( selections[0].PsetName, "M8 flange" );
		EXPECT_EQ( selections[1].Pset, 2 );
		EXPECT_EQ( selections[1].TorqueFinalTarget, 48 );
		EXPECT_EQ( selections[2].BatchSize, 3 );
		EXPECT_EQ( second.PsetChangeTime, selections[1].PsetChangeTime );
	}

	//SelectPset for a set the table lacks, SelectJob and DownloadVin are refused as a PF6000 with neither a Source Batch nor
	//an identifier source refuses them, and what was selected stands.
	TEST( TighteningTests, WhatTheStationLacksIsRefused ){
		Bench bench;
		ASSERT_TRUE( bench.Start() );
		EXPECT_EQ( refusedWith(bench.Client.Ask(Mid0018{.Pset=7})), 3 ) << "Parameter set can not be set";
		EXPECT_EQ( refusedWith(bench.Client.Ask(Mid0038{.JobId=1})), 20 ) << "Job can not be set";
		EXPECT_EQ( refusedWith(bench.Client.Ask(Mid0050{.Vin="VIN0050"})), 8 ) << "VIN input source not granted";
		bench.Station.Sample( cycle );
		let results = bench.Results( 1 );
		ASSERT_EQ( results.size(), 1u );
		EXPECT_EQ( results[0].Pset, 1 );
		EXPECT_EQ( results[0].Vin, "" );
		EXPECT_EQ( bench.Client.Pushed<Mid0015>().size(), 1u );
	}

	//A reject is NOK by its torque, under the low limit or over the high one, and counts for nothing in the batch.
	TEST( TighteningTests, ARejectIsATorqueOutsideTheLimits ){
		Bench bench{ R"(, "reject":1)" };
		ASSERT_TRUE( bench.Start() );
		bench.Station.Sample( cycle*60 );
		let results = bench.Results( 60 );
		ASSERT_EQ( results.size(), 60u );
		uint low{}, high{};
		for( let& result : results ){
			EXPECT_EQ( result.Status, 0 );
			ASSERT_TRUE( result.Torque );
			ASSERT_TRUE( result.TorqueStatus );
			if( *result.TorqueStatus==0 ){
				EXPECT_LT( *result.Torque, 23 );
				++low;
			}
			else{
				EXPECT_EQ( result.TorqueStatus, 2 );
				EXPECT_GT( *result.Torque, 27 );
				++high;
			}
			EXPECT_EQ( result.BatchCounter, 0 );
			EXPECT_EQ( result.BatchStatus, 3 ) << "running";
		}
		EXPECT_GT( low, 0u );
		EXPECT_GT( high, 0u );
		EXPECT_EQ( bench.Station.ToString(), "tightening[results=60 nok=60 pset=1 tool=true]" );
	}

	//The batch counts the OK rundowns up to the set's size, says OK on the one that fills it, and starts over.
	TEST( TighteningTests, TheBatchCountsToItsSizeAndStartsOver ){
		Bench bench;
		ASSERT_TRUE( bench.Start() );
		bench.Station.Sample( cycle*7 );
		let results = bench.Results( 7 );
		ASSERT_EQ( results.size(), 7u );
		for( uint i=0; i<results.size(); ++i ){
			EXPECT_EQ( results[i].BatchSize, 3 );
			EXPECT_EQ( results[i].BatchCounter, i%3+1 ) << i;
			EXPECT_EQ( results[i].BatchStatus, i%3==2 ? 1 : 3 ) << i;
		}
	}

	//Every result is kept for MID 0064, as it was pushed and under the station's id, up to `keep` of them.
	TEST( TighteningTests, ResultsAreKeptForMid0064 ){
		Bench bench{ R"(, "keep":2)" };
		ASSERT_TRUE( bench.Start() );
		bench.Station.Sample( cycle*3 );
		let results = bench.Results( 3 );
		ASSERT_EQ( results.size(), 3u );
		let latest = bench.Kept( 0 );
		EXPECT_EQ( latest.TighteningId, 3u );
		EXPECT_EQ( latest.Torque, results[2].Torque );
		EXPECT_EQ( latest.Time, results[2].Time );
		EXPECT_EQ( latest.StationId, bench.Client.Identity()->StationId );
		EXPECT_EQ( bench.Kept(2).Torque, results[1].Torque );
		EXPECT_EQ( refusedWith(bench.Client.Ask(Mid0064{.TighteningId=1})), 15 ) << "let go past `keep`";
	}
}