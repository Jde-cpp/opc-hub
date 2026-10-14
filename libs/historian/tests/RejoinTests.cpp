//A node that left its group and joined again (#326):  a new index, but one series.  A read of the node by its current
//index takes every index the group's files map to its NodeId, each file's preamble saying which of its indexes are the
//node's - the earlier index's values, the Bad_DataLost its rejoin wrote at the removal time, then the current index's -
//under the index asked for, forward, backward, paged, with bounds, at times, aggregated, after a restart, across days
//that list none of its indexes, and in a subscription's snapshot;  and an edit reaches the earlier index's records.
#include "reads.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;

	Ω indexes( const vector<ReadValue>& values )ι->vector<NodeIndex>{
		vector<NodeIndex> y;
		for( let& v : values )
			y.push_back( v.Value.node_index() );
		return y;
	}
	//Each value's number, none for a marker or a bound not found, which doubles' NaN can't be compared.
	Ω values( const vector<ReadValue>& values )ι->vector<optional<double>>{
		vector<optional<double>> y;
		for( let& v : values )
			y.push_back( numberOf(v.Value) );
		return y;
	}
	Ω isMarker( const ReadValue& v, NodeIndex index, TimePoint at, bool bound=false )ι->bool{
		return v.Bound==bound && !v.Value.has_value() && v.Value.status()==UA_STATUSCODE_BADDATALOST && v.Value.node_index()==index && v.Value.source_ts()==ticks( at );
	}

	//Speed 1@+1s and 2@+2s and temp 10@+2s on March 7, archived.  Speed leaves on the 8th at +1min and rejoins at +2min as
	//Again, which stores 3@+2min, after the Bad_DataLost its rejoin writes at the removal time, in the 8th's file, and
	//4@+3min still in the buffer.
	struct Rejoins : GatewayFiles{
		α TwoDays()ε->void{
			Pump = AddGroup();
			Speed = Join( *Pump, "Pump1.Speed" );
			Temp = Join( *Pump, "Pump1.Temp" );
			DataChange( *Pump, Speed, 1, T0+1s );
			DataChange( *Pump, Speed, 2, T0+2s );
			DataChange( *Pump, Temp, 10, T0+2s );
			EXPECT_TRUE( Flush(*Pump) );
			Time->AdvanceTo( Eighth+1min );
			Settle( *Pump );
			Removed = Time->Now();
			Pump->Remove( Speed );
			Time->AdvanceTo( Eighth+2min );
			Again = Join( *Pump, "Pump1.Speed" );
			EXPECT_NE( Again, Speed );
			DataChange( *Pump, Again, 3, Eighth+2min );
			EXPECT_TRUE( Flush(*Pump) );
			Time->AdvanceTo( Eighth+3min );
			DataChange( *Pump, Again, 4, Eighth+3min );
		}
		//Speed 1@+1s and 2@+2s on the 7th, which it leaves at +10s;  temp alone on the 8th and 9th;  Speed back on the 10th as
		//Again, 3@+2min in its file, the marker in the 7th's archive, and 4@+3min in the buffer.
		α GapDays()ε->void{
			Pump = AddGroup();
			Speed = Join( *Pump, "Pump1.Speed" );
			Temp = Join( *Pump, "Pump1.Temp" );
			DataChange( *Pump, Speed, 1, T0+1s );
			DataChange( *Pump, Speed, 2, T0+2s );
			DataChange( *Pump, Temp, 10, T0+2s );
			Time->AdvanceTo( T0+10s );
			Removed = Time->Now();
			Pump->Remove( Speed );
			EXPECT_TRUE( Flush(*Pump) );
			for( let day : {March8, March9} ){
				Time->AdvanceTo( sys_days{day}+1min );
				Settle( *Pump );
				DataChange( *Pump, Temp, 10+(double)(unsigned)day.day()-7, Time->Now()+1min );
				EXPECT_TRUE( Flush(*Pump) );
			}
			Time->AdvanceTo( Tenth+1min );
			Settle( *Pump );
			Again = Join( *Pump, "Pump1.Speed" );
			Time->AdvanceTo( Tenth+2min );
			DataChange( *Pump, Again, 3, Tenth+2min );
			EXPECT_TRUE( Flush(*Pump) );
			Time->AdvanceTo( Tenth+3min );
			DataChange( *Pump, Again, 4, Tenth+3min );
		}
		//The gateway starting again with the group's rows as they are now:  what the process knew of the node's earlier
		//index goes, and the files alone say which indexes are its.
		α RestartWithCurrentRows()ε->void{
			let name = Pump->Name();
			Restart();
			Pump = Rejoin( name, {{Node("Pump1.Speed"), {}, Again}, {Node("Pump1.Temp"), {}, Temp}} );
		}
		α All( ReadRequest request, vector<uint>* pages=nullptr )ε->vector<ReadValue>{ return readAll( *Pump, move(request), pages ); }
		α Edit( vector<EditDetails> details )ε->vector<EditResult>{ return BlockAny( Pump->Edit(move(details), Admin) ); }
		const TimePoint T0{ Time->Now() };
		const TimePoint Eighth{ sys_days{March8} };
		const TimePoint Tenth{ sys_days{March10} };
		TimePoint Removed;
		sp<Group> Pump;
		NodeIndex Speed{}, Temp{}, Again{};
	};

	//Forward, backward and paged, the earlier index's values come under the current one, with the marker between, from the
	//archive, the live file and the buffer alike; the earlier index alone, once the group has forgotten it, reads its own.
	TEST_F( Rejoins, OneSeries ){
		TwoDays();
		let all = All( {.Nodes={Again}, .Start=ticks(T0), .End=ticks(Eighth+4min)} );
		EXPECT_EQ( values(all), (vector<optional<double>>{1, 2, nullopt, 3, 4}) ) << "none is the marker";
		EXPECT_EQ( sources(all), (vector<Ticks>{ticks(T0+1s), ticks(T0+2s), ticks(Removed), ticks(Eighth+2min), ticks(Eighth+3min)}) );
		EXPECT_EQ( indexes(all), vector<NodeIndex>(5, Again) );
		ASSERT_EQ( all.size(), 5u );
		EXPECT_TRUE( isMarker(all[2], Again, Removed) );
		EXPECT_EQ( sources(All({.Nodes={Again}, .Start=ticks(Eighth+4min), .End=ticks(T0)})), (vector<Ticks>{ticks(Eighth+3min), ticks(Eighth+2min), ticks(Removed), ticks(T0+2s), ticks(T0+1s)}) );
		EXPECT_EQ( sources(All({.Nodes={Again}, .End=ticks(Eighth+4min), .Limit=100})), (vector<Ticks>{ticks(Eighth+3min), ticks(Eighth+2min), ticks(Removed), ticks(T0+2s), ticks(T0+1s)}) ) << "the last N walk past the join day";
		vector<uint> pages;
		let paged = All( {.Nodes={Again, Temp}, .Start=ticks(T0), .End=ticks(Eighth+4min), .Limit=2}, &pages );
		EXPECT_EQ( sources(paged), (vector<Ticks>{ticks(T0+1s), ticks(T0+2s), ticks(T0+2s), ticks(Removed), ticks(Eighth+2min), ticks(Eighth+3min)}) );
		EXPECT_EQ( indexes(paged), (vector<NodeIndex>{Again, Again, Temp, Again, Again, Again}) );
		EXPECT_EQ( pages, (vector<uint>{2, 2, 2}) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+4min)})), (vector<double>{1, 2}) ) << "an index the group has no node for reads alone";
	}

	//The bounds go by the series:  before a range in the earlier membership its last value there, before one in the gap
	//the marker, and after it the current index's next.
	TEST_F( Rejoins, Bounds ){
		TwoDays();
		let across = All( {.Nodes={Again}, .Start=ticks(T0+1500ms), .End=ticks(Eighth+2min+30s), .Bounds=true} );
		ASSERT_EQ( across.size(), 5u );
		EXPECT_TRUE( isBound(across[0], Again, 1, T0+1s) );
		EXPECT_EQ( values(across), (vector<optional<double>>{1, 2, nullopt, 3, 4}) );
		EXPECT_EQ( bounds(across), (vector<bool>{true, false, false, false, true}) );
		let gap = All( {.Nodes={Again}, .Start=ticks(Eighth+1min+30s), .End=ticks(Eighth+4min), .Bounds=true} );
		ASSERT_EQ( gap.size(), 4u );
		EXPECT_TRUE( isMarker(gap[0], Again, Removed, true) );
		EXPECT_EQ( values(gap), (vector<optional<double>>{nullopt, 3, 4, nullopt}) );
		EXPECT_TRUE( notFound(gap[3], Again, Eighth+4min) );
		let reverse = All( {.Nodes={Again}, .Start=ticks(Eighth+2min+30s), .End=ticks(T0+1500ms), .Bounds=true} );
		ASSERT_EQ( reverse.size(), 5u );
		EXPECT_TRUE( isBound(reverse[0], Again, 4, Eighth+3min) );
		EXPECT_TRUE( isBound(reverse[4], Again, 1, T0+1s) );
	}

	//At-time and processed reads compute from the joined series.
	TEST_F( Rejoins, AtTimeAndAggregates ){
		TwoDays();
		let at = readAllAtTime( *Pump, {.Nodes={Again}, .Times={ticks(T0+1500ms), ticks(Eighth+2min+30s)}} );
		ASSERT_EQ( at.size(), 2u );
		EXPECT_EQ( numberOf(at[0].Value), 1 );
		EXPECT_EQ( numberOf(at[1].Value), 3 );
		EXPECT_EQ( indexes(at), (vector<NodeIndex>{Again, Again}) );
		let max = readAllProcessed( *Pump, {.Nodes={Again}, .Start=ticks(T0), .End=ticks(Eighth+4min), .Aggregate=EAggregate::Maximum} );
		ASSERT_EQ( max.size(), 1u );
		EXPECT_EQ( numberOf(max[0].Value), 4 );
		let min = readAllProcessed( *Pump, {.Nodes={Again}, .Start=ticks(T0), .End=ticks(Eighth+4min), .Aggregate=EAggregate::Minimum} );
		ASSERT_EQ( min.size(), 1u );
		EXPECT_EQ( numberOf(min[0].Value), 1 );
		EXPECT_EQ( min[0].Value.node_index(), Again );
	}

	//After a restart the group knows nothing of the earlier index:  the files' preambles join it all the same.
	TEST_F( Rejoins, AfterARestart ){
		TwoDays();
		RestartWithCurrentRows();
		let all = All( {.Nodes={Again}, .Start=ticks(T0), .End=ticks(Eighth+4min)} );
		EXPECT_EQ( values(all), (vector<optional<double>>{1, 2, nullopt, 3, 4}) );
		EXPECT_EQ( indexes(all), vector<NodeIndex>(5, Again) );
		EXPECT_EQ( sources(All({.Nodes={Again}, .End=ticks(Eighth+4min), .Limit=100})), (vector<Ticks>{ticks(Eighth+3min), ticks(Eighth+2min), ticks(Removed), ticks(T0+2s), ticks(T0+1s)}) );
		let gap = All( {.Nodes={Again}, .Start=ticks(Eighth+1min+30s), .End=ticks(Eighth+4min), .Bounds=true} );
		ASSERT_GE( gap.size(), 1u );
		EXPECT_TRUE( isMarker(gap[0], Again, Removed, true) );
	}

	//Days whose files list none of the node's indexes lie between its memberships:  a forward read passes through them, a
	//backward one over them by their preambles, to the day the earlier index was last listed, and the bounds reach back the
	//same way, before and after a restart.
	TEST_F( Rejoins, AcrossDaysWithoutIt ){
		GapDays();
		for( let restarted : {false, true} ){
			SCOPED_TRACE( restarted ? "restarted" : "live" );
			if( restarted )
				RestartWithCurrentRows();
			const vector<Ticks> forward{ ticks(T0+1s), ticks(T0+2s), ticks(Removed), ticks(Tenth+2min), ticks(Tenth+3min) };
			let all = All( {.Nodes={Again}, .Start=ticks(T0), .End=ticks(Tenth+4min)} );
			EXPECT_EQ( sources(all), forward );
			EXPECT_EQ( indexes(all), vector<NodeIndex>(5, Again) );
			EXPECT_EQ( sources(All({.Nodes={Again}, .End=ticks(Tenth+4min), .Limit=100})), (vector<Ticks>{forward.rbegin(), forward.rend()}) );
			EXPECT_EQ( sources(All({.Nodes={Again}, .Start=ticks(Tenth+4min), .End=ticks(T0)})), (vector<Ticks>{forward.rbegin(), forward.rend()}) );
			let bounded = All( {.Nodes={Again}, .Start=ticks(sys_days{March9}+12h), .End=ticks(Tenth+4min), .Bounds=true} );
			ASSERT_EQ( bounded.size(), 4u );
			EXPECT_TRUE( isMarker(bounded[0], Again, Removed, true) );
			EXPECT_EQ( values(bounded), (vector<optional<double>>{nullopt, 3, 4, nullopt}) );
			let temp = All( {.Nodes={Temp, Again}, .Start=ticks(T0), .End=ticks(Tenth+4min)} );
			EXPECT_EQ( indexes(temp), (vector<NodeIndex>{Again, Again, Temp, Again, Temp, Temp, Again, Again}) );
		}
	}

	//An edit of the node reaches its earlier index's records, written under that index, and an insert goes under the
	//current one;  the modified read returns them under the index asked for.
	TEST_F( Rejoins, EditsReachTheEarlierIndex ){
		TwoDays();
		let replaced = Edit( {UpdateData{Again, UA_PERFORMUPDATETYPE_REPLACE, {Reading(1.5, T0+1s)}}, UpdateData{Again, UA_PERFORMUPDATETYPE_INSERT, {Reading(1.7, T0+1700ms)}}} );
		ASSERT_EQ( replaced.size(), 2u );
		EXPECT_EQ( replaced[0].Results, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED}) );
		EXPECT_EQ( replaced[1].Results, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		let all = All( {.Nodes={Again}, .Start=ticks(T0), .End=ticks(Eighth+4min)} );
		EXPECT_EQ( values(all), (vector<optional<double>>{1.5, 1.7, 2, nullopt, 3, 4}) );
		EXPECT_EQ( indexes(all), vector<NodeIndex>(6, Again) );
		let modified = All( {.Nodes={Again}, .Start=ticks(T0), .End=ticks(Eighth+4min), .Modified=true} );
		EXPECT_EQ( doubles(modified), (vector<double>{1, 1.7}) ) << "the value replaced, and the one inserted";
		EXPECT_EQ( indexes(modified), (vector<NodeIndex>{Again, Again}) );
		let deleted = Edit( {DeleteRaw{Again, ticks(T0), ticks(Eighth+4min)}} );
		ASSERT_EQ( deleted.size(), 1u );
		EXPECT_EQ( deleted[0].Status, UA_STATUSCODE_GOOD );
		EXPECT_TRUE( All({.Nodes={Again}, .Start=ticks(T0), .End=ticks(Eighth+4min)}).empty() ) << "every record of the series, under both indexes";
		EXPECT_TRUE( All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+4min)}).empty() );
	}

	//A subscription's snapshot joins the series as a read does.
	TEST_F( Rejoins, SubscriptionSnapshot ){
		TwoDays();
		struct Listener final : ISubscriber{
			α OnRecords( vector<Proto::HistoryRecord>&& )ι->void override{}
			α OnEnded( ESubscriptionEnd )ι->void override{}
		};
		auto subscription = Pump->Subscribe( ms<Listener>(), {Again}, ticks(T0+1500ms) );
		vector<ReadValue> snapshot;
		for( ;; ){
			auto page = subscription->Next( 0 );
			std::ranges::move( page.Values, std::back_inserter(snapshot) );
			if( page.Continuation.empty() )
				break;
		}
		ASSERT_EQ( snapshot.size(), 5u );
		EXPECT_TRUE( isBound(snapshot[0], Again, 1, T0+1s) );
		EXPECT_EQ( values(snapshot), (vector<optional<double>>{1, 2, nullopt, 3, 4}) );
		EXPECT_EQ( indexes(snapshot), vector<NodeIndex>(5, Again) );
	}

	//OpcServer's shape, whose indexes the historian issues:  a node historized again takes the next, and reads as one.
	TEST_F( ServerFiles, RejoinedNodeReadsAsOne ){
		let t0 = Time->Now();
		let speed = Historize( "Pump1.Speed" );
		SetValue( speed, 1 );
		Time->Advance( 1s );
		SetValue( speed, 2 );
		Time->Advance( 1s );
		Server->Remove( speed );
		let removed = Time->Now();
		Time->Advance( 1min );
		let again = Historize( "Pump1.Speed" );
		EXPECT_EQ( again, speed+1 );
		SetValue( again, 3 );
		EXPECT_TRUE( Flush(*Server) );
		let all = readAll( *Server, {.Nodes={again}, .Start=ticks(t0), .End=ticks(Time->Now()+1s)} );
		EXPECT_EQ( values(all), (vector<optional<double>>{1, 2, nullopt, 3}) );
		EXPECT_EQ( indexes(all), vector<NodeIndex>(4, again) );
		ASSERT_EQ( all.size(), 4u );
		EXPECT_TRUE( isMarker(all[2], again, removed) );
	}
}