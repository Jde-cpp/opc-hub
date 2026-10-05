//OpcServer's shape:  one group, node_indexes the historian issues, no identity on membership, values from setValue with
//no server timestamp, and no connection to break.
#include "hosts.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	TEST_F( ServerHost, IssuesIndexes ){
		let speed = Historize( "Pump1.Speed" );
		let flow = Historize( "Pump1.Flow" );
		let level = Historize( "Tank1.Level" );
		EXPECT_EQ( speed, 1 );
		EXPECT_EQ( flow, 2 );
		EXPECT_EQ( level, 3 );

		Server->Remove( flow );
		EXPECT_EQ( Historize("Pump1.Flow"), 4 );//never issued twice, even to the same node.
		EXPECT_EQ( Server->Find(Node("Pump1.Speed")), speed );
		EXPECT_FALSE( Server->Find(Node("Pump2.Speed")) );

		EXPECT_THROW( Server->Add({Node("Pump2.Speed"), {}, 9}), Exception );//no index of the host's.
		EXPECT_THROW( Historize("Pump1.Speed"), Exception );//already a member.
	}

	//The newest file's preamble gives the map and its FileStart the next index:  a node keeps its index across a restart,
	//a new one takes the next, and one the nodesets dropped is removed.  Index 4 went to a node removed in an earlier
	//day's file, so only the newest file's FileStart says it was issued.
	TEST_F( ServerHost, KeepsIndexesAcrossRestart ){
		let speed = Historize( "Pump1.Speed" );
		Historize( "Pump1.Flow" );
		Historize( "Tank1.Level" );
		Server->Remove( Historize("Pump1.Temp") );
		EXPECT_TRUE( Flush(*Server) );//March 7's file, with Temp's NodeAdded and NodeRemoved.
		Time->AdvanceTo( sys_days{2026y/March/8}+1h );
		EXPECT_TRUE( SetValue(speed, 1) );
		EXPECT_TRUE( Flush(*Server) );//March 8's:  a preamble of 1 to 3, and a FileStart whose next_node_index is 5.
		Restart();
		_group = Server = Library->AddGroup( {.Name="server", .Indexes=EIndexes::Issued}, {{Node("Pump1.Speed")}, {Node("Tank1.Level")}, {Node("Pump2.Speed")}} );
		EXPECT_EQ( Server->Find(Node("Pump1.Speed")), 1 );
		EXPECT_EQ( Server->Find(Node("Tank1.Level")), 3 );
		EXPECT_EQ( Server->Find(Node("Pump2.Speed")), 5 );
		EXPECT_FALSE( Server->Find(Node("Pump1.Flow")) );
		let added = Records<NodeAdded>();
		ASSERT_EQ( added.size(), 1 );//the others are already in the preamble.
		EXPECT_EQ( added[0].Index, 5 );
		EXPECT_FALSE( added[0].By );
		let removed = Records<NodeRemoved>();
		ASSERT_EQ( removed.size(), 1 );
		EXPECT_EQ( removed[0].Index, 2 );
		EXPECT_EQ( Historize("Pump1.Flow"), 6 );//never issued twice, even to the same node.
	}

	TEST_F( ServerHost, OneGroup ){
		EXPECT_EQ( Library->FindGroup("server"), Server );
		EXPECT_THROW( Library->AddGroup({.Name="server", .Indexes=EIndexes::Issued}), Exception );
	}

	TEST_F( ServerHost, ThresholdsFromHAConfiguration ){
		let index = Historize( "Pump1.Speed", {.ExceptionDeviation=2.5, .DeviationFormat=UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE,
			.MinTimeInterval=1s, .MaxTimeInterval=1min, .Stepped=false, .Range=Range{0, 3600}} );
		let config = Server->FindThresholds( index );
		ASSERT_TRUE( config );
		EXPECT_EQ( config->ExceptionDeviation, 2.5 );
		EXPECT_EQ( config->DeviationFormat, UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE );
		EXPECT_EQ( config->MinTimeInterval, 1s );
		EXPECT_EQ( config->MaxTimeInterval, 1min );
		EXPECT_FALSE( config->Stepped );
		EXPECT_EQ( config->Range->High, 3600 );

		let defaults = Server->FindThresholds( Historize("Pump1.Flow") );
		EXPECT_FALSE( defaults->ExceptionDeviation );//every change stored.
		EXPECT_EQ( defaults->MaxTimeInterval, Duration::zero() );//no heartbeat.
		EXPECT_TRUE( defaults->Stepped );
	}

	TEST_F( ServerHost, MembershipHasNoWriter ){
		Historize( "Pump1.Speed" );
		Time->Advance( 1h );
		Server->Remove( 1 );
		let added = Records<NodeAdded>();
		let removed = Records<NodeRemoved>();
		ASSERT_EQ( added.size(), 1 );
		ASSERT_EQ( removed.size(), 1 );
		EXPECT_FALSE( added[0].By );
		EXPECT_FALSE( removed[0].By );
		EXPECT_EQ( added[0].Ts, Time->Now()-1h );
		EXPECT_EQ( removed[0].Ts, Time->Now() );
	}

	//setValue sends no server timestamp; OpcServer is the server, so the time it took the write is the server's time.
	TEST_F( ServerHost, SetValueStampsServerTime ){
		let index = Historize( "Pump1.Speed" );
		let written = Time->Now();
		Time->Advance( 3ms );
		EXPECT_TRUE( Server->Enqueue(index, Reading(1750, written)) );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 1 );
		EXPECT_EQ( values[0].Index, index );
		EXPECT_EQ( values[0].Data.sourceTimestamp, ticks(written) );
		EXPECT_EQ( values[0].Data.serverTimestamp, ticks(Time->Now()) );
		EXPECT_EQ( values[0].Data.Get<double>( 0 ), 1750 );
	}

	TEST_F( ServerHost, EnqueueOnlyMembers ){
		let index = Historize( "Pump1.Speed" );
		EXPECT_TRUE( SetValue(index, 1) );
		EXPECT_FALSE( SetValue(index+1, 2) );
		Server->Remove( index );
		EXPECT_FALSE( SetValue(index, 3) );
		EXPECT_EQ( Records<DataValue>().size(), 1 );
	}

	//The URI names the namespace; the index is OpcServer's own numbering, which the group ignores.
	TEST_F( ServerHost, NamespaceByUri ){
		ExNodeId noUri{ flat_map<string,string>{ {"ns", "2"}, {"s", "Pump1.Speed"} } };
		EXPECT_THROW( Server->Add({noUri}), Exception );

		auto indexed = Node( "Pump1.Speed" );
		indexed.nodeId.namespaceIndex = 2;
		let index = Server->Add( {indexed} );
		EXPECT_EQ( Server->Find(Node("Pump1.Speed")), index );
		indexed.nodeId.namespaceIndex = 5;
		EXPECT_EQ( Server->Find(indexed), index );
		EXPECT_FALSE( Server->Find(Node("Pump1.Speed", "urn:other")) );
	}

	TEST_F( ServerHost, NeverBreaks ){
		let index = Historize( "Pump1.Speed" );
		EXPECT_TRUE( Server->IsConnected() );
		EXPECT_FALSE( Server->FindBreak(index) );
	}
}