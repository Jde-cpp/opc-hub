#pragma once
#include <numeric>
#include <random>
#include <absl/random/random.h>
#include <ranges>

namespace Jde{
	template<typename T=uint> α Round( double value )->T{
		return static_cast<T>( llround(value) );
	}
}
namespace Jde::Math{
	Τ struct StatResult{
		T Average{0.0};
		T Variance{0.0};
		T Min{0.0};
		T Max{0.0};
	};

	//Not cryptographically secure - use Crypto::Random for anything security-bearing (session ids, tokens, keys).
	Ξ BitGen()->absl::BitGen&{ static thread_local absl::BitGen gen; return gen; }//seeded from OS entropy, per thread.
	Ξ Random()->uint32{ return absl::Uniform<uint32_t>( BitGen() ); }//32 bits:  uint32 is uint_fast32_t, 64 wide on linux.
	Ξ Random( uint32 n )->uint32{ return absl::Uniform<uint32>( BitGen(), 0, n ); }//[0, n), unbiased - unlike Random()%n.

#define let const auto
	Ŧ Statistics( const T& values, bool calcVariance=true )ι->StatResult<typename T::value_type>{
		typedef typename T::value_type TValue;
		let size = values.size();
		//ASSERT( size>0 );
		TValue sum{};
		TValue min{ std::numeric_limits<TValue>::max() };
		TValue max{ std::numeric_limits<TValue>::lowest() };//min() is the smallest *positive* normal for floating point.
		TValue average{};
		TValue variance{};
		for( let& value : values ){
			sum += value;
			min = std::min( min, value );
			max = std::max( max, value );
		}
		if( size>0 ){
			average = sum/size;
			if( size>1 && calcVariance ){
				auto varianceFunction = [&average, &size]( double accumulator, const double& val )
				{
					let diff = val - average;
					return accumulator + diff*diff / (size - 1);//sample?
				};
				double v2 = std::accumulate( values.begin(), values.end(), 0.0, varianceFunction );
				variance = static_cast<TValue>( v2 );
			}
		}
		return StatResult<TValue>{ average, variance, min, max };
	}
#undef let
}