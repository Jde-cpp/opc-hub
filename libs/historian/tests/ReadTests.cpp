//Raw reads (#205):  forward, reverse and open-ended, over archives, a live file's runs and the buffer; the stateless
//continuation, with its archive offset and generation check, and readLimit; and bounds, including the forward scan of
//later preambles.
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
	TEST_F( Reads, ReadsWhatAFlushIsWriting ){
		Pump = AddGroup();
		Speed = Join( *Pump, "Pump1.Speed" );
		for( uint i=1; i<=3; ++i )
			DataChange( *Pump, Speed, i, T0+seconds{i} );
		Time->Advance( 1min );//the flush at `delay`, whose write is out on the executor.
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s)})), (vector<double>{1, 2, 3}) );
		Settle( *Pump );
		EXPECT_EQ( doubles(All({.Nodes={Speed}, .Start=ticks(T0), .End=ticks(T0+10s)})), (vector<double>{1, 2, 3}) );
		EXPECT_TRUE( Pump->Buffer().empty() );
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
}