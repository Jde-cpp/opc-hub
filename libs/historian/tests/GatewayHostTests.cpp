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

	//A connection breaks for every group on it; the gateway calls each.  The break is the first one until a value
	//arrives, however many callbacks come before the connection returns.
	TEST_F( GatewayHost, Break ){
		auto pump1 = AddGroup();
		auto pump2 = AddGroup();
		let speed = Join( *pump1, "Pump1.Speed" );
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