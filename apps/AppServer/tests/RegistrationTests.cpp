//Registration under contention (emulator-review W1).  Every AppServer bounce with two apps alive re-registers both in
//the same instant.  AddInstance used to re-load the `hosts` enum with a BlockAwait on its executor thread, over a query
//co_spawned onto the same pool:  with executor.threads:2 (this suite's setting, and the AppServer's) two registrations
//parked both threads with their queries queued behind them, and neither ever answered.  The re-load is awaited now.
//On sqlite (ctest) the select completes inline and this passes either way;  against MySQL (-include=args/mysql) it is
//the wedge itself:  two ConnectionInfo replies, or two 10 s timeouts.
#include "helpers.h"
#include "../src/appStartup.h"

#define let const auto
namespace Jde::App::Server::Tests{
	TEST( RegistrationTests, TwoRegistrationsInTheSameInstantBothComplete ){
		auto a = Connect();
		auto b = Connect();
		let requestA = SendInstance( *a, "Tests.Registration", "first", "registration-host", 4101, 4101 );
		let requestB = SendInstance( *b, "Tests.Registration", "second", "registration-host", 4102, 4102 );
		let first = AwaitInstance( *a, requestA, "first" );
		let second = AwaitInstance( *b, requestB, "second" );
		EXPECT_EQ( first.Program, second.Program ) << "one program, two instances";
		EXPECT_NE( first.Instance, second.Instance );
		EXPECT_NE( first.Connection, second.Connection );
		EXPECT_NE( first.Connection, 0u );
	}
	//and a burst wider than the pool:  more registrations in flight than executor threads, all answered.
	TEST( RegistrationTests, ABurstWiderThanThePoolCompletes ){
		constexpr uint count{ 4 };
		vector<sp<RawClientSession>> sessions;
		vector<RequestId> requests;
		for( uint i=0; i<count; ++i ){
			sessions.push_back( Connect() );
			requests.push_back( SendInstance(*sessions.back(), "Tests.Registration", Ƒ("burst{}", i), "registration-host", 4200+i, 4200+i) );
		}
		flat_set<ConnectionPK> connections;
		for( uint i=0; i<count; ++i )
			connections.emplace( AwaitInstance(*sessions[i], requests[i], Ƒ("burst{}", i)).Connection );
		EXPECT_EQ( connections.size(), count ) << "every registration answered with its own connection";
	}
	//325-logs-resource review #4:  two live instances of one (program, name) with different resource schemas would share an
	//instance row, the last to register deciding whose `logs` governs the other's lines - the second is refused instead.
	TEST( RegistrationTests, ALiveNameRefusesAnotherSchema ){
		constexpr sv plantA{ "opc.registrationPlantA" };
		auto a = Connect();
		let first = RegisterInstance( *a, "Tests.Registration", "shared", "plant-a", 4301, 4301, string{plantA} );
		auto b = Connect();
		let refused = b->WaitForException( SendInstance(*b, "Tests.Registration", "shared", "plant-b", 4302, 4302, "opc.registrationPlantB") );
		ASSERT_TRUE( refused ) << "another endpoint, another schema";
		EXPECT_TRUE( refused->exception().what().contains("/instanceName") ) << refused->exception().what();
		EXPECT_EQ( App::InstanceLogSchema(first.Instance), plantA ) << "the live instance keeps its schema";

		auto restarted = Connect();
		let renamed = RegisterInstance( *restarted, "Tests.Registration", "shared", "plant-a", 4301, 4303, "opc.registrationPlantA2" );
		EXPECT_EQ( renamed.Instance, first.Instance );
		EXPECT_EQ( App::InstanceLogSchema(renamed.Instance), "opc.registrationPlantA2" ) << "the same endpoint is a restart, which RemoveExisting supersedes";
		EXPECT_EQ( RegisterInstance(*b, "Tests.Registration", "shared", "plant-b", 4302, 4302, "opc.registrationPlantA2").Instance, first.Instance ) << "the same schema shares the row, as before";
	}
	//...but one process hosting two roles shares its identity knowingly:  Jde.Opc.Tests runs the gateway and an OpcServer under
	//one application and instance name, registered with different schemas from the same host and pid.
	TEST( RegistrationTests, OneProcessRegistersTwoRolesUnderOneName ){
		auto gateway = Connect();
		let first = RegisterInstance( *gateway, "Tests.Registration", "roles", "roles-host", 0, 4401 );
		auto server = Connect();
		EXPECT_EQ( RegisterInstance(*server, "Tests.Registration", "roles", "roles-host", 0, 4401, "opc.registrationRoles").Instance, first.Instance );
	}
}