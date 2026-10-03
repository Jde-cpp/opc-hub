//The gateway's shape:  many groups named by guid, node_indexes that are hist_group_nodes row ids, thresholds the host
//resolves, a writer on every membership change, and connections that break.
#include <thread>
#include "hosts.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	TEST_F( GatewayHost, GroupsByGuid ){
		let pump1 = AddGroup();
		let pump2 = AddGroup();
		EXPECT_NE( pump1->Name(), pump2->Name() );
		EXPECT_EQ( Library.FindGroup(pump1->Name()), pump1 );
		EXPECT_FALSE( Library.FindGroup("server") );
		EXPECT_THROW( Library.AddGroup({.Name=pump1->Name()}), Exception );
		EXPECT_THROW( Library.AddGroup({.Name="../pump"}), Exception );//names a file.
		EXPECT_THROW( Library.AddGroup({.Name=""}), Exception );
	}

	//Deleting a hist_groups row:  every member leaves, by the caller who deleted it, and what the group buffered stays for
	//the flush.
	TEST_F( GatewayHost, RemoveGroup ){
		auto pump1 = AddGroup();
		auto pump2 = AddGroup();
		let speed = Join( *pump1, "Pump1.Speed" );
		Join( *pump1, "Pump1.Flow" );
		DataChange( *pump1, speed, 1750, Time->Now() );
		Library.RemoveGroup( pump1->Name(), Writer{{{8}}, "operator"} );
		EXPECT_FALSE( Library.FindGroup(pump1->Name()) );
		EXPECT_EQ( Library.FindGroup(pump2->Name()), pump2 );
		let removed = Records<NodeRemoved>();
		ASSERT_EQ( removed.size(), 2 );
		ASSERT_TRUE( removed[0].By );
		EXPECT_EQ( removed[0].By->UserName, "operator" );
		EXPECT_EQ( Records<DataValue>().size(), 1 );

		EXPECT_FALSE( DataChange(*pump1, speed, 1760, Time->Now()) );//one racing the delete.
		EXPECT_THROW( Join(*pump1, "Pump1.Level"), Exception );
		EXPECT_THROW( Library.RemoveGroup(pump1->Name()), Exception );
	}

	//Members leave in index order however they joined, so a removed group writes the same records every run.
	TEST_F( GatewayHost, RemovedInIndexOrder ){
		vector<Member> members;
		for( NodeIndex index=40; index>0; --index )
			members.push_back( {Node(Ƒ("Pump{}.Speed", index)), {}, index*7} );
		_group = Library.AddGroup( {.Name="pumps", .Indexes=EIndexes::Host}, move(members) );
		Library.RemoveGroup( "pumps", Admin );
		let removed = Records<NodeRemoved>();
		ASSERT_EQ( removed.size(), 40 );
		EXPECT_TRUE( std::ranges::is_sorted(removed, {}, &NodeRemoved::Index) );
	}

	TEST_F( GatewayHost, RowIdsAsIndexes ){
		auto pump1 = AddGroup();
		auto pump2 = AddGroup();
		EXPECT_EQ( Join(*pump1, "Pump1.Speed"), 101 );
		EXPECT_EQ( Join(*pump2, "Pump2.Speed"), 102 );
		EXPECT_EQ( Join(*pump1, "Pump1.Flow"), 103 );
		EXPECT_EQ( pump1->Find(Node("Pump1.Flow")), 103 );
		EXPECT_FALSE( pump2->Find(Node("Pump1.Flow")) );

		EXPECT_THROW( pump1->Add({Node("Pump1.Level")}), Exception );//the host's index is required.
		EXPECT_THROW( pump1->Add({Node("Pump1.Level"), {}, 101}), Exception );//already a member's.
		pump1->Remove( 101 );
		EXPECT_EQ( Join(*pump1, "Pump1.Speed"), 104 );//a rejoin is a new row.
	}

	//A group's hist_group_nodes rows when the gateway starts.
	TEST_F( GatewayHost, MembersAtStart ){
		auto group = Library.AddGroup( {.Name="pump1"}, {{Node("Pump1.Speed"), {}, 101}, {Node("Pump1.Flow"), {.MaxTimeInterval=1min}, 103}} );
		_group = group;
		EXPECT_EQ( group->Find(Node("Pump1.Speed")), 101 );
		EXPECT_EQ( group->Find(Node("Pump1.Flow")), 103 );
		EXPECT_EQ( group->FindThresholds(103)->MaxTimeInterval, 1min );
		let added = Records<NodeAdded>();
		ASSERT_EQ( added.size(), 2 );
		EXPECT_FALSE( added[0].By );//no caller at start.
		EXPECT_THROW( Library.AddGroup({.Name="pump2"}, {{Node("Pump2.Speed")}}), Exception );//the row id is required.
	}

	//Each row id is the node's index, so a node that left and rejoined while the gateway was down comes back under its
	//new row, and a row deleted meanwhile is removed.
	TEST_F( GatewayHost, Restart ){
		auto group = Restart( {.Name="pump1"},
			{ {Node("Pump1.Speed"), {}, 101}, {Node("Pump1.Flow"), {}, 105}, {Node("Pump1.Temp"), {}, 106} },
			{ .Members={{Node("Pump1.Speed"), 101}, {Node("Pump1.Flow"), 102}, {Node("Pump1.Level"), 103}} } );
		EXPECT_EQ( group->Find(Node("Pump1.Flow")), 105 );
		flat_set<NodeIndex> removed, added;
		for( let& r : Records<NodeRemoved>() )
			removed.emplace( r.Index );
		for( let& r : Records<NodeAdded>() )
			added.emplace( r.Index );
		EXPECT_EQ( removed, (flat_set<NodeIndex>{102, 103}) );
		EXPECT_EQ( added, (flat_set<NodeIndex>{105, 106}) );
		EXPECT_TRUE( std::holds_alternative<NodeRemoved>(group->Buffer().front()) );//a rejoin leaves before it joins.

		EXPECT_THROW( Restart({.Name="pump1"}, {{Node("Pump1.Pressure"), {}, 101}}, {.Members={{Node("Pump1.Speed"), 101}}}), Exception );//Speed's index.
	}

	//row, then template member, then group:  the library holds only the result, so an edit to a template member or a
	//group reaches it as the host's SetThresholds on each affected node.
	TEST_F( GatewayHost, ThresholdsResolvedByHost ){
		auto group = AddGroup();
		Columns member{ .ExceptionDeviation=0.5 };
		Duration groupHeartbeat = 10min;
		let inherits = Join( *group, "Pump1.Speed", Resolve({}, member, groupHeartbeat) );
		let overrides = Join( *group, "Pump1.Flow", Resolve({.ExceptionDeviation=1, .MaxTimeInterval=1min}, member, groupHeartbeat) );
		EXPECT_EQ( group->FindThresholds(inherits)->ExceptionDeviation, 0.5 );
		EXPECT_EQ( group->FindThresholds(inherits)->MaxTimeInterval, 10min );
		EXPECT_EQ( group->FindThresholds(overrides)->ExceptionDeviation, 1 );
		EXPECT_EQ( group->FindThresholds(overrides)->MaxTimeInterval, 1min );

		member.ExceptionDeviation = 2;
		groupHeartbeat = 0min;
		group->SetThresholds( inherits, Resolve({}, member, groupHeartbeat) );
		group->SetThresholds( overrides, Resolve({.ExceptionDeviation=1, .MaxTimeInterval=1min}, member, groupHeartbeat) );
		EXPECT_EQ( group->FindThresholds(inherits)->ExceptionDeviation, 2 );
		EXPECT_EQ( group->FindThresholds(inherits)->MaxTimeInterval, Duration::zero() );
		EXPECT_EQ( group->FindThresholds(overrides)->ExceptionDeviation, 1 );
		EXPECT_EQ( group->FindThresholds(overrides)->MaxTimeInterval, 1min );
		EXPECT_THROW( group->SetThresholds(999, {}), Exception );
	}

	//A percent-of-range format with no usable range stores every change, with a warning; any other bad threshold is an
	//error naming its node.
	TEST_F( GatewayHost, ThresholdsChecked ){
		auto group = AddGroup();
		let percent = UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE;
		let noRange = Join( *group, "Pump1.Speed", {.ExceptionDeviation=2, .DeviationFormat=percent} );
		EXPECT_FALSE( group->FindThresholds(noRange)->ExceptionDeviation );
		let flat = Join( *group, "Pump1.Flow", {.ExceptionDeviation=2, .DeviationFormat=percent, .Range=Range{5, 5}} );
		EXPECT_FALSE( group->FindThresholds(flat)->ExceptionDeviation );
		group->SetThresholds( flat, {.ExceptionDeviation=2, .DeviationFormat=percent, .Range=Range{0, 100}} );//re-resolved.
		EXPECT_EQ( group->FindThresholds(flat)->ExceptionDeviation, 2 );
		EXPECT_EQ( Join(*group, "Pump1.Temp", {.ExceptionDeviation=0, .MinTimeInterval=1min, .MaxTimeInterval=10s}), 103 );//a heartbeat never counts for MinTimeInterval.

		EXPECT_THROW( Join(*group, "Pump1.Level", {.ExceptionDeviation=-1}), Exception );
		EXPECT_THROW( Join(*group, "Pump1.Level", {.ExceptionDeviation=std::numeric_limits<double>::quiet_NaN()}), Exception );
		EXPECT_THROW( Join(*group, "Pump1.Level", {.ExceptionDeviation=1, .DeviationFormat=UA_EXCEPTIONDEVIATIONFORMAT_UNKNOWN}), Exception );
		EXPECT_THROW( Join(*group, "Pump1.Level", {.MinTimeInterval=-1s}), Exception );
		EXPECT_FALSE( group->Find(Node("Pump1.Level")) );
		EXPECT_THROW( group->SetThresholds(flat, {.MaxTimeInterval=-1min}), Exception );
		EXPECT_EQ( group->FindThresholds(flat)->ExceptionDeviation, 2 );//unchanged.
	}

	TEST_F( GatewayHost, WriterOnMembership ){
		auto group = AddGroup();
		let index = Join( *group, "Pump1.Speed" );
		group->Remove( index, Writer{{{8}}, "operator"} );
		let added = Records<NodeAdded>();
		let removed = Records<NodeRemoved>();
		ASSERT_EQ( added.size(), 1 );
		ASSERT_EQ( removed.size(), 1 );
		EXPECT_EQ( added[0].Index, index );
		EXPECT_EQ( added[0].Node, Node("Pump1.Speed") );
		ASSERT_TRUE( added[0].By );
		EXPECT_EQ( added[0].By->IdentityId, Admin.IdentityId );
		EXPECT_EQ( added[0].By->UserName, "admin" );
		ASSERT_TRUE( removed[0].By );
		EXPECT_EQ( removed[0].By->UserName, "operator" );
	}

	//Every value carries both timestamps; the group keeps the server's.
	TEST_F( GatewayHost, DataChange ){
		auto group = AddGroup();
		let index = Join( *group, "Pump1.Speed" );
		let source = Time->Now()-2s;
		EXPECT_TRUE( DataChange(*group, index, 1750, source) );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 1 );
		EXPECT_EQ( values[0].Data.sourceTimestamp, Ua(source) );
		EXPECT_EQ( values[0].Data.serverTimestamp, Ua(source+5ms) );
	}

	//A connection breaks for every group on it; the gateway calls each.  A node's break is the first one until a value
	//arrives, however many callbacks come before, and that value carries it.
	TEST_F( GatewayHost, Break ){
		auto pump1 = AddGroup();
		auto pump2 = AddGroup();
		let speed = Join( *pump1, "Pump1.Speed" );
		let flow = Join( *pump1, "Pump1.Flow" );
		let other = Join( *pump2, "Pump2.Speed" );
		let broke = Time->Now();
		pump1->Disconnected( broke );
		EXPECT_FALSE( pump1->IsConnected() );
		EXPECT_TRUE( pump2->IsConnected() );
		EXPECT_EQ( pump1->FindBreak(speed), broke );
		EXPECT_FALSE( pump2->FindBreak(other) );

		Time->Advance( 30s );
		pump1->Disconnected( Time->Now() );
		EXPECT_EQ( pump1->FindBreak(speed), broke );
		pump1->Connected();
		EXPECT_TRUE( pump1->IsConnected() );
		EXPECT_EQ( pump1->FindBreak(speed), broke );//the next value is judged against it.

		DataChange( *pump1, speed, 1750, Time->Now() );
		DataChange( *pump1, speed, 1760, Time->Now() );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 2 );
		EXPECT_EQ( values[0].Break, broke );
		EXPECT_FALSE( values[1].Break );
		EXPECT_FALSE( pump1->FindBreak(speed) );
		EXPECT_EQ( pump1->FindBreak(flow), broke );//no value yet.

		Time->Advance( 1min );
		let again = Time->Now();
		pump1->Disconnected( again );
		EXPECT_EQ( pump1->FindBreak(speed), again );
		EXPECT_EQ( pump1->FindBreak(flow), broke );

		DataChange( *pump1, speed, 1770, Time->Now() );//one already in flight when the callback came.
		Time->Advance( 1min );
		pump1->Disconnected( Time->Now() );
		EXPECT_EQ( pump1->FindBreak(speed), Time->Now() );
	}

	//A rejoin is a break at the removal time, or at the break the node left with, and its first value is judged against
	//it.  A new node has no earlier value to compare, so it joins with none, even while the connection is down.
	TEST_F( GatewayHost, RejoinBreaks ){
		auto group = AddGroup();
		let speed = Join( *group, "Pump1.Speed" );
		let flow = Join( *group, "Pump1.Flow" );
		let removed = Time->Now();
		group->Remove( speed );
		Time->Advance( 1h );
		let rejoined = Join( *group, "Pump1.Speed" );
		EXPECT_EQ( group->FindBreak(rejoined), removed );
		DataChange( *group, rejoined, 1750, Time->Now() );
		EXPECT_FALSE( group->FindBreak(rejoined) );
		let values = Records<DataValue>();
		ASSERT_EQ( values.size(), 1 );
		EXPECT_EQ( values[0].Break, removed );

		let broke = Time->Now();
		group->Disconnected( broke );
		Time->Advance( 1min );
		group->Remove( flow );
		EXPECT_FALSE( group->FindBreak(Join(*group, "Pump1.Level")) );
		group->Connected();
		Time->Advance( 1h );
		EXPECT_EQ( group->FindBreak(Join(*group, "Pump1.Flow")), broke );
	}

	//Each connection's strand enqueues into its own groups at once.
	TEST_F( GatewayHost, EnqueueFromStrands ){
		constexpr uint count{ 2000 };
		vector<tuple<sp<Group>,NodeIndex>> nodes;
		for( uint i=0; i<4; ++i ){
			auto group = AddGroup();
			nodes.emplace_back( group, Join(*group, "Pump.Speed") );
		}
		vector<std::jthread> strands;
		for( auto& [group, index] : nodes ){
			strands.emplace_back( [&, group, index]{
				for( uint i=0; i<count; ++i )
					DataChange( *group, index, (double)i, Time->Now() );
			});
		}
		strands.clear();
		for( auto& [group, _] : nodes ){
			let buffer = group->Buffer();
			EXPECT_EQ( std::ranges::count_if(buffer, []( let& r ){ return std::holds_alternative<DataValue>(r); }), count );
		}
	}
}