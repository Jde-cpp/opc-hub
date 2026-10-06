#pragma once
//A read as a test runs it:  every page, and what each value says.
#include "dayFiles.h"
#include <thread>
#include <boost/asio/io_context.hpp>
#include <jde/fwk/process/execution.h>
#include <jde/fwk/settings.h>
DISABLE_WARNINGS
#include <jde/historian/proto/Hist.Read.pb.h>
ENABLE_WARNINGS

namespace Jde::Opc::Hist::Tests{
	constexpr Day March10{ 2026y/March/10 }, March11{ 2026y/March/11 };
	//Every executor thread busy until it ends, so a flush's write, which completes there, waits:  the flush holds what it
	//took, between Taking and its Commit.  The test fails, rather than hangs, when the threads aren't all taken.
	struct HeldExecutor final{
		HeldExecutor()ι{
			const auto threads = std::max( 1u, Jde::Settings::FindNumber<unsigned>("/workers/executor/threads").value_or(std::thread::hardware_concurrency()) );
			for( uint i=0; i<threads; ++i )
				Post( [state=_state]{ ++state->Running; state->Released.wait( false ); } );
			for( const auto deadline = std::chrono::steady_clock::now()+5s; _state->Running<threads && std::chrono::steady_clock::now()<deadline; )
				std::this_thread::sleep_for( 1ms );
			EXPECT_EQ( _state->Running, threads ) << "executor threads held";
		}
		~HeldExecutor(){
			_state->Released = true;
			_state->Released.notify_all();
		}
		//Runs the executor's ready handlers on this thread, one at a time, until done:  false when it isn't within a few
		//seconds.  A flush moves on one await at a time, so the test can stop it between any two.
		α RunUntil( const std::function<bool()>& done )ι->bool{
			const auto ioc = Executor();
			for( const auto deadline = std::chrono::steady_clock::now()+5s; !done(); ){
				if( std::chrono::steady_clock::now()>=deadline )
					return false;
				ioc->run_one_for( 10ms );
			}
			return true;
		}
	private:
		struct State final{ std::atomic<uint> Running; std::atomic<bool> Released; };
		sp<State> _state{ ms<State>() };
	};
	//Every page of a read, each page's size in pages.
	Ξ readAll( Group& group, ReadRequest request, vector<uint>* pages=nullptr )ε->vector<ReadValue>{
		vector<ReadValue> y;
		for( uint count{}; ; ++count ){
			auto page = group.Read( request );
			if( pages )
				pages->push_back( page.Values.size() );
			std::ranges::move( page.Values, std::back_inserter(y) );
			if( page.Continuation.empty() )
				break;
			THROW_IF( count>100'000, "A read that never ends." );
			request.Continuation = move( page.Continuation );
		}
		return y;
	}
	Ξ doubles( const vector<ReadValue>& values )ι->vector<double>{
		vector<double> y;
		for( const auto& v : values )
			y.push_back( v.Value.has_value() ? v.Value.value().double_value() : std::numeric_limits<double>::quiet_NaN() );
		return y;
	}
	Ξ sources( const vector<ReadValue>& values )ι->vector<Ticks>{
		vector<Ticks> y;
		for( const auto& v : values )
			y.push_back( v.Value.source_ts() );
		return y;
	}
	Ξ bounds( const vector<ReadValue>& values )ι->vector<bool>{
		vector<bool> y;
		for( const auto& v : values )
			y.push_back( v.Bound );
		return y;
	}
	Ξ notFound( const ReadValue& v, NodeIndex index, TimePoint at )ι->bool{
		return v.Bound && !v.Value.has_value() && v.Value.status()==UA_STATUSCODE_BADBOUNDNOTFOUND && v.Value.node_index()==index && v.Value.source_ts()==ticks( at );
	}
	Ξ isBound( const ReadValue& v, NodeIndex index, double value, TimePoint source )ι->bool{
		return v.Bound && v.Value.has_value() && v.Value.value().double_value()==value && v.Value.node_index()==index && v.Value.source_ts()==ticks( source );
	}
	Ξ continuation( const ReadResult& page )ι->Proto::Continuation{
		Proto::Continuation y;
		EXPECT_TRUE( y.ParseFromString(page.Continuation) );
		return y;
	}
}