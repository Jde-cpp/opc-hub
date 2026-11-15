//The scan budget (#504):  a UA callback lets a read take so many records, parsed or skimmed, and the page then ends where
//the read can resume, so a read paged under any budget returns what it does with none.  A read whose budget is spent
//before it starts waits for the next call, and one that can't stop part way holds its budget.
#include <jde/opc/UAException.h>
#include "reads.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	constexpr std::array<uint,5> Budgets{ 1, 2, 3, 7, 50 };

	//Three nodes over two days:  the 7th archived, written in runs, a node at some seconds twice and another missing
	//some; the 8th live, in runs and the buffer.
	struct ScanBudgets : GatewayFiles{
		α Load()ε->void{
			Pump = AddGroup();
			Speed = Join( *Pump, "Pump1.Speed" );
			Flow = Join( *Pump, "Pump1.Flow" );
			Temp = Join( *Pump, "Pump1.Temp" );
			T0 = Time->Now();
			Write( T0, 60 );
			Time->AdvanceTo( sys_days{March8}+1min );
			Settle( *Pump );
			ASSERT_EQ( generation(File(*Pump, March7)), 1 );
			T1 = Time->Now();
			Write( T1, 40, 15 );
		}
		//count seconds from `from`, a flush each 10 of them but those of the last `buffered`, which stay in the buffer
		//until a later flush.
		α Write( TimePoint from, uint count, uint buffered=0 )ε->void{
			for( uint i=0; i<count; ++i ){
				let at = from+seconds{i};
				ASSERT_TRUE( DataChange(*Pump, Speed, i, at) );
				if( i%5==0 )
					ASSERT_TRUE( DataChange(*Pump, Speed, i+0.5, at) );
				if( i%3 )
					ASSERT_TRUE( DataChange(*Pump, Flow, 100.0+i, at) );
				ASSERT_TRUE( DataChange(*Pump, Temp, 200.0+i, at) );
				if( i%10==9 && i+buffered<count )
					ASSERT_TRUE( Flush(*Pump) );
			}
		}
		//The reads the budgets page:  one node and two, with bounds and without, from inside a run, across both days,
		//under a Limit and with OneDay.
		α Requests()Ι->vector<ReadRequest>{
			let end = T1+40s;
			return {
				{.Nodes={Speed}, .Start=ticks(T0), .End=ticks(end)},
				{.Nodes={Flow}, .Start=ticks(T0+10s+500ms), .End=ticks(T1+20s), .Bounds=true},
				{.Nodes={Speed, Temp}, .Start=ticks(T0+5s), .End=ticks(T0+50s), .Bounds=true},
				{.Nodes={Temp, Flow}, .Start=ticks(T0+30s), .End=ticks(T1+35s), .Bounds=true, .Limit=4},
				{.Nodes={Speed}, .Start=ticks(T0+15s), .End=ticks(end), .Bounds=true, .OneDay=true},
				{.Nodes={Flow, Speed}, .Start=ticks(T0+45s)}
			};
		}
		sp<Group> Pump;
		NodeIndex Speed, Flow, Temp;
		TimePoint T0, T1;
	};

	//Each budget's pages make the read whole, and a page past the first takes about its budget:  what the records at
	//one time, the run a resume falls in and the closing bounds add.  An archive resumes at its place, or each page
	//would read the day from its start.
	TEST_F( ScanBudgets, ForwardReadsResume ){
		Load();
		let requests = Requests();
		for( uint i=0; i<requests.size(); ++i ){
			let whole = described( readAll(*Pump, requests[i]) );
			ASSERT_FALSE( whole.empty() ) << i;
			for( let budget : Budgets ){
				SCOPED_TRACE( Ƒ("request {}, budget {}", i, budget) );
				vector<uint> pages, taken;
				EXPECT_EQ( described(readAll(*Pump, requests[i], {.Pages=&pages, .Budget=budget, .Taken=&taken})), whole );
				if( budget<7 )
					EXPECT_GT( pages.size(), 3u );
				for( uint p=1; p+1<taken.size(); ++p )
					EXPECT_LE( taken[p], budget+80 ) << "page " << p;
			}
		}
	}

	//A node that falls quiet:  its pages go on past records of the others alone, each about its budget, a page that
	//resumes where the node has none to the day's end included, and a page whose budget the quiet stretch spends resumes
	//past what it skimmed, not at the node's last record, where the next would skim the stretch again.
	TEST_F( ScanBudgets, AQuietNodeGoesOn ){
		Pump = AddGroup();
		let quiet = Join( *Pump, "Pump1.Quiet" ), busy = Join( *Pump, "Pump1.Busy" );
		T0 = Time->Now();
		constexpr uint seconds = 600;
		for( uint i=0; i<seconds; ++i ){
			let at = T0+std::chrono::seconds{i};
			if( i<20 || i==seconds-1 )
				ASSERT_TRUE( DataChange(*Pump, quiet, i, at) );
			ASSERT_TRUE( DataChange(*Pump, busy, i, at) );
			if( i%60==59 )
				ASSERT_TRUE( Flush(*Pump) );
		}
		ReadRequest r{ .Nodes={quiet}, .Start=ticks(T0), .End=ticks(T0+std::chrono::seconds{seconds}) };
		let whole = described( readAll(*Pump, r) );
		ASSERT_EQ( whole.size(), 21u );
		{
			ScanBudget budget{ 100 };
			r.Budget = &budget;
			let first = Pump->Read( r );
			r.Budget = nullptr;
			EXPECT_EQ( first.Values.size(), 20u );//its records before the quiet stretch, whose skim spends the budget...
			EXPECT_GE( continuation(first).time(), ticks(T0+60s) );//...and where the next page resumes, past what that skim read.
		}
		for( let budget : {50u, 100u} ){
			SCOPED_TRACE( Ƒ("budget {}", budget) );
			vector<uint> pages, taken;
			EXPECT_EQ( described(readAll(*Pump, r, {.Pages=&pages, .Budget=budget, .Taken=&taken})), whole );
			for( uint p=0; p<taken.size(); ++p )
				EXPECT_LE( taken[p], budget+80 ) << "page " << p;
		}
		//Archived, each page resumes at its place:  the day is read about once in all.
		Time->AdvanceTo( sys_days{March8}+1min );
		Settle( *Pump );
		ASSERT_EQ( generation(File(*Pump, March7)), 1 );
		for( let budget : {50u, 100u} ){
			SCOPED_TRACE( Ƒ("archived, budget {}", budget) );
			vector<uint> pages, taken;
			EXPECT_EQ( described(readAll(*Pump, r, {.Pages=&pages, .Budget=budget, .Taken=&taken})), whole );
			EXPECT_LE( std::accumulate(taken.begin(), taken.end(), 0u), seconds+21+2*taken.size() );
		}
	}

	//A read whose budget is spent before it starts returns nothing, with its continuation again, or for a first page one
	//that starts it, its opening bounds included.
	TEST_F( ScanBudgets, SpentBeforeItStarts ){
		Load();
		ReadRequest r{ .Nodes={Flow, Temp}, .Start=ticks(T0+10s+500ms), .End=ticks(T1+20s), .Bounds=true, .Limit=5 };
		let first = Pump->Read( r );
		ScanBudget spent{ 2 };
		spent.Taken = 2;
		r.Budget = &spent;
		let waited = Pump->Read( r );
		EXPECT_TRUE( waited.Values.empty() );
		EXPECT_FALSE( waited.NoData );
		ASSERT_FALSE( waited.Continuation.empty() );
		EXPECT_TRUE( continuation(waited).unstarted() );
		EXPECT_EQ( spent.Taken, 2 );
		auto started = r;
		started.Budget = nullptr;
		started.Continuation = waited.Continuation;
		EXPECT_EQ( described(Pump->Read(started).Values), described(first.Values) );
		EXPECT_TRUE( first.Values.front().Bound );

		r.Continuation = first.Continuation;
		EXPECT_EQ( Pump->Read( r ).Continuation, first.Continuation );
	}

	//A reverse read holds its budget:  its pages are the ones it reads with none.
	TEST_F( ScanBudgets, ReverseReadsHold ){
		Load();
		for( let& r : {ReadRequest{.Nodes={Speed}, .Start=ticks(T1+35s), .End=ticks(T0+5s), .Bounds=true, .Limit=7}, ReadRequest{.Nodes={Temp, Flow}, .End=ticks(T1+20s), .Limit=9, .OneDay=true}} ){
			vector<uint> pages, held;
			let whole = described( readAll(*Pump, r, &pages) );
			EXPECT_EQ( described(readAll(*Pump, r, {.Pages=&held, .Budget=1})), whole );
			EXPECT_EQ( held, pages );
		}
	}

	//Holds nest, and a hold a throw leaves is let go.
	TEST( ScanBudgetHolds, Nest ){
		ScanBudget budget{ 3 };
		budget.Taken = 3;
		EXPECT_TRUE( budget.Exhausted() );
		{
			const ScanBudget::Hold outer{ &budget };
			{
				const ScanBudget::Hold inner{ &budget };
				EXPECT_FALSE( budget.Exhausted() );
			}
			EXPECT_FALSE( budget.Exhausted() );
			try{
				const ScanBudget::Hold thrown{ &budget };
				throw std::runtime_error{ "mid-read" };
			}
			catch( const std::exception& )
			{}
			EXPECT_FALSE( budget.Exhausted() );
		}
		EXPECT_TRUE( budget.Exhausted() );
		const ScanBudget::Hold none{ nullptr };
	}

	//An at-time page of one day's times in time order stops as the stream passes them, returning those it has, and
	//carries the rest:  its pages make what the read without a budget returns.  Times out of order, or over two days
	//in one page, hold the budget, and page as they would without it.
	TEST_F( ScanBudgets, AtTimesResume ){
		Load();
		vector<UA_DateTime> times;
		for( auto t = T0+1s+250ms; t<T0+58s; t+=3s+250ms )
			times.push_back( ticks(t) );
		vector<UA_DateTime> twice{ times };//each time twice over, in order.
		for( uint i=0; i<times.size(); ++i )
			twice.insert( twice.begin()+2*i, times[i] );
		vector<UA_DateTime> late{ ticks(T1+2s+500ms), ticks(T1+9s), ticks(T1+30s+100ms) };
		struct Case{ AtTimeRequest Request; bool Holds; };
		let cases = {
			Case{ {.Nodes={Speed, Flow}, .Times=times, .OneDay=true}, false },
			Case{ {.Nodes={Flow}, .Times=times, .SimpleBounds=true, .OneDay=true}, false },
			Case{ {.Nodes={Temp, Speed, Temp}, .Times=twice, .Limit=5, .OneDay=true}, false },
			Case{ {.Nodes={Flow, Temp}, .Times=late}, false },
			Case{ {.Nodes={Speed, Flow}, .Times={times.rbegin(), times.rend()}, .OneDay=true}, true },
			Case{ {.Nodes={Speed}, .Times={times.front(), late.front(), times.back(), late.back()}}, true }
		};
		uint i{};
		for( let& c : cases ){
			vector<uint> pages;
			let expected = described( readAllAtTime(*Pump, c.Request, &pages) );
			ASSERT_FALSE( expected.empty() );
			for( let budget : Budgets ){
				SCOPED_TRACE( Ƒ("case {}, budget {}", i, budget) );
				vector<uint> budgeted;
				EXPECT_EQ( described(readAllAtTime(*Pump, c.Request, {.Pages=&budgeted, .Budget=budget})), expected );
				if( c.Holds )
					EXPECT_EQ( budgeted, pages );
				else if( budget==1 )
					EXPECT_GT( budgeted.size(), pages.size() );
			}
			++i;
		}
	}

	//Processed reads over the archive, the live file and the buffer stop part way through their intervals and resume
	//there, under every aggregate the budget stops, forward and back:  each budget's pages make what the read without
	//one returns, and a page past the first takes about its budget, an archive resumed at its place.
	TEST_F( ScanBudgets, ProcessedResume ){
		Load();
		let start = ticks( T0+3s ), end = ticks( T1+30s );
		constexpr std::array aggregates{ EAggregate::Average, EAggregate::TimeAverage, EAggregate::Interpolative, EAggregate::Count, EAggregate::Minimum,
			EAggregate::Maximum, EAggregate::Start, EAggregate::End, EAggregate::StandardDeviationSample };
		vector<ProcessedRequest> requests;
		for( let aggregate : aggregates ){
			requests.push_back( {.Nodes={Speed}, .Start=start, .End=end, .Interval=7s, .Aggregate=aggregate, .OneDay=true} );
			requests.push_back( {.Nodes={Flow, Temp}, .Start=ticks(T0+20s+500ms), .End=ticks(T1+15s), .Interval=1min, .Aggregate=aggregate} );
			requests.push_back( {.Nodes={Speed}, .Start=start, .End=end, .Aggregate=aggregate, .OneDay=true} );//one interval.
			requests.push_back( {.Nodes={Temp, Flow}, .Start=end, .End=start, .Aggregate=aggregate} );//back, one interval.
		}
		for( uint i=0; i<requests.size(); ++i ){
			let expected = described( readAllProcessed(*Pump, requests[i]) );
			ASSERT_FALSE( expected.empty() ) << i;
			for( let budget : {1u, 4u} ){
				SCOPED_TRACE( Ƒ("request {}, budget {}", i, budget) );
				vector<uint> taken;
				EXPECT_EQ( described(readAllProcessed(*Pump, requests[i], {.Budget=budget, .Taken=&taken})), expected );
				for( uint p=1; p+1<taken.size(); ++p )
					EXPECT_LE( taken[p], budget+100 ) << "page " << p;
			}
		}
	}

	//An at-time or processed read whose budget is spent before it starts returns nothing, with its continuation again, or
	//one that starts it from its first time or interval.
	TEST_F( ScanBudgets, ComputedSpentBeforeItStarts ){
		Load();
		ScanBudget spent{ 1 };
		spent.Taken = 1;
		ProcessedRequest processed{ .Nodes={Speed, Temp}, .Start=ticks(T0), .End=ticks(T1+20s), .Interval=10s, .Aggregate=EAggregate::TimeAverage, .Limit=4, .Budget=&spent };
		let waited = Pump->ReadProcessed( processed );
		EXPECT_TRUE( waited.Values.empty() );
		EXPECT_EQ( continuation(waited).next(), 0 );
		processed.Budget = nullptr;
		let first = Pump->ReadProcessed( processed );
		processed.Continuation = waited.Continuation;
		EXPECT_EQ( described(Pump->ReadProcessed(processed).Values), described(first.Values) );
		processed.Continuation = first.Continuation;
		processed.Budget = &spent;
		EXPECT_EQ( Pump->ReadProcessed(processed).Continuation, first.Continuation );

		AtTimeRequest atTime{ .Nodes={Flow}, .Times={ticks(T0+5s), ticks(T0+6s)}, .Budget=&spent };
		let atWaited = Pump->ReadAtTime( atTime );
		EXPECT_TRUE( atWaited.Values.empty() );
		atTime.Budget = nullptr;
		atTime.Continuation = atWaited.Continuation;
		EXPECT_EQ( Pump->ReadAtTime(atTime).Values.size(), 2 );
	}

	//A carried pass the page can't rebuild is refused:  one ending past the read, or holding a boundary its page has
	//none at.
	TEST_F( ScanBudgets, ForeignPassIsRefused ){
		Load();
		ProcessedRequest r{ .Nodes={Speed}, .Start=ticks(T0), .End=ticks(T1+20s), .Aggregate=EAggregate::TimeAverage };
		ScanBudget budget{ 1 };
		r.Budget = &budget;
		let page = Pump->ReadProcessed( r );
		ASSERT_TRUE( continuation(page).has_pass() );
		r.Budget = nullptr;
		auto c = continuation( page );
		c.mutable_pass()->set_last( 5 );
		r.Continuation = c.SerializeAsString();
		EXPECT_THROW( Pump->ReadProcessed(r), UAException );
		c = continuation( page );
		auto& track = *c.mutable_pass()->mutable_tracks( 0 );
		track.set_settled( 1 );
		track.clear_boundaries();
		track.add_boundaries()->set_time( ticks(T0+1s) );
		r.Continuation = c.SerializeAsString();
		EXPECT_THROW( Pump->ReadProcessed(r), UAException );
	}
}