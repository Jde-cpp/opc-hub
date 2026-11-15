//How long one UA callback's read takes over a busy day (#504), run by hand:  --gtest_also_run_disabled_tests, with the
//settings' /testing/tests at ScanBench.*.  A group of 7 or 100 nodes, each written at 1 Hz for HIST_BENCH_HOURS hours (24 by default)
//of March 8th, is read as UAHistory reads it, OneDay set, first from the live file, then from its archive, with no scan
//budget and with hist.scanLimit's default each call:  each mode's callbacks, the longest and in all, and the day's records
//per ms of the longest.  It asserts nothing.
#include <iostream>
#include "dayFiles.h"

#define let const auto

namespace Jde::Opc::Hist::Tests{
	using namespace std::chrono;
	namespace{
		Ω benchHours()ι->uint{
			let env = std::getenv( "HIST_BENCH_HOURS" );
			return env ? std::max( 1ul, std::stoul(env) ) : 24ul;
		}
		struct Timing final{ uint Calls{}; steady_clock::duration Longest{}, Total{}; uint Values{}; };
		//Every page of a read, each timed as a callback is, under a budget of its own when one is given.
		Ŧ timed( T request, optional<uint> limit, auto read )ε->Timing{
			Timing y;
			for( ;; ){
				optional<ScanBudget> budget;
				if( limit )
					request.Budget = &budget.emplace( *limit );
				let start = steady_clock::now();
				auto page = read( request );
				let elapsed = steady_clock::now()-start;
				++y.Calls;
				y.Longest = std::max( y.Longest, elapsed );
				y.Total += elapsed;
				y.Values += page.Values.size();
				if( page.Continuation.empty() )
					break;
				request.Continuation = move( page.Continuation );
			}
			return y;
		}
	}

	struct ScanBench : GatewayFiles{
		α Load( uint nodes )ε->void{
			Time->AdvanceTo( sys_days{March8} );
			Pump = AddGroup();
			for( uint i=0; i<nodes; ++i )
				Nodes.push_back( Join(*Pump, Ƒ("Pump{}.Speed", i)) );
			let count = benchHours()*3600;
			Records = count*nodes;
			for( uint s=0; s<count; ++s ){
				let at = sys_days{March8}+seconds{s};
				for( uint i=0; i<nodes; ++i )
					ASSERT_TRUE( DataChange(*Pump, Nodes[i], (double)(s+i), at) );
				if( s%60==59 ){
					Time->AdvanceTo( at+1s );
					ASSERT_TRUE( Flush(*Pump) );
				}
			}
			ASSERT_TRUE( Flush(*Pump) );
		}
		α Archive()ε->void{
			Time->AdvanceTo( sys_days{March9}+2min );
			Settle( *Pump );
			ASSERT_EQ( generation(File(*Pump, March8)), 1 );
		}
		α Report( sv state )ε->void{
			const TimePoint day{ sys_days{March8} }, end{ day+std::chrono::hours{benchHours()} };
			let node = Nodes.front();
			vector<UA_DateTime> hourly;
			for( uint h=0; h<benchHours(); ++h )
				hourly.push_back( ticks(day+std::chrono::hours{h}+30min) );
			for( let limit : {optional<uint>{}, optional<uint>{Settings::DefaultScanLimit}} ){
				let budget = limit ? Ƒ( "{:>7}", *limit ) : string{ "     no" };
				let say = [&]( sv mode, const Timing& t ){
					let longest = duration_cast<microseconds>( t.Longest ).count();
					std::cout << Ƒ( "{:3} nodes {:7} {} budget {:14} {:5} calls, longest {:9} µs, all {:9} µs, {:7} values, {:8.0f} records/ms\n", Nodes.size(), state, budget, mode, t.Calls,
						longest, duration_cast<microseconds>(t.Total).count(), t.Values, longest ? (double)Records*1000/(double)longest : 0.0 );
				};
				say( "average 1 day", timed(ProcessedRequest{.Nodes={node}, .Start=ticks(day), .End=ticks(end), .Interval=end-day, .Aggregate=EAggregate::Average, .OneDay=true}, limit,
					[&]( const ProcessedRequest& r ){ return Pump->ReadProcessed( r ); }) );
				say( "raw forward", timed(ReadRequest{.Nodes={node}, .Start=ticks(day), .End=ticks(end)-1, .OneDay=true}, limit, [&]( const ReadRequest& r ){ return Pump->Read( r ); }) );
				say( "reverse last", timed(ReadRequest{.Nodes={node}, .End=ticks(end)-1, .Limit=1, .OneDay=true}, limit, [&]( const ReadRequest& r ){
					auto page = Pump->Read( r );
					page.Continuation.clear();//the value alone, as a last-value lookup asks.
					return page;
				}) );
				say( "at-time hourly", timed(AtTimeRequest{.Nodes={node}, .Times=hourly, .OneDay=true}, limit, [&]( const AtTimeRequest& r ){ return Pump->ReadAtTime( r ); }) );
			}
		}
		α Run( uint nodes )ε->void{
			Load( nodes );
			Report( "live" );
			Archive();
			Report( "archive" );
		}
		sp<Group> Pump;
		vector<NodeIndex> Nodes;
		uint Records{};
	};

	TEST_F( ScanBench, DISABLED_SevenNodes ){ Run( 7 ); }
	TEST_F( ScanBench, DISABLED_HundredNodes ){ Run( 100 ); }
}