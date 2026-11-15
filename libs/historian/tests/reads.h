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
	//A read paged as a UA host pages it, each page with a fresh budget of `budget` records when one is given:  its pages'
	//sizes, and the records each page took.
	struct Paging final{ vector<uint>* Pages{}; optional<uint> Budget; vector<uint>* Taken{}; };
	Ŧ readPaged( T request, Paging paging, auto read )ε->vector<ReadValue>{
		vector<ReadValue> y;
		for( uint count{}; ; ++count ){
			optional<ScanBudget> budget;
			if( paging.Budget )
				request.Budget = &budget.emplace( *paging.Budget );
			auto page = read( request );
			if( paging.Pages )
				paging.Pages->push_back( page.Values.size() );
			if( paging.Taken && budget )
				paging.Taken->push_back( budget->Taken );
			std::ranges::move( page.Values, std::back_inserter(y) );
			if( page.Continuation.empty() )
				break;
			THROW_IF( count>100'000, "A read that never ends." );
			request.Continuation = move( page.Continuation );
		}
		return y;
	}
	//Every page of a read, each page's size in pages.
	Ξ readAll( Group& group, ReadRequest request, vector<uint>* pages=nullptr )ε->vector<ReadValue>{
		return readPaged( move(request), {pages}, [&]( const ReadRequest& r ){ return group.Read( r ); } );
	}
	Ξ readAll( Group& group, ReadRequest request, Paging paging )ε->vector<ReadValue>{
		return readPaged( move(request), paging, [&]( const ReadRequest& r ){ return group.Read( r ); } );
	}
	//Every page of an at-time or a processed read, the same way.
	Ξ readAllAtTime( Group& group, AtTimeRequest request, vector<uint>* pages=nullptr )ε->vector<ReadValue>{
		return readPaged( move(request), {pages}, [&]( const AtTimeRequest& r ){ return group.ReadAtTime( r ); } );
	}
	Ξ readAllAtTime( Group& group, AtTimeRequest request, Paging paging )ε->vector<ReadValue>{
		return readPaged( move(request), paging, [&]( const AtTimeRequest& r ){ return group.ReadAtTime( r ); } );
	}
	Ξ readAllProcessed( Group& group, ProcessedRequest request, vector<uint>* pages=nullptr )ε->vector<ReadValue>{
		return readPaged( move(request), {pages}, [&]( const ProcessedRequest& r ){ return group.ReadProcessed( r ); } );
	}
	Ξ readAllProcessed( Group& group, ProcessedRequest request, Paging paging )ε->vector<ReadValue>{
		return readPaged( move(request), paging, [&]( const ProcessedRequest& r ){ return group.ReadProcessed( r ); } );
	}
	//A read's values as one string each, the bound flag with them:  two reads that return the same agree here.
	Ξ described( const vector<ReadValue>& values )ι->vector<string>{
		vector<string> y;
		for( const auto& v : values )
			y.push_back( Ƒ("{}{}", v.Bound ? "bound " : "", v.Value.ShortDebugString()) );
		return y;
	}
	//A result's number, whatever numeric type it carries:  an aggregate's Double or Int32, or a raw value's.
	Ξ numberOf( const Proto::DataValue& v )ι->optional<double>{
		if( !v.has_value() )
			return nullopt;
		switch( v.value().of_case() ){
		case Opc::Proto::Value::kDoubleValue: return v.value().double_value();
		case Opc::Proto::Value::kFloatValue: return (double)v.value().float_value();
		case Opc::Proto::Value::kInt32: return (double)v.value().int32();
		case Opc::Proto::Value::kInt64: return (double)v.value().int64();
		case Opc::Proto::Value::kUint32: return (double)v.value().uint32();
		default: return nullopt;
		}
	}
	Ξ statuses( const vector<ReadValue>& values )ι->vector<StatusCode>{
		vector<StatusCode> y;
		for( const auto& v : values )
			y.push_back( v.Value.status() );
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