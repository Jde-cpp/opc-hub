//Raw reads (#205):  forward, reverse and open-ended, over archives, a live file's runs and the buffer; the stateless
//continuation, with its archive offset and generation check, and readLimit; and bounds, including the forward scan of
//later preambles.
#include <jde/opc/UAException.h>
#include "reads.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;

	//March 7 archived, March 8 live:  speed 1@+1s, 2@+2s and temp 10@+2s on the 7th, speed 3@+2min on the 8th in its file and
	//4@+3min still in the buffer.
	struct Reads : GatewayFiles{
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
			DataChange( *Pump, Speed, 3, Eighth+2min );
			EXPECT_TRUE( Flush(*Pump) );
			Time->AdvanceTo( Eighth+3min );
			DataChange( *Pump, Speed, 4, Eighth+3min );
		}
		α Read( ReadRequest request )ε->ReadResult{ return Pump->Read( move(request) ); }
		α All( ReadRequest request, vector<uint>* pages=nullptr )ε->vector<ReadValue>{ return readAll( *Pump, move(request), pages ); }
		const TimePoint T0{ Time->Now() };
		const TimePoint Eighth{ sys_days{March8} };
		sp<Group> Pump;
		NodeIndex Speed{}, Temp{};
	};

	TEST_F( Reads, AcrossDaysAndTheBuffer ){
		TwoDays();
		let all = All( {.Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(Eighth+4min)} );
		EXPECT_EQ( doubles(all), (vector<double>{1, 2, 10, 3, 4}) );
		EXPECT_EQ( sources(all), (vector<Ticks>{ticks(T0+1s), ticks(T0+2s), ticks(T0+2s), ticks(Eighth+2min), ticks(Eighth+3min)}) );
		EXPECT_EQ( all[2].Value.node_index(), Temp );
		EXPECT_EQ( all[2].Value.server_ts(), ticks(T0+2s+5ms) );
		EXPECT_EQ( bounds(all), vector<bool>(5, false) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+4min)})), (vector<double>{1, 2, 3, 4}) );
		EXPECT_EQ( doubles(All({.Nodes={Temp}, .Start=ticks(T0), .End=ticks(Eighth+4min)})), (vector<double>{10}) );
		EXPECT_TRUE( All({.Nodes={Speed, Temp}, .Start=ticks(sys_days{March9}), .End=ticks(sys_days{March9}+1h)}).empty() );
		EXPECT_TRUE( All({.Nodes={Speed}, .Start=ticks(T0+1500ms), .End=ticks(T0+1900ms)}).empty() );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0+2s), .End=ticks(T0+2s)})), (vector<double>{2}) );//both ends inside the range.
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .Start=ticks(T0+2s), .End=ticks(Eighth+2min)})), (vector<double>{2, 10, 3}) );
		EXPECT_THROW( Read({.Nodes={}, .Start=ticks(T0)}), Exception );
		EXPECT_THROW( Read({.Nodes={Speed}}), Exception );
	}

	//A live file's runs, each in order, merge by source time, ties going to the earlier run.
	TEST_F( Reads, MergesALiveFilesRuns ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		DataChange( *Pump, Speed, 2, T0+2s );
		DataChange( *Pump, Speed, 4, T0+4s );
		EXPECT_TRUE( Flush(*Pump) );
		DataChange( *Pump, Speed, 3, T0+3s );
		DataChange( *Pump, Speed, 5, T0+5s );
		EXPECT_TRUE( Flush(*Pump) );
		DataChange( *Pump, Speed, 4.5, T0+4s );
		DataChange( *Pump, Speed, 1, T0+1s );
		EXPECT_TRUE( Flush(*Pump) );
		DataChange( *Pump, Speed, 3.5, T0+3s );//buffered, after the file's at its time.
		ASSERT_EQ( Pump->Runs(March7).size(), 4 );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s)})), (vector<double>{1, 2, 3, 3.5, 4, 4.5, 5}) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0+3s), .End=ticks(T0+4s)})), (vector<double>{3, 3.5, 4, 4.5}) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0+4s), .End=ticks(T0+3s)})), (vector<double>{4.5, 4, 3.5, 3}) );
	}

	//A start after end reads later values first; end alone is "the last N"; start alone reads forward to the present.
	TEST_F( Reads, Backward ){
		TwoDays();
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .Start=ticks(Eighth+4min), .End=ticks(T0)})), (vector<double>{4, 3, 10, 2, 1}) );
		vector<uint> pages;
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .End=ticks(Eighth+4min), .Limit=2}, &pages)), (vector<double>{4, 3, 10, 2, 1}) );
		EXPECT_EQ( pages, (vector<uint>{2, 2, 1}) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .End=ticks(T0+2s), .Limit=10})), (vector<double>{2, 1}) );//the end is inside the range.
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .Start=ticks(Eighth+2min+30s)})), (vector<double>{4}) );
		EXPECT_EQ( doubles(All({.Nodes={Speed, Temp}, .Start=ticks(T0+2s)})), (vector<double>{2, 10, 3, 4}) );
		EXPECT_TRUE( All({.Nodes={Speed, Temp}, .End=ticks(T0)}).empty() );
	}

	//The continuation is stateless:  where to resume and how many at that time each node has had, and a CRC of the
	//arguments but Limit, so one passed with other nodes, times or bounds is refused.
	TEST_F( Reads, PagesWithAContinuation ){
		TwoDays();
		ReadRequest r{ .Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(Eighth+4min), .Limit=2 };
		auto page = Read( r );
		EXPECT_EQ( doubles(page.Values), (vector<double>{1, 2}) );
		ASSERT_FALSE( page.Continuation.empty() );
		let c = continuation( page );
		EXPECT_EQ( c.time(), ticks(T0+2s) );
		EXPECT_EQ( c.counts_size(), 2 );
		EXPECT_EQ( c.counts(0), 1 );
		EXPECT_EQ( c.counts(1), 0 );
		EXPECT_TRUE( c.has_generation() );
		EXPECT_EQ( c.generation(), 1 );
		EXPECT_GT( c.offset(), 0 );

		for( uint i=0; i<4; ++i ){
			auto other = r;
			switch( i ){
			case 0: other.Nodes = {Speed}; break;
			case 1: other.Nodes = {Temp, Speed}; break;
			case 2: other.Bounds = true; break;
			case 3: other.End = ticks( Eighth+5min ); break;
			}
			other.Continuation = page.Continuation;
			EXPECT_THROW( Read(other), Exception ) << i;
		}
		r.Continuation = "not a continuation";
		EXPECT_THROW( Read(r), Exception );

		r.Continuation = page.Continuation;
		r.Limit = 1;
		auto second = Read( r );
		EXPECT_EQ( doubles(second.Values), (vector<double>{10}) );
		EXPECT_EQ( continuation(second).counts(1), 1 );
		r.Continuation = second.Continuation;
		r.Limit = 5;
		auto rest = Read( r );
		EXPECT_EQ( doubles(rest.Values), (vector<double>{3, 4}) );
		EXPECT_TRUE( rest.Continuation.empty() );
	}

	//A page that stops in an archive resumes at its byte offset while the file's generation matches, and by time once a
	//merge has moved it on:  a record that landed behind the resume point isn't in the rest of the read.
	TEST_F( Reads, ResumesAnArchiveAtItsOffset ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		for( uint i=1; i<=5; ++i )
			DataChange( *Pump, Speed, i, T0+seconds{i} );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( Eighth+1min );
		Settle( *Pump );
		ReadRequest r{ .Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s), .Limit=2 };
		auto first = Read( r );
		EXPECT_EQ( doubles(first.Values), (vector<double>{1, 2}) );
		let c1 = continuation( first );
		EXPECT_EQ( c1.generation(), 1 );
		r.Continuation = first.Continuation;
		auto second = Read( r );
		EXPECT_EQ( doubles(second.Values), (vector<double>{3, 4}) );
		let c2 = continuation( second );
		EXPECT_EQ( c2.generation(), 1 );
		EXPECT_GT( c2.offset(), c1.offset() );
		EXPECT_EQ( c2.time(), ticks(T0+4s) );

		DataChange( *Pump, Speed, 2.5, T0+2500ms );
		DataChange( *Pump, Speed, 4.5, T0+4500ms );
		EXPECT_TRUE( Flush(*Pump) );//merged at once, into generation 2.
		r.Continuation = second.Continuation;
		auto third = Read( r );
		EXPECT_EQ( doubles(third.Values), (vector<double>{4.5, 5}) );
		EXPECT_TRUE( third.Continuation.empty() );
		r.Continuation = first.Continuation;
		EXPECT_EQ( doubles(Read(r).Values), (vector<double>{2.5, 3}) );
		r.Continuation.clear();
		r.Limit = 3;
		auto again = Read( r );
		EXPECT_EQ( continuation(again).generation(), 2 );
		r.Continuation = again.Continuation;
		EXPECT_EQ( doubles(Read(r).Values), (vector<double>{3, 4, 4.5}) );
	}

	//With bounds, each node's value at or before the earlier end and its first record at or after the later, a record
	//at the time itself serving as the bound, else Bad_BoundNotFound at that time.  Bounds count toward the limit.
	TEST_F( Reads, Bounds ){
		TwoDays();
		let v = All( {.Nodes={Speed, Temp}, .Start=ticks(T0+1500ms), .End=ticks(Eighth+1min), .Bounds=true} );
		ASSERT_EQ( v.size(), 6 );
		EXPECT_TRUE( isBound(v[0], Speed, 1, T0+1s) );
		EXPECT_TRUE( notFound(v[1], Temp, T0+1500ms) );
		EXPECT_EQ( doubles({v[2], v[3]}), (vector<double>{2, 10}) );
		EXPECT_FALSE( v[2].Bound || v[3].Bound );
		EXPECT_TRUE( notFound(v[4], Temp, Eighth+1min) );//the closing bounds by time:  one not found carries the end's.
		EXPECT_TRUE( isBound(v[5], Speed, 3, Eighth+2min) );

		let one = All( {.Nodes={Speed, Temp}, .Start=ticks(T0+2s), .End=ticks(T0+2s), .Bounds=true} );//the record at the time is both bounds.
		EXPECT_EQ( doubles(one), (vector<double>{2, 10}) );
		EXPECT_EQ( bounds(one), (vector<bool>{true, true}) );

		let open = All( {.Nodes={Speed, Temp}, .Start=ticks(T0+2s), .End=ticks(T0+3s), .Bounds=true} );
		ASSERT_EQ( open.size(), 4 );
		EXPECT_TRUE( isBound(open[0], Speed, 2, T0+2s) );
		EXPECT_TRUE( isBound(open[1], Temp, 10, T0+2s) );
		EXPECT_TRUE( notFound(open[2], Temp, T0+3s) );
		EXPECT_TRUE( isBound(open[3], Speed, 3, Eighth+2min) );//through the later day's records.

		let openEnd = All( {.Nodes={Speed}, .Start=ticks(T0+1500ms), .Bounds=true} );//an open end has no bound.
		EXPECT_EQ( doubles(openEnd), (vector<double>{1, 2, 3, 4}) );
		EXPECT_EQ( bounds(openEnd), (vector<bool>{true, false, false, false}) );

		vector<uint> pages;
		let paged = All( {.Nodes={Speed, Temp}, .Start=ticks(T0+1500ms), .End=ticks(Eighth+1min), .Bounds=true, .Limit=3}, &pages );
		EXPECT_EQ( pages, (vector<uint>{3, 3}) );//the opening bounds count; the closing ones follow the last page's values.
		ASSERT_EQ( paged.size(), 6 );
		EXPECT_EQ( bounds(paged), (vector<bool>{true, true, false, false, true, true}) );
		EXPECT_EQ( doubles({paged[2], paged[3]}), (vector<double>{2, 10}) );

		pages.clear();
		let tight = All( {.Nodes={Speed, Temp}, .Start=ticks(T0+1500ms), .End=ticks(Eighth+1min), .Bounds=true, .Limit=1}, &pages );
		EXPECT_EQ( pages, (vector<uint>{2, 1, 3}) );//a first page the bounds alone pass holds them; the closing bounds follow the last page's value.
		EXPECT_EQ( bounds(tight), (vector<bool>{true, true, false, false, true, true}) );
		EXPECT_EQ( doubles({tight[2], tight[3]}), (vector<double>{2, 10}) );
	}

	//The closing bound of a node quiet past the range is found through the later days' preambles:  each read until
	//its start value is past the range, which puts the record in the day before, and the newest day read through.
	TEST_F( Reads, ClosingBoundThroughLaterPreambles ){
		TwoDays();
		Time->AdvanceTo( sys_days{March9}+1min );
		Settle( *Pump );
		DataChange( *Pump, Speed, 5, sys_days{March9}+2min );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( sys_days{March10}+1min );
		Settle( *Pump );
		DataChange( *Pump, Speed, 6, sys_days{March10}+2min );
		EXPECT_TRUE( Flush(*Pump) );
		const ReadRequest r{ .Nodes={Temp}, .Start=ticks(T0+3s), .End=ticks(T0+4s), .Bounds=true };
		auto v = All( r );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( isBound(v[0], Temp, 10, T0+2s) );
		EXPECT_TRUE( notFound(v[1], Temp, T0+4s) );

		DataChange( *Pump, Temp, 11, sys_days{March10}+5min );//the newest day's own records, which no later preamble tells of.
		v = All( r );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( isBound(v[1], Temp, 11, sys_days{March10}+5min) );
		EXPECT_TRUE( Flush(*Pump) );
		v = All( r );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( isBound(v[1], Temp, 11, sys_days{March10}+5min) );

		Time->AdvanceTo( sys_days{March11}+1min );//the 11th's preamble puts it in the 10th.
		Settle( *Pump );
		DataChange( *Pump, Speed, 7, sys_days{March11}+2min );
		DataChange( *Pump, Temp, 12, sys_days{March11}+3min );
		EXPECT_TRUE( Flush(*Pump) );
		v = All( r );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( isBound(v[1], Temp, 11, sys_days{March10}+5min) );
		v = All( {.Nodes={Temp}, .Start=ticks(sys_days{March10}+6min), .End=ticks(sys_days{March10}+7min), .Bounds=true} );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( isBound(v[0], Temp, 11, sys_days{March10}+5min) );
		EXPECT_TRUE( isBound(v[1], Temp, 12, sys_days{March11}+3min) );
	}

	//Reverse, the bounds at the later end open the read and those at the earlier end close it.
	TEST_F( Reads, ReverseBounds ){
		TwoDays();
		let v = All( {.Nodes={Speed, Temp}, .Start=ticks(Eighth+1min), .End=ticks(T0+1500ms), .Bounds=true} );
		ASSERT_EQ( v.size(), 6 );
		EXPECT_TRUE( isBound(v[0], Speed, 3, Eighth+2min) );
		EXPECT_TRUE( notFound(v[1], Temp, Eighth+1min) );
		EXPECT_EQ( doubles({v[2], v[3]}), (vector<double>{10, 2}) );
		EXPECT_FALSE( v[2].Bound || v[3].Bound );
		EXPECT_TRUE( notFound(v[4], Temp, T0+1500ms) );
		EXPECT_TRUE( isBound(v[5], Speed, 1, T0+1s) );

		vector<uint> pages;
		let paged = All( {.Nodes={Speed, Temp}, .Start=ticks(Eighth+1min), .End=ticks(T0+1500ms), .Bounds=true, .Limit=2}, &pages );
		EXPECT_EQ( pages, (vector<uint>{2, 4}) );
		EXPECT_EQ( bounds(paged), bounds(v) );
		EXPECT_EQ( doubles({paged[2], paged[3]}), (vector<double>{10, 2}) );

		let one = All( {.Nodes={Speed, Temp}, .Start=ticks(T0+2s), .End=ticks(T0+1s), .Bounds=true} );
		ASSERT_EQ( one.size(), 4 );
		EXPECT_TRUE( isBound(one[0], Temp, 10, T0+2s) );//the records at each end serve as the bounds, latest first.
		EXPECT_TRUE( isBound(one[1], Speed, 2, T0+2s) );
		EXPECT_TRUE( isBound(one[2], Speed, 1, T0+1s) );
		EXPECT_TRUE( notFound(one[3], Temp, T0+1s) );

		let lastN = All( {.Nodes={Temp}, .End=ticks(Eighth+4min), .Bounds=true, .Limit=5} );
		ASSERT_EQ( lastN.size(), 2 );
		EXPECT_TRUE( notFound(lastN[0], Temp, Eighth+4min) );
		EXPECT_EQ( doubles({lastN[1]}), (vector<double>{10}) );
		EXPECT_FALSE( lastN[1].Bound );
	}

	//A reverse read of a node quiet for days jumps by its start values rather than walking the days between.
	TEST_F( Reads, ReverseOverQuietDays ){
		TwoDays();
		for( let day : {March9, March10, March11} ){
			Time->AdvanceTo( sys_days{day}+1min );
			Settle( *Pump );
			DataChange( *Pump, Speed, (double)(unsigned)day.day(), sys_days{day}+2min );
			EXPECT_TRUE( Flush(*Pump) );
		}
		EXPECT_EQ( doubles(All({.Nodes={Temp}, .End=ticks(sys_days{March9}+1h), .Limit=5})), (vector<double>{10}) );
		let v = All( {.Nodes={Temp}, .End=ticks(sys_days{March11}+1h), .Bounds=true, .Limit=5} );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( notFound(v[0], Temp, sys_days{March11}+1h) );
		EXPECT_EQ( doubles({v[1]}), (vector<double>{10}) );
		EXPECT_EQ( doubles(Read({.Nodes={Speed, Temp}, .End=ticks(sys_days{March11}+1h), .Limit=3}).Values), (vector<double>{11, 10, 9}) );
	}

	//A range whose first day has no file takes the bound before it from the file before:  the node's last record there,
	//or that file's start value for it.
	TEST_F( Reads, BoundFromTheFileBefore ){
		TwoDays();
		Time->AdvanceTo( sys_days{March9}+1min );
		Settle( *Pump );
		for( let day : {March9, March10} ){
			SCOPED_TRACE( Ƒ("{}", DayDirectory(day).string()) );
			let v = All( {.Nodes={Speed, Temp}, .Start=ticks(sys_days{day}+2h), .End=ticks(sys_days{day}+3h), .Bounds=true} );
			ASSERT_EQ( v.size(), 4 );
			EXPECT_TRUE( isBound(v[0], Temp, 10, T0+2s) );//the 8th's start value for it.
			EXPECT_TRUE( isBound(v[1], Speed, 4, Eighth+3min) );//the 8th's last record of it.
			EXPECT_TRUE( notFound(v[2], Speed, sys_days{day}+3h) );
			EXPECT_TRUE( notFound(v[3], Temp, sys_days{day}+3h) );
		}
		DataChange( *Pump, Speed, 5, sys_days{March9}+2h+30min );//buffered, with no file for its day.
		let v = All( {.Nodes={Speed}, .Start=ticks(sys_days{March9}+2h+45min), .End=ticks(sys_days{March9}+3h), .Bounds=true} );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( isBound(v[0], Speed, 5, sys_days{March9}+2h+30min) );
		let reverse = All( {.Nodes={Speed}, .Start=ticks(sys_days{March9}+3h), .End=ticks(sys_days{March9}+2h+45min), .Bounds=true} );
		ASSERT_EQ( reverse.size(), 2 );
		EXPECT_TRUE( isBound(reverse[1], Speed, 5, sys_days{March9}+2h+30min) );
	}

	//A read carries nothing past a NodeRemoved:  the bound before the range is then not found.
	TEST_F( Reads, NodeRemovedClearsTheBound ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		DataChange( *Pump, Speed, 1, T0+1s );
		Time->Advance( 5s );
		let held = All( {.Nodes={Speed}, .Start=ticks(T0+10s), .End=ticks(T0+20s), .Bounds=true} );
		ASSERT_EQ( held.size(), 2 );
		EXPECT_TRUE( isBound(held[0], Speed, 1, T0+1s) );
		EXPECT_TRUE( notFound(held[1], Speed, T0+20s) );
		Pump->Remove( Speed, Admin );
		let v = All( {.Nodes={Speed}, .Start=ticks(T0+10s), .End=ticks(T0+20s), .Bounds=true} );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( notFound(v[0], Speed, T0+10s) );
		EXPECT_TRUE( notFound(v[1], Speed, T0+20s) );
		EXPECT_TRUE( Flush(*Pump) );
		let flushed = All( {.Nodes={Speed}, .Start=ticks(T0+10s), .End=ticks(T0+20s), .Bounds=true} );
		ASSERT_EQ( flushed.size(), 2 );
		EXPECT_TRUE( notFound(flushed[0], Speed, T0+10s) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+20s)})), (vector<double>{1}) );//its records stay.
	}

	//Limit is capped at hist.readLimit, which 0 takes.
	TEST_F( Reads, LimitFollowsReadLimit ){
		auto config = Config( 1min );
		config.ReadLimit = 2;
		Restart( move(config) );
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		for( uint i=1; i<=5; ++i )
			DataChange( *Pump, Speed, i, T0+seconds{i} );
		vector<uint> pages;
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s)}, &pages)), (vector<double>{1, 2, 3, 4, 5}) );
		EXPECT_EQ( pages, (vector<uint>{2, 2, 1}) );
		pages.clear();
		All( {.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s), .Limit=10}, &pages );
		EXPECT_EQ( pages, (vector<uint>{2, 2, 1}) );
		pages.clear();
		All( {.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s), .Limit=1}, &pages );
		EXPECT_EQ( pages, vector<uint>(5, 1) );
	}

	//What a running flush took and hasn't yet written is read all the same:  no record is in neither the buffer nor a file.
	//The executor is held, so the read lands between the flush's Taking and its Commit every time.
	TEST_F( Reads, ReadsWhatAFlushIsWriting ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		for( uint i=1; i<=3; ++i )
			DataChange( *Pump, Speed, i, T0+seconds{i} );
		const ReadRequest request{ .Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s) };
		{
			HeldExecutor held;
			Time->Advance( 1min );//the flush at `delay`, which takes the buffer and waits on its write.
			EXPECT_TRUE( Pump->Buffer().empty() );
			EXPECT_TRUE( Pump->Runs(March7).empty() );//nothing committed
			EXPECT_EQ( doubles(All(request)), (vector<double>{1, 2, 3}) );
		}
		Settle( *Pump );
		EXPECT_FALSE( Pump->Runs(March7).empty() );
		EXPECT_EQ( doubles(All(request)), (vector<double>{1, 2, 3}) );
		EXPECT_TRUE( Pump->Buffer().empty() );
	}

	//A reverse read jumps over the days that hold nothing of its nodes though one has left the group, which no later
	//preamble lists:  its newest record ends it.  The days between aren't opened, so an unreadable one doesn't fail it.
	TEST_F( Reads, ReverseJumpsPastANodeThatLeft ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		Temp = Join( *Pump, "Pump1.Temp" );
		let flow = Join( *Pump, "Pump1.Flow" );
		DataChange( *Pump, Speed, 1, T0+1s );
		DataChange( *Pump, Temp, 10, T0+2s );
		EXPECT_TRUE( Flush(*Pump) );
		Time->AdvanceTo( Eighth+1min );
		Settle( *Pump );
		DataChange( *Pump, Temp, 11, Eighth+2min );
		Pump->Remove( Temp );
		EXPECT_TRUE( Flush(*Pump) );
		for( uint d=1; d<=5; ++d ){//Flow alone, March 9 through 13, each day's file rewritten at the next midnight.
			Time->AdvanceTo( Eighth+days{d}+1min );
			Settle( *Pump );
			DataChange( *Pump, flow, d, Time->Now() );
			EXPECT_TRUE( Flush(*Pump) );
		}
		let file = File( *Pump, March10 );//an archive the process has forgotten, so a read would scan it.
		fs::permissions( file, fs::perms::none, fs::perm_options::replace );
		if( std::ifstream{file}.is_open() ){
			fs::permissions( file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace );
			GTEST_SKIP() << "This user can open a file it has no permission to read.";
		}
		const ReadRequest request{ .Nodes={Speed, Temp}, .End=ticks(Time->Now()) };
		EXPECT_EQ( doubles(All(request)), (vector<double>{11, 10, 1}) );
		fs::permissions( file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace );
		EXPECT_EQ( doubles(All(request)), (vector<double>{11, 10, 1}) );
	}

	//A reverse read seeds its closing bounds only on the page that reaches the range's first day, so the pages before
	//don't read the file before that day through:  here March 7's, since March 8, the first day, has none.
	TEST_F( Reads, ReverseSeedsOnlyItsLastPage ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		DataChange( *Pump, Speed, 1, T0+1s );
		EXPECT_TRUE( Flush(*Pump) );
		const TimePoint ninth{ sys_days{March9} };
		Time->AdvanceTo( ninth+1min );//March 7's file rewritten, and none for March 8.
		Settle( *Pump );
		for( uint i=2; i<=5; ++i )
			DataChange( *Pump, Speed, i, ninth+1min+seconds{i} );
		EXPECT_TRUE( Flush(*Pump) );
		let file = File( *Pump, March7 );//unreadable, so a read that opens it fails.
		fs::permissions( file, fs::perms::none, fs::perm_options::replace );
		if( std::ifstream{file}.is_open() ){
			fs::permissions( file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace );
			GTEST_SKIP() << "This user can open a file it has no permission to read.";
		}
		const ReadRequest request{ .Nodes={Speed}, .Start=ticks(ninth+1h), .End=ticks(Eighth+12h), .Bounds=true, .Limit=2 };
		let page = Read( request );
		EXPECT_EQ( doubles(page.Values).size(), 2 );
		EXPECT_FALSE( page.Continuation.empty() );
		fs::permissions( file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace );
		let all = All( request );
		ASSERT_EQ( all.size(), 6 );
		EXPECT_TRUE( notFound(all[0], Speed, ninth+1h) );
		EXPECT_EQ( doubles({all.begin()+1, all.end()-1}), (vector<double>{5, 4, 3, 2}) );
		EXPECT_TRUE( isBound(all.back(), Speed, 1, T0+1s) );
	}

	//A day's file removed while the process still knows it, today's here, reads as a purged day does, with no records:
	//neither a read over that day nor one of its buffered nodes elsewhere fails.  The next flush makes it again.
	TEST_F( Reads, RemovedDayReadsAsPurged ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		DataChange( *Pump, Speed, 1, T0+1s );
		EXPECT_TRUE( Flush(*Pump) );
		DataChange( *Pump, Speed, 2, T0+2s );
		ASSERT_TRUE( fs::remove(File(*Pump, March7)) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s)})), (vector<double>{2}) );
		EXPECT_TRUE( All({.Nodes={Speed}, .Start=ticks(T0-48h), .End=ticks(T0-24h)}).empty() );
		EXPECT_TRUE( Flush(*Pump) );
		EXPECT_TRUE( fs::exists(File(*Pump, March7)) );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s)})), (vector<double>{2}) );
	}

	//A day's file that the process doesn't know and can't open fails the read, at Error, rather than reading as a day with
	//no records.
	TEST_F( Reads, UnreadableDayFailsTheRead ){
		TwoDays();
		Time->Advance( 3min );
		EXPECT_TRUE( Flush(*Pump) );//two `delay`s past March 7's rewrite, so it forgets the archive.
		let file = File( *Pump, March7 );
		fs::permissions( file, fs::perms::none, fs::perm_options::replace );
		if( std::ifstream{file}.is_open() ){
			fs::permissions( file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace );
			GTEST_SKIP() << "This user can open a file it has no permission to read.";
		}
		const ReadRequest request{ .Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+4min) };
		try{
			Read( request );
			ADD_FAILURE() << "read past a day it can't open";
		}
		catch( const IO::IOException& e ){
			EXPECT_EQ( e.Level(), ELogLevel::Error );
		}
		fs::permissions( file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace );
		EXPECT_EQ( doubles(All(request)), (vector<double>{1, 2, 3, 4}) );
	}

	//So does one in a day directory the process can't search, which the day walk keeps rather than passes:  a read that
	//reaches the day fails, and one that doesn't is served.
	TEST_F( Reads, UnsearchableDayFailsTheRead ){
		TwoDays();
		Time->Advance( 3min );
		EXPECT_TRUE( Flush(*Pump) );//two `delay`s past March 7's rewrite, so it forgets the archive.
		let file = File( *Pump, March7 );
		let dir = file.parent_path();
		fs::permissions( dir, fs::perms::none, fs::perm_options::replace );
		std::error_code ec;
		if( fs::exists(file, ec) || !ec ){
			fs::permissions( dir, fs::perms::owner_all, fs::perm_options::replace );
			GTEST_SKIP() << "This user can stat a file in a directory it can't search.";
		}
		const ReadRequest request{ .Nodes={Speed}, .Start=ticks(T0), .End=ticks(Eighth+4min) };
		try{
			Read( request );
			ADD_FAILURE() << "read past a day it can't search";
		}
		catch( const IO::IOException& e ){
			EXPECT_EQ( e.Level(), ELogLevel::Error );
		}
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(Eighth), .End=ticks(Eighth+4min)})), (vector<double>{3, 4}) );
		fs::permissions( dir, fs::perms::owner_all, fs::perm_options::replace );
		EXPECT_EQ( doubles(All(request)), (vector<double>{1, 2, 3, 4}) );
	}

	//What a full buffer dropped is read as the next flush writes it:  the gap's Bad_DataLost marker at the first dropped
	//value's time, then the newest dropped, written back, before the values still buffered.
	TEST_F( Reads, ReadsWhatATrimDropped ){
		auto config = Config( 1min );
		config.MaxBuffer = 4'000;
		Restart( move(config) );
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		Block();
		EXPECT_FALSE( Flush(*Pump) );//so the group knows its files are unwritable, and trims rather than flushes.
		for( uint n=1; n<=40; ++n ){
			DataChange( *Pump, Speed, n, T0+n*10ms );
			Time->Advance( 0s );//the trim's hop.
		}
		const ReadRequest request{ .Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+1s) };
		let buffered = All( request );
		ASSERT_GE( buffered.size(), 3 );
		EXPECT_EQ( buffered[0].Value.status(), UA_STATUSCODE_BADDATALOST );
		EXPECT_EQ( buffered[0].Value.source_ts(), ticks(T0+10ms) );
		EXPECT_EQ( buffered[1].Value.value().double_value()+1, buffered[2].Value.value().double_value() );
		Unblock();
		EXPECT_TRUE( Flush(*Pump) );
		let flushed = All( request );
		ASSERT_EQ( flushed.size(), buffered.size() );
		for( uint i=0; i<flushed.size(); ++i )
			EXPECT_EQ( flushed[i].Value.ShortDebugString(), buffered[i].Value.ShortDebugString() ) << i;
	}

	//What a flush has written and not yet finished with is read from its file, and not again from what it took:  the test
	//runs the held executor's handlers until the day is committed, which leaves the flush waiting on its .flushed write.
	TEST_F( Reads, ReadsWhatAFlushHasWritten ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		for( uint i=1; i<=3; ++i )
			DataChange( *Pump, Speed, i, T0+seconds{i} );
		const ReadRequest request{ .Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s) };
		{
			HeldExecutor held;
			Time->Advance( 1min );
			ASSERT_TRUE( held.RunUntil([this]{ return !Pump->Runs(March7).empty(); }) ) << "the day's commit";
			EXPECT_FALSE( Pump->Flushed() ) << "the flush has ended";
			EXPECT_EQ( doubles(All(request)), (vector<double>{1, 2, 3}) );
		}
		Settle( *Pump );
		EXPECT_TRUE( Pump->Flushed() );
		EXPECT_EQ( doubles(All(request)), (vector<double>{1, 2, 3}) );
	}

	//Each day the snapshot holds records for is served with it, file or none, so a flush that writes them before the read
	//reaches the day doesn't return them twice.  Here a late record makes its day's file, an archive from the start, which
	//takes the day's name only when the rewrite commits:  meanwhile the read crosses a month of archives.  The commit may
	//land before the read or after it instead, so the test can pass without that.
	TEST_F( Reads, FlushDuringTheReadReturnsItsRecordsOnce ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		constexpr uint count = 30;
		vector<double> expected;
		for( uint i=0; i<count; ++i ){
			DataChange( *Pump, Speed, i, T0-days{count+2-i} );
			expected.push_back( i );
		}
		EXPECT_TRUE( Flush(*Pump) );
		DataChange( *Pump, Speed, 100, T0-days{2} );
		expected.push_back( 100 );
		Time->Advance( 1min );//the rewrite that makes its file, out on the executor.
		const ReadRequest request{ .Nodes={Speed}, .Start=ticks(T0-days{count+3}), .End=ticks(T0) };
		EXPECT_EQ( doubles(All(request)), expected );
		Settle( *Pump );
		EXPECT_EQ( doubles(All(request)), expected );
	}

	//A read opens each day's file as it serves the day, under the files lock, so a rewrite that renames its archive over
	//the day before the read reaches it leaves the read the file its runs describe.
	TEST_F( Reads, ServedFileOutlivesARewrite ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		for( uint i=1; i<=2; ++i ){
			DataChange( *Pump, Speed, i, T0+seconds{i} );
			EXPECT_TRUE( Flush(*Pump) );
		}
		let root = Path()/"served";
		let file = root/DayDirectory( March7 )/( Pump->Name()+".binpb" );
		fs::create_directories( file.parent_path() );
		fs::copy_file( File(*Pump, March7), file );
		GroupFiles files{ root, Pump->Name(), utc(), 1min, March7 };
		let served = files.Serve( March7, SRCE_CUR );
		ASSERT_TRUE( served );
		EXPECT_EQ( served->Generation, 0 ) << "a live file, read through its runs";
		let temp = root/"rewritten.tmp";
		save( temp, "rewritten" );
		Replace( temp, file );
		Merge merge{ served->File, served->Runs, {} };
		vector<double> values;
		for( Proto::HistoryRecord r; merge.Next(r); ){
			if( r.has_value() )
				values.push_back( r.value().value().double_value() );
		}
		EXPECT_EQ( values, (vector<double>{1, 2}) );
		Merge reopened{ ms<ReadHandle>(file), served->Runs, {} };//as the path opens now
		Proto::HistoryRecord r;
		EXPECT_THROW( while(reopened.Next(r)){}, IO::IOException );
	}

	//A heartbeat is a stored value, returned as its record marks it, and a Bad_DataLost marker a value with none.
	TEST_F( Reads, HeartbeatsAndMarkers ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed", {.MaxTimeInterval=10s} );
		DataChange( *Pump, Speed, 1, T0 );
		Time->Advance( 11s );
		Pump->Disconnected( Time->Now() );
		Pump->Connected();
		DataChange( *Pump, Speed, 2, T0+15s );
		let v = All( {.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+20s)} );
		ASSERT_EQ( v.size(), 4 );
		EXPECT_EQ( v[0].Value.value().double_value(), 1 );
		EXPECT_FALSE( v[0].Value.has_heartbeat() );
		EXPECT_EQ( v[1].Value.value().double_value(), 1 );
		EXPECT_TRUE( v[1].Value.has_heartbeat() );
		EXPECT_EQ( v[1].Value.heartbeat(), ticks(T0) );
		EXPECT_FALSE( v[2].Value.has_value() );
		EXPECT_EQ( v[2].Value.status(), UA_STATUSCODE_BADDATALOST );
		EXPECT_EQ( v[3].Value.value().double_value(), 2 );
		EXPECT_TRUE( Flush(*Pump) );
		let flushed = All( {.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+20s)} );
		ASSERT_EQ( flushed.size(), 4 );
		for( uint i=0; i<4; ++i )
			EXPECT_EQ( flushed[i].Value.ShortDebugString(), v[i].Value.ShortDebugString() ) << i;
	}

	//OpcServer's shape:  one node per read, each callback bounded by its limit.
	TEST_F( ServerFiles, ReadsOneNodeAtATime ){
		let speed = Historize( "Pump1.Speed" );
		let temp = Historize( "Pump1.Temp" );
		let t0 = Time->Now();
		for( uint i=1; i<=3; ++i ){
			SetValue( speed, i );
			SetValue( temp, 10+i );
			Time->Advance( 1s );
		}
		EXPECT_TRUE( Flush(*Server) );
		vector<uint> pages;
		EXPECT_EQ( doubles(readAll(*Server, {.Nodes={speed}, .Start=ticks(t0), .End=ticks(t0+10s), .Limit=2}, &pages)), (vector<double>{1, 2, 3}) );
		EXPECT_EQ( pages, (vector<uint>{2, 1}) );
		pages.clear();
		let v = readAll( *Server, {.Nodes={temp}, .End=ticks(t0+1s), .Bounds=true, .Limit=1}, &pages );
		ASSERT_EQ( v.size(), 2 );
		EXPECT_TRUE( isBound(v[0], temp, 12, t0+1s) );//the record at the end serves as its bound.
		EXPECT_FALSE( v[1].Bound );
		EXPECT_EQ( v[1].Value.value().double_value(), 11 );
		EXPECT_EQ( pages, (vector<uint>{1, 1}) );
	}

	//OpcServer's bound on a callback, which holds open62541's service lock:  a page ends with the day it read while another
	//is left to read, whatever it holds.  March 7 and 9 hold speed's values, and March 8 only temp's.
	TEST_F( ServerFiles, ReadsADayAPage ){
		let speed = Historize( "Pump1.Speed" );
		let temp = Historize( "Pump1.Temp" );
		EXPECT_FALSE( Server->Earliest() );//no file yet.
		let t0 = Time->Now();
		SetValue( speed, 1 );
		SetValue( temp, 10 );
		EXPECT_TRUE( Flush(*Server) );
		Time->AdvanceTo( sys_days{March8}+1h );
		Settle( *Server );
		SetValue( temp, 11 );
		EXPECT_TRUE( Flush(*Server) );
		Time->AdvanceTo( sys_days{March9}+1h );
		Settle( *Server );
		SetValue( speed, 3 );
		Time->Advance( 1s );
		SetValue( speed, 4 );
		EXPECT_TRUE( Flush(*Server) );
		Time->Advance( 1s );
		SetValue( speed, 5 );//buffered.
		let end = Time->Now()+1min;
		EXPECT_EQ( Server->Earliest(), TimePoint{sys_days{March7}} );

		vector<uint> pages;
		EXPECT_EQ( doubles(readAll(*Server, {.Nodes={speed}, .Start=ticks(t0), .End=ticks(end), .OneDay=true}, &pages)), (vector<double>{1, 3, 4, 5}) );
		EXPECT_EQ( pages, (vector<uint>{1, 0, 3}) );
		pages.clear();
		EXPECT_EQ( doubles(readAll(*Server, {.Nodes={speed}, .Start=ticks(t0), .End=ticks(end), .Limit=2, .OneDay=true}, &pages)), (vector<double>{1, 3, 4, 5}) );
		EXPECT_EQ( pages, (vector<uint>{1, 0, 2, 1}) );//the limit still ends a page inside a day.
		pages.clear();
		EXPECT_EQ( doubles(readAll(*Server, {.Nodes={speed}, .Start=ticks(t0), .OneDay=true}, &pages)), (vector<double>{1, 3, 4, 5}) );
		EXPECT_EQ( pages, (vector<uint>{1, 0, 3}) );

		//Back a day a page, and from March 8, which holds nothing of speed's, straight to its start value's day.
		pages.clear();
		EXPECT_EQ( doubles(readAll(*Server, {.Nodes={speed}, .Start=ticks(end), .End=ticks(t0), .OneDay=true}, &pages)), (vector<double>{5, 4, 3, 1}) );
		EXPECT_EQ( pages, (vector<uint>{3, 0, 1}) );
		pages.clear();
		EXPECT_EQ( doubles(readAll(*Server, {.Nodes={speed}, .End=ticks(end), .Limit=2, .OneDay=true}, &pages)), (vector<double>{5, 4, 3, 1}) );
		EXPECT_EQ( pages, (vector<uint>{2, 1, 0, 1}) );

		//The opening bounds lead the first page, though its day holds no value, and the closing ones end the last.
		pages.clear();
		let bounded = readAll( *Server, {.Nodes={speed}, .Start=ticks(t0+1s), .End=ticks(end), .Bounds=true, .OneDay=true}, &pages );
		ASSERT_EQ( bounded.size(), 5 );
		EXPECT_TRUE( isBound(bounded[0], speed, 1, t0) );
		let values = doubles( bounded );
		EXPECT_EQ( (vector<double>{values.begin(), values.begin()+4}), (vector<double>{1, 3, 4, 5}) );
		EXPECT_TRUE( notFound(bounded[4], speed, end) );
		EXPECT_EQ( pages, (vector<uint>{1, 0, 4}) );

		//One day's read is one page.
		let page = Server->Read( {.Nodes={speed}, .Start=ticks(sys_days{March9}), .End=ticks(end), .OneDay=true} );
		EXPECT_EQ( page.Values.size(), 3 );
		EXPECT_TRUE( page.Continuation.empty() );
	}

	//A UA host answers a continuation that isn't the read's with the status the exception carries.
	TEST_F( ServerFiles, RefusesAContinuationWithItsStatus ){
		let speed = Historize( "Pump1.Speed" );
		let t0 = Time->Now();
		for( uint i=1; i<=3; ++i ){
			SetValue( speed, i );
			Time->Advance( 1s );
		}
		let page = Server->Read( {.Nodes={speed}, .Start=ticks(t0), .Limit=2} );
		ASSERT_FALSE( page.Continuation.empty() );
		for( let& request : {ReadRequest{.Nodes={speed}, .Start=ticks(t0), .Limit=2, .Continuation="x"}, ReadRequest{.Nodes={speed}, .Start=ticks(t0+1s), .Limit=2, .Continuation=page.Continuation}} ){
			try{
				Server->Read( request );
				ADD_FAILURE() << "read with a continuation that isn't its own";
			}
			catch( const UAException& e ){
				EXPECT_EQ( e.Code(), UA_STATUSCODE_BADCONTINUATIONPOINTINVALID );
			}
		}
	}
}