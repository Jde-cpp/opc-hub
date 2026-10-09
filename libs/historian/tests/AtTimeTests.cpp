//At-time reads (#207):  a record at the time, else the value interpolated from the node's bounding records, stepped
//or sloped, with interpolated or simple bounds; the times in the request's order, paged by a continuation; and a read
//over day files and the buffer.
#include <jde/opc/UAException.h>
#include "reads.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	constexpr StatusCode Info{ 0x400 }, Interpolated{ 0x2 };
	constexpr StatusCode Uncertain{ 0x40000000 }, Bad{ 0x80000000 }, SubNormal{ UA_STATUSCODE_UNCERTAINDATASUBNORMAL }, NoData{ UA_STATUSCODE_BADNODATA };
	constexpr StatusCode GoodInterpolated{ Info | Interpolated }, SubNormalInterpolated{ SubNormal | Info | Interpolated };

	//Speed, stepped, and Flow, sloped, with the same records on the 7th:  10@+10s, 20@+20s, a Bad at +40s, 50@+50s,
	//70@+70s Uncertain, 80@+80s.
	struct AtTimes : GatewayFiles{
		α Load()ε->void{
			Pump = AddGroup();
			Speed = Join( *Pump, "Pump1.Speed" );
			Flow = Join( *Pump, "Pump1.Flow", {.Stepped=false} );
			for( let index : {Speed, Flow} ){
				EXPECT_TRUE( Pump->Enqueue(index, Reading(10, T0+10s)) );
				EXPECT_TRUE( Pump->Enqueue(index, Reading(20, T0+20s)) );
				EXPECT_TRUE( Pump->Enqueue(index, Status(Bad, T0+40s)) );
				EXPECT_TRUE( Pump->Enqueue(index, Reading(50, T0+50s)) );
				EXPECT_TRUE( Pump->Enqueue(index, Graded(70, Uncertain, T0+70s)) );
				EXPECT_TRUE( Pump->Enqueue(index, Reading(80, T0+80s)) );
			}
			EXPECT_TRUE( Flush(*Pump) );
		}
		α Read( AtTimeRequest request )ε->ReadResult{ return Pump->ReadAtTime( move(request) ); }
		α All( AtTimeRequest request, vector<uint>* pages=nullptr )ε->vector<ReadValue>{ return readAllAtTime( *Pump, move(request), pages ); }
		α At( const vector<Duration>& offsets )Ι->vector<UA_DateTime>{
			vector<UA_DateTime> y;
			for( let& d : offsets )
				y.push_back( ticks(T0+d) );
			return y;
		}
		//A value as (node, time, value, status).
		Ω Is( const ReadValue& v, NodeIndex index, TimePoint time, optional<double> value, StatusCode status )->::testing::AssertionResult{
			let number = numberOf( v.Value );
			if( v.Value.node_index()==index && v.Value.source_ts()==ticks(time) && number==value && v.Value.status()==status && !v.Bound && !v.Modification )
				return ::testing::AssertionSuccess();
			return ::testing::AssertionFailure() << Ƒ( "{} vs node {} at {} value {} status {:x}", v.Value.ShortDebugString(), index, ToIsoString(time), value ? Ƒ("{}", *value) : "none", status );
		}
		const TimePoint T0{ Time->Now() };
		sp<Group> Pump;
		NodeIndex Speed{}, Flow{};
	};

	//Interpolated bounds (Part 13 §3.1.8):  a record at the time is returned as it is; otherwise the nearest non-Bad
	//records either side, stepped or sloped per the node, Uncertain where a Bad one was skipped or an Uncertain one
	//used, Bad_NoData before the first and extrapolated past the last.  Uncertain is Bad unless the configuration says
	//otherwise, and the extrapolation sloped only when it asks.
	TEST_F( AtTimes, InterpolatedBounds ){
		Load();
		let v = All( {.Nodes={Speed, Flow}, .Times=At({20s, 25s, 45s, 65s, 75s, 90s, 5s, 70s})} );
		ASSERT_EQ( v.size(), 16 );
		EXPECT_TRUE( Is(v[0], Speed, T0+20s, 20, 0) );
		EXPECT_TRUE( Is(v[1], Flow, T0+20s, 20, 0) );
		EXPECT_TRUE( Is(v[2], Speed, T0+25s, 20, GoodInterpolated) );
		EXPECT_TRUE( Is(v[3], Flow, T0+25s, 25, SubNormalInterpolated) );//the Bad at +40s lies before the value after.
		EXPECT_TRUE( Is(v[4], Speed, T0+45s, 20, SubNormalInterpolated) );
		EXPECT_TRUE( Is(v[5], Flow, T0+45s, 45, SubNormalInterpolated) );
		EXPECT_TRUE( Is(v[6], Speed, T0+65s, 50, GoodInterpolated) );
		EXPECT_TRUE( Is(v[7], Flow, T0+65s, 65, SubNormalInterpolated) );//the Uncertain at +70s is Bad, so the value after is 80.
		EXPECT_TRUE( Is(v[8], Speed, T0+75s, 50, SubNormalInterpolated) );
		EXPECT_TRUE( Is(v[9], Flow, T0+75s, 75, SubNormalInterpolated) );
		EXPECT_TRUE( Is(v[10], Speed, T0+90s, 80, SubNormalInterpolated) );//past the last record.
		EXPECT_TRUE( Is(v[11], Flow, T0+90s, 80, SubNormalInterpolated) );
		EXPECT_TRUE( Is(v[12], Speed, T0+5s, nullopt, NoData) );
		EXPECT_TRUE( Is(v[13], Flow, T0+5s, nullopt, NoData) );
		EXPECT_TRUE( Is(v[14], Speed, T0+70s, 50, SubNormalInterpolated) );//the Uncertain record at the time is Bad, and skipped.
		EXPECT_TRUE( Is(v[15], Flow, T0+70s, 70, SubNormalInterpolated) );

		AtTimeRequest counted{ .Nodes={Speed, Flow}, .Times=At({65s, 75s, 70s, 90s}), .Configuration={.TreatUncertainAsBad=false} };
		let u = All( counted );
		ASSERT_EQ( u.size(), 8 );
		EXPECT_TRUE( Is(u[0], Speed, T0+65s, 50, GoodInterpolated) );
		EXPECT_TRUE( Is(u[1], Flow, T0+65s, 65, SubNormalInterpolated) );//the value after is the Uncertain one.
		EXPECT_TRUE( Is(u[2], Speed, T0+75s, 70, SubNormalInterpolated) );
		EXPECT_TRUE( Is(u[3], Flow, T0+75s, 75, SubNormalInterpolated) );
		EXPECT_TRUE( Is(u[4], Speed, T0+70s, 70, Uncertain) );//the record itself.
		EXPECT_TRUE( Is(u[5], Flow, T0+70s, 70, Uncertain) );
		EXPECT_TRUE( Is(u[6], Speed, T0+90s, 80, SubNormalInterpolated) );
		EXPECT_TRUE( Is(u[7], Flow, T0+90s, 80, SubNormalInterpolated) );

		let sloped = All( {.Nodes={Flow, Speed}, .Times=At({90s}), .Configuration={.UseSlopedExtrapolation=true}} );
		ASSERT_EQ( sloped.size(), 2 );
		EXPECT_TRUE( Is(sloped[0], Flow, T0+90s, 90, SubNormalInterpolated) );//the line through 50@+50s and 80@+80s.
		EXPECT_TRUE( Is(sloped[1], Speed, T0+90s, 80, SubNormalInterpolated) );//stepped whatever the configuration says.
	}

	//Simple bounds (Part 13 §3.1.9):  the nearest records whatever their status, so a Bad one before the time is
	//Bad_NoData and a Bad one after it steps back to the one before, Uncertain.
	TEST_F( AtTimes, SimpleBounds ){
		Load();
		let v = All( {.Nodes={Speed, Flow}, .Times=At({35s, 45s, 65s, 70s, 75s, 90s}), .SimpleBounds=true} );
		ASSERT_EQ( v.size(), 12 );
		EXPECT_TRUE( Is(v[0], Speed, T0+35s, 20, GoodInterpolated) );
		EXPECT_TRUE( Is(v[1], Flow, T0+35s, 20, SubNormalInterpolated) );//the record after is Bad.
		EXPECT_TRUE( Is(v[2], Speed, T0+45s, nullopt, NoData) );//the record before is Bad.
		EXPECT_TRUE( Is(v[3], Flow, T0+45s, nullopt, NoData) );
		EXPECT_TRUE( Is(v[4], Speed, T0+65s, 50, GoodInterpolated) );
		EXPECT_TRUE( Is(v[5], Flow, T0+65s, 50, SubNormalInterpolated) );//the Uncertain record after is Bad by default.
		EXPECT_TRUE( Is(v[6], Speed, T0+70s, nullopt, NoData) );//and so is the record at the time.
		EXPECT_TRUE( Is(v[7], Flow, T0+70s, nullopt, NoData) );
		EXPECT_TRUE( Is(v[8], Speed, T0+75s, nullopt, NoData) );
		EXPECT_TRUE( Is(v[9], Flow, T0+75s, nullopt, NoData) );
		EXPECT_TRUE( Is(v[10], Speed, T0+90s, 80, SubNormalInterpolated) );
		EXPECT_TRUE( Is(v[11], Flow, T0+90s, 80, SubNormalInterpolated) );

		let u = All( {.Nodes={Flow}, .Times=At({65s, 70s, 75s}), .SimpleBounds=true, .Configuration={.TreatUncertainAsBad=false}} );
		ASSERT_EQ( u.size(), 3 );
		EXPECT_TRUE( Is(u[0], Flow, T0+65s, 65, SubNormalInterpolated) );
		EXPECT_TRUE( Is(u[1], Flow, T0+70s, 70, Uncertain) );
		EXPECT_TRUE( Is(u[2], Flow, T0+75s, 75, SubNormalInterpolated) );
	}

	//The values come in the order of the request's times, each time's in the nodes' order, a repeated time twice.  A
	//page holds Limit values and resumes where the last stopped, inside a time included; the continuation is refused
	//with other arguments, or another mode's.
	TEST_F( AtTimes, OrderAndPaging ){
		Load();
		AtTimeRequest r{ .Nodes={Flow, Speed}, .Times=At({25s, 20s, 25s}) };
		let whole = All( r );
		ASSERT_EQ( whole.size(), 6 );
		EXPECT_TRUE( Is(whole[0], Flow, T0+25s, 25, SubNormalInterpolated) );
		EXPECT_TRUE( Is(whole[1], Speed, T0+25s, 20, GoodInterpolated) );
		EXPECT_TRUE( Is(whole[2], Flow, T0+20s, 20, 0) );
		EXPECT_TRUE( Is(whole[3], Speed, T0+20s, 20, 0) );
		EXPECT_TRUE( Is(whole[4], Flow, T0+25s, 25, SubNormalInterpolated) );
		EXPECT_TRUE( Is(whole[5], Speed, T0+25s, 20, GoodInterpolated) );
		for( let limit : {1u, 4u, 5u, 6u} ){
			vector<uint> pages;
			r.Limit = limit;
			let paged = All( r, &pages );
			ASSERT_EQ( paged.size(), 6 ) << limit;
			for( uint i=0; i<6; ++i )
				EXPECT_EQ( paged[i].Value.ShortDebugString(), whole[i].Value.ShortDebugString() ) << limit << " " << i;
			for( uint i=0; i+1<pages.size(); ++i )
				EXPECT_EQ( pages[i], limit ) << limit;
		}
		r.Limit = 3;
		auto page = Read( r );
		ASSERT_EQ( page.Values.size(), 3 );
		let c = continuation( page );
		EXPECT_EQ( c.next(), 1 );
		ASSERT_EQ( c.counts_size(), 2 );
		EXPECT_EQ( c.counts(0), 1 );
		EXPECT_EQ( c.counts(1), 0 );
		for( uint i=0; i<5; ++i ){
			auto other = r;
			switch( i ){
			case 0: other.Nodes = {Speed, Flow}; break;
			case 1: other.Times = At( {25s, 20s} ); break;
			case 2: other.SimpleBounds = true; break;
			case 3: other.Configuration.TreatUncertainAsBad = false; break;
			case 4: other.Configuration.UseSlopedExtrapolation = true; break;
			}
			other.Continuation = page.Continuation;
			EXPECT_THROW( Read(other), UAException ) << i;
		}
		r.Continuation = "not a continuation";
		EXPECT_THROW( Read(r), UAException );
		let raw = Pump->Read( {.Nodes={Flow, Speed}, .Start=ticks(T0), .End=ticks(T0+100s), .Limit=2} );
		ASSERT_FALSE( raw.Continuation.empty() );
		r.Continuation = raw.Continuation;
		EXPECT_THROW( Read(r), UAException );
		EXPECT_THROW( Pump->ReadProcessed({.Nodes={Flow, Speed}, .Start=ticks(T0), .End=ticks(T0+100s), .Interval=5s, .Continuation=page.Continuation}), UAException );
		EXPECT_THROW( Read({.Nodes={}, .Times=At({20s})}), Exception );
		EXPECT_THROW( Read({.Nodes={Speed}, .Times={}}), Exception );
	}

	//Times on several days read each day's records once, with the bounds from the files either side, a value still in
	//the buffer included.
	TEST_F( AtTimes, AcrossDaysAndTheBuffer ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		let eighth = sys_days{March8};
		DataChange( *Pump, Speed, 1, T0+1s );
		DataChange( *Pump, Speed, 2, T0+2s );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( eighth+1min );
		Settle( *Pump );
		DataChange( *Pump, Speed, 3, eighth+2min );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( eighth+3min );
		DataChange( *Pump, Speed, 4, eighth+3min );
		let v = All( {.Nodes={Speed}, .Times={ticks(T0+1500ms), ticks(eighth+1min), ticks(eighth+2min+30s), ticks(eighth+3min), ticks(eighth+5min), ticks(T0)}} );
		ASSERT_EQ( v.size(), 6 );
		EXPECT_TRUE( Is(v[0], Speed, T0+1500ms, 1, GoodInterpolated) );
		EXPECT_TRUE( Is(v[1], Speed, eighth+1min, 2, GoodInterpolated) );//the 7th's last, by the 8th's start value.
		EXPECT_TRUE( Is(v[2], Speed, eighth+2min+30s, 3, GoodInterpolated) );
		EXPECT_TRUE( Is(v[3], Speed, eighth+3min, 4, 0) );//the buffer's.
		EXPECT_TRUE( Is(v[4], Speed, eighth+5min, 4, SubNormalInterpolated) );
		EXPECT_TRUE( Is(v[5], Speed, T0, nullopt, NoData) );
	}
}