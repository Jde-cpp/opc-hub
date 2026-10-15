//A node that left its group and joined again (#326):  a new index, but one series.  A read of the node by any of its
//indexes takes every index the group gave its NodeId or its files' preambles map to it - the earlier index's values,
//the Bad_DataLost its rejoin wrote at the removal time, then the current index's - under the index asked for, forward,
//backward, paged, with bounds, at times, aggregated, after a restart, across days that list none of its indexes, and in
//a subscription's snapshot and stream;  and an edit reaches the earlier index's records, an insert going under the index
//whose start value it may move.
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
	//archive, the live file and the buffer alike; the earlier index, named, reads the same series under its own.
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
		let earlier = All( {.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+4min)} );
		EXPECT_EQ( values(earlier), (vector<optional<double>>{1, 2, nullopt, 3, 4}) );
		EXPECT_EQ( indexes(earlier), vector<NodeIndex>(5, Speed) );
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

	//The walk back for an opening bound reads the 7th's archive while the 8th's live file is open:  the page's records
	//after it keep the live file's place, so a page resumed once the 8th is archived reads on by time.
	TEST_F( Rejoins, WalkBackKeepsTheOpenDaysPlace ){
		TwoDays();
		ReadRequest request{ .Nodes={Again}, .Start=ticks(Eighth+30s), .End=ticks(Eighth+4min), .Bounds=true, .Limit=2 };
		auto page = Pump->Read( request );
		ASSERT_EQ( page.Values.size(), 2u );
		EXPECT_TRUE( isBound(page.Values[0], Again, 2, T0+2s) );
		EXPECT_TRUE( isMarker(page.Values[1], Again, Removed) );
		EXPECT_FALSE( continuation(page).has_generation() );
		Time->AdvanceTo( sys_days{March9}+1min );
		Settle( *Pump );//the 8th archived, generation 1 as the 7th is.
		request.Continuation = move( page.Continuation );
		let rest = All( move(request) );
		EXPECT_EQ( values(rest), (vector<optional<double>>{3, 4, nullopt}) );
		EXPECT_EQ( bounds(rest), (vector<bool>{false, false, true}) );
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

	//A rejoin after a restart writes no marker, the process having forgotten the break, and the restart left the earlier
	//index's newest record unknown, since the newest file doesn't list it.  A backward read of the node beside one whose
	//latest is older reads the earlier index's last day rather than jumping past it to the other's.
	TEST_F( Rejoins, RestartedBeforeTheRejoin ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		Temp = Join( *Pump, "Pump1.Temp" );
		let flow = Join( *Pump, "Pump1.Flow" );
		DataChange( *Pump, Temp, 10, T0+1s );
		DataChange( *Pump, Speed, 1, T0+2s );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( Eighth+1min );
		Settle( *Pump );
		DataChange( *Pump, Speed, 2, Eighth+1min );
		Time->AdvanceTo( Eighth+2min );
		Pump->Remove( Speed );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( sys_days{March9}+1min );
		Settle( *Pump );
		DataChange( *Pump, flow, 5, Time->Now() );
		EXPECT_TRUE( Flush(*Pump) );
		let name = Pump->Name();
		Restart();
		Pump = Rejoin( name, {{Node("Pump1.Temp"), {}, Temp}, {Node("Pump1.Flow"), {}, flow}} );
		Time->AdvanceTo( Tenth+1min );
		Settle( *Pump );
		Again = Join( *Pump, "Pump1.Speed" );
		Time->AdvanceTo( Tenth+2min );
		DataChange( *Pump, Again, 3, Tenth+2min );
		EXPECT_TRUE( Flush(*Pump) );
		let back = All( {.Nodes={Temp, Again}, .End=ticks(Tenth+1min+30s), .Limit=100} );
		EXPECT_EQ( sources(back), (vector<Ticks>{ticks(Eighth+1min), ticks(T0+2s), ticks(T0+1s)}) );
		EXPECT_EQ( indexes(back), (vector<NodeIndex>{Again, Again, Temp}) );
	}

	//A backward read of a node with fewer values than it asks for ends at the day each of its indexes was first listed:
	//it opens no older file, so one that can't be read doesn't fail it.
	TEST_F( Rejoins, LastValuesStopAtTheFirstDay ){
		Pump = AddGroup();
		Temp = Join( *Pump, "Pump1.Temp" );
		DataChange( *Pump, Temp, 10, T0+1s );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( Eighth+1min );
		Settle( *Pump );
		Speed = Join( *Pump, "Pump1.Speed" );
		Time->AdvanceTo( Eighth+2min );
		DataChange( *Pump, Speed, 1, Eighth+2min );
		Time->AdvanceTo( Eighth+3min );
		DataChange( *Pump, Speed, 2, Eighth+3min );
		EXPECT_TRUE( Flush(*Pump) );
		fs::permissions( File(*Pump, March7), fs::perms::none );
		EXPECT_EQ( sources(All({.Nodes={Speed}, .End=ticks(Eighth+4min), .Limit=100})), (vector<Ticks>{ticks(Eighth+3min), ticks(Eighth+2min)}) );
		fs::permissions( File(*Pump, March7), fs::perms::owner_read | fs::perms::owner_write );
	}

	//An edit of the node reaches its earlier index's records, written under that index, and an insert here goes under the
	//current one, which the 8th's file lists;  the modified read returns them under the index asked for.
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

	//An edit of the earlier index's last value, after the group has forgotten that index, corrects the start value a
	//later day's preamble copied:  a bound or an at-time value there is the edited one, after a delete the one before,
	//and after an insert later than it, which goes under the index that day lists, the one inserted.
	TEST_F( Rejoins, EditCorrectsLaterStartValues ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		Temp = Join( *Pump, "Pump1.Temp" );
		DataChange( *Pump, Speed, 1, T0+1s );
		DataChange( *Pump, Speed, 2, T0+2s );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( Eighth+1min );
		Settle( *Pump );
		DataChange( *Pump, Temp, 10, Eighth+1min );
		EXPECT_TRUE( Flush(*Pump) );//the 8th's file, whose preamble lists Speed with its start value, 2.
		Time->AdvanceTo( Eighth+2min );
		Pump->Remove( Speed );
		EXPECT_TRUE( Flush(*Pump) );//and the group forgets the index.
		Time->AdvanceTo( Eighth+3min );
		Again = Join( *Pump, "Pump1.Speed" );
		let opening = [&]{
			let bounded = All( {.Nodes={Again}, .Start=ticks(Eighth+30s), .End=ticks(Eighth+4min), .Bounds=true} );
			return bounded.empty() ? optional<double>{} : numberOf( bounded.front().Value );
		};
		let atTime = [&]{
			let at = readAllAtTime( *Pump, {.Nodes={Again}, .Times={ticks(Eighth+30s)}} );
			return at.empty() ? optional<double>{} : numberOf( at.front().Value );
		};
		ASSERT_EQ( opening(), 2 );
		let replaced = Edit( {UpdateData{Again, UA_PERFORMUPDATETYPE_REPLACE, {Reading(20, T0+2s)}}} );
		ASSERT_EQ( replaced.size(), 1u );
		EXPECT_EQ( replaced[0].Results, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED}) );
		EXPECT_EQ( opening(), 20 );
		EXPECT_EQ( atTime(), 20 );
		let deleted = Edit( {DeleteAtTime{Again, {ticks(T0+2s)}}} );
		ASSERT_EQ( deleted.size(), 1u );
		EXPECT_EQ( deleted[0].Results, (vector<StatusCode>{UA_STATUSCODE_GOOD}) );
		EXPECT_EQ( opening(), 1 );
		EXPECT_EQ( atTime(), 1 );
		let inserted = Edit( {UpdateData{Again, UA_PERFORMUPDATETYPE_INSERT, {Reading(5, T0+5s)}}} );
		ASSERT_EQ( inserted.size(), 1u );
		EXPECT_EQ( inserted[0].Results, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		EXPECT_EQ( opening(), 5 );
		EXPECT_EQ( atTime(), 5 );
	}

	//An insert before the node's first membership goes under its first index, the one the next file lists:  that file's
	//start value takes it, and the later membership's file, which listed no start value, still lists none.
	TEST_F( Rejoins, InsertBeforeTheFirstMembership ){
		TwoDays();
		let inserted = Edit( {UpdateData{Again, UA_PERFORMUPDATETYPE_INSERT, {Reading(0.5, sys_days{March6}+12h)}}} );
		ASSERT_EQ( inserted.size(), 1u );
		EXPECT_EQ( inserted[0].Results, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		let bounded = All( {.Nodes={Again}, .Start=ticks(Eighth+30s), .End=ticks(Eighth+4min), .Bounds=true} );
		ASSERT_FALSE( bounded.empty() );
		EXPECT_TRUE( isBound(bounded[0], Again, 2, T0+2s) );
		EXPECT_EQ( sources(All({.Nodes={Again}, .End=ticks(Eighth+4min), .Limit=100})), (vector<Ticks>{ticks(Eighth+3min), ticks(Eighth+2min), ticks(Removed), ticks(T0+2s), ticks(T0+1s), ticks(sys_days{March6}+12h)}) );
	}

	//An insert on a day between the memberships, whose file lists neither index:  the bound of a range that ends before
	//any later file lists the node walks back to it by the day's modifications.
	TEST_F( Rejoins, WalkBackFindsAnInsertBetween ){
		GapDays();
		let inserted = Edit( {UpdateData{Again, UA_PERFORMUPDATETYPE_INSERT, {Reading(7, sys_days{March8}+12h)}}} );
		ASSERT_EQ( inserted.size(), 1u );
		EXPECT_EQ( inserted[0].Results, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		let bounded = All( {.Nodes={Again}, .Start=ticks(sys_days{March9}+12h), .End=ticks(sys_days{March9}+18h), .Bounds=true} );
		ASSERT_FALSE( bounded.empty() );
		EXPECT_TRUE( isBound(bounded[0], Again, 7, sys_days{March8}+12h) );
	}

	//A node named twice gets its bounds in each slot, a rejoined one's from the walk back, as the first does.
	TEST_F( Rejoins, NamedTwice ){
		TwoDays();
		let temp = All( {.Nodes={Temp, Temp}, .Start=ticks(T0+3s), .End=ticks(Eighth+4min), .Bounds=true} );
		EXPECT_EQ( values(temp), (vector<optional<double>>{10, 10, nullopt, nullopt}) );
		EXPECT_EQ( bounds(temp), (vector<bool>(4, true)) );
		let again = All( {.Nodes={Again, Again}, .Start=ticks(Eighth+30s), .End=ticks(Eighth+4min), .Bounds=true} );
		EXPECT_EQ( values(again), (vector<optional<double>>{2, 2, nullopt, 3, 4, nullopt, nullopt}) );
		EXPECT_EQ( bounds(again), (vector<bool>{true, true, false, false, false, true, true}) );
		EXPECT_EQ( indexes(again), vector<NodeIndex>(7, Again) );
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

	//The stream delivers a record of any of the node's indexes under the one the subscription named:  an edit's
	//Modification of the earlier index's value, and the current index's values to one that named the earlier.
	TEST_F( Rejoins, StreamUnderTheIndexNamed ){
		TwoDays();
		struct Listener final : ISubscriber{
			α OnRecords( vector<Proto::HistoryRecord>&& records )ι->void override{ ul _{ Mutex }; std::ranges::move( records, std::back_inserter(Records) ); }
			α OnEnded( ESubscriptionEnd )ι->void override{}
			α Indexes()ι->vector<NodeIndex>{
				ul _{ Mutex };
				vector<NodeIndex> y;
				for( let& r : Records )
					y.push_back( r.has_modification() ? r.modification().node_index() : r.value().node_index() );
				return y;
			}
			absl::Mutex Mutex;
			vector<Proto::HistoryRecord> Records ABSL_GUARDED_BY(Mutex);
		};
		auto current = ms<Listener>(), earlier = ms<Listener>();
		auto byCurrent = Pump->Subscribe( current, {Again}, ticks(Eighth+4min) );
		auto byEarlier = Pump->Subscribe( earlier, {Speed}, ticks(Eighth+4min) );
		let replaced = Edit( {UpdateData{Again, UA_PERFORMUPDATETYPE_REPLACE, {Reading(1.5, T0+1s)}}} );
		ASSERT_EQ( replaced.size(), 1u );
		EXPECT_EQ( replaced[0].Results, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED}) );
		EXPECT_TRUE( Settle(*Pump) );
		Time->Advance( 1ms );//Modifications go on the clock's hop.
		DataChange( *Pump, Again, 5, Eighth+5min );
		EXPECT_EQ( current->Indexes(), (vector<NodeIndex>{Again, Again}) ) << "the Modification, then the value";
		EXPECT_EQ( earlier->Indexes(), (vector<NodeIndex>{Speed, Speed}) );
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