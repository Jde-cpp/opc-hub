#pragma once
#ifndef PROTO_UTILITIES_H
#define PROTO_UTILITIES_H
#pragma warning( push )
#pragma warning( disable : 4127 )
#pragma warning( disable : 5054 )
#include <google/protobuf/message.h>
#include <google/protobuf/timestamp.pb.h>
#pragma warning( pop )

#define let const auto
namespace Jde::Protobuf{
	Ŧ Deserialize( const vector<char>& data )ε->up<T>;
	Ŧ Deserialize( const google::protobuf::uint8* p, uint size )ε->T;
	Ŧ Deserialize( string&& x )ε->T;
	Ŧ FromVector( vector<T>&& x )ι->google::protobuf::RepeatedPtrField<T>;
	Ŧ ToVector( google::protobuf::RepeatedPtrField<T>&& x )ι->vector<T>;
	Ŧ ToVector( const google::protobuf::RepeatedField<T>& x )ι->vector<T>;

	inline constexpr int64_t MaxSeconds{ (std::numeric_limits<Duration::rep>::max)()/(Duration::period::den/Duration::period::num) - 1 };//± the seconds a TimePoint holds, less one for the nanos on top.
	α ToString( const google::protobuf::MessageLite& msg )ε->string;
	α ToTimestamp( TimePoint t )ι->google::protobuf::Timestamp;
	Ξ ToBytes( const uuid& g )ι->sv{ return sv{(char*)g.data(), 16}; }
	Ξ ToGuid( sv g )ι->uuid{ uuid y{}; memcpy(&y, g.data(), std::min(sizeof(uuid), g.size())); return y; }//value-init: short input must zero-fill the tail, not leave it indeterminate.
	α ToTimePoint( const google::protobuf::Timestamp& t )ι->TimePoint;
	α ToTimePoint( int64_t seconds, int64_t nanos )ι->TimePoint;//saturates: seconds to ±MaxSeconds, nanos to ±(1e9-1).

	namespace Internal{
		Ŧ Deserialize( const google::protobuf::uint8* p, uint size, T& proto )ε->void{
			THROW_IF( size>(uint)std::numeric_limits<int>::max(), "{} bytes exceeds protobuf's {} byte limit.", size, std::numeric_limits<int>::max() );//ParsePartialFromArray takes an int - a narrowing cast would silently negate/truncate the size.
			THROW_IF( !proto.ParsePartialFromArray(p, (int)size), "ParsePartialFromArray returned false." );//not MergePartialFromCodedStream: it answers true when a 0 or END_GROUP tag ends the parse early.
		}
	}
}


namespace Jde{
	Ξ Protobuf::ToString( const google::protobuf::MessageLite& msg )ε->string{
		string output;
		THROW_IF( !msg.SerializeToString(&output), "SerializeToString returned false." );
		return output;
	}

	Ŧ Protobuf::Deserialize( const vector<char>& data )ε->up<T>{
		auto p = mu<T>();
		Internal::Deserialize<T>( (google::protobuf::uint8*)data.data(), data.size(), *p );
		return p;
	}
	Ŧ Protobuf::Deserialize( const google::protobuf::uint8* p, uint size )ε->T{
		T y;
		Internal::Deserialize<T>( p, size, y );
		return y;
	}
	Ŧ Protobuf::Deserialize( string&& x )ε->T{
		T y;
		Internal::Deserialize<T>( (google::protobuf::uint8*)x.data(), x.size(), y );
		x.clear();
		return y;
	}

	Ŧ Protobuf::FromVector( vector<T>&& x )ι->google::protobuf::RepeatedPtrField<T>{
		google::protobuf::RepeatedPtrField<T> y;
		for( auto& item : x )
			*y.Add() = std::move( item );
		x.clear();
		return y;
	}
	Ŧ Protobuf::ToVector( google::protobuf::RepeatedPtrField<T>&& x )ι->vector<T>{
		vector<T> y; y.reserve( x.size() );
		for_each( x, [&y](auto& item){y.push_back(move(item));} );
		return y;
	}
	Ŧ Protobuf::ToVector( const google::protobuf::RepeatedField<T>& field )ι->vector<T>{
		return vector<T>{ std::begin(field), std::end(field) };
	}

	Ξ Protobuf::ToTimestamp( TimePoint t )ι->google::protobuf::Timestamp{
		let sinceEpoch = t.time_since_epoch();
		auto secs = duration_cast<std::chrono::seconds>( sinceEpoch );//toward zero, so sinceEpoch-secs can't overflow; floor's can, at TimePoint::min().
		auto rest = sinceEpoch-secs;
		if( rest<Duration::zero() ){//a Timestamp's nanos are in [0,1e9), before 1970 too.
			--secs;
			rest += std::chrono::seconds{ 1 };
		}
		google::protobuf::Timestamp proto;
		proto.set_seconds( secs.count() );
		proto.set_nanos( (int)duration_cast<std::chrono::nanoseconds>(rest).count() );
		return proto;
	}
	Ξ Protobuf::ToTimePoint( const google::protobuf::Timestamp& t )ι->TimePoint{
		return ToTimePoint( t.seconds(), t.nanos() );
	}
	Ξ Protobuf::ToTimePoint( int64_t secs, int64_t nanos )ι->TimePoint{
		constexpr int64_t maxNanos{ 999'999'999 };
		return TimePoint{}
			+ duration_cast<Duration>( std::chrono::seconds{std::clamp(secs, -MaxSeconds, MaxSeconds)} )
			+ duration_cast<Duration>( std::chrono::nanoseconds{std::clamp(nanos, -maxNanos, maxNanos)} );
	}
}
#undef let
#endif