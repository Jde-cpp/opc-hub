#pragma once
//The two hosts with `delay` at a minute, and their day files as a test reads them.
#include "hosts.h"
#include "files.h"
#include "../src/io/DayFiles.h"

namespace Jde::Opc::Hist::Tests{
	constexpr Day March6{ 2026y/March/6 }, March7{ 2026y/March/7 }, March8{ 2026y/March/8 }, March9{ 2026y/March/9 };
	Ξ utc()ι->const time_zone&{ return *locate_zone( "UTC" ); }
	//The runs a group holds for a file are those a scan of it rebuilds.
	Ξ expectRuns( const vector<Run>& actual, const fs::path& file )ε->void{
		const auto expected = Scan( file ).Runs;
		ASSERT_EQ( actual.size(), expected.size() ) << file;
		for( uint i=0; i<actual.size(); ++i ){
			SCOPED_TRACE( Ƒ("run {} of {}", i, actual.size()) );
			EXPECT_EQ( actual[i].Offset, expected[i].Offset );
			EXPECT_EQ( actual[i].End, expected[i].End );
			EXPECT_EQ( actual[i].Chain, expected[i].Chain );
			EXPECT_EQ( actual[i].First, expected[i].First );
			EXPECT_EQ( actual[i].Last, expected[i].Last );
		}
	}
	Ξ isValue( const Proto::HistoryRecord& r, NodeIndex index, double v, TimePoint source )ι->bool{
		return r.has_value() && r.value().node_index()==index && r.value().value().double_value()==v && r.value().source_ts()==ticks( source );
	}
	Ξ isPreamble( const Proto::HistoryRecord& r, NodeIndex index, Day day, const time_zone& tz=utc() )ι->bool{
		return r.has_node_added() && r.node_added().node_index()==index && r.node_added().ts()==StartOf( day, tz ) && !r.node_added().has_identity_id();
	}

	//The gateway's shape with `delay` at its default, a minute.
	struct GatewayFiles : GatewayHost{
		GatewayFiles()ε:GatewayHost{ 1min }{}
		α File( const Group& group, Day day )Ι->fs::path{ return Path()/DayDirectory( day )/( group.Name()+".binpb" ); }
		//The gateway starting again with a group's hist_group_nodes rows.
		α Rejoin( const string& name, vector<Member> members )ε->sp<Group>{
			return _group = Library->AddGroup( {.Name=name, .Indexes=EIndexes::Host, .PublishingInterval=500ms}, move(members) );
		}
		//A file where 2026's directory goes, so no day file of this year can be made, and its removal.
		α Block()Ι->void{ save( Path()/"2026", "in the way" ); }
		α Unblock()Ι->void{ fs::remove( Path()/"2026" ); }
	};
	//OpcServer's shape with `delay` at a minute.
	struct ServerFiles : ServerHost{
		ServerFiles()ε:ServerHost{ 1min }{}
		α File( Day day )Ι->fs::path{ return Path()/DayDirectory( day )/"server.binpb"; }
		//OpcServer starting again with the nodes its nodesets historize.
		α Start( vector<Member> members )ε->void{
			Restart();
			_group = Server = Library->AddGroup( {.Name="server", .Indexes=EIndexes::Issued}, move(members) );
		}
	};
}
