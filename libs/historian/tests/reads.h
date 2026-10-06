#pragma once
//A read as a test runs it:  every page, and what each value says.
#include "dayFiles.h"
DISABLE_WARNINGS
#include <jde/historian/proto/Hist.Read.pb.h>
ENABLE_WARNINGS

namespace Jde::Opc::Hist::Tests{
	constexpr Day March10{ 2026y/March/10 }, March11{ 2026y/March/11 };
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