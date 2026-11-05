#include <jde/opc/uatypes/DateTime.h>
#include <limits>
#include <jde/fwk/io/protobuf.h>
#include <jde/opc/uatypes/Logger.h>

#define let const auto

namespace Jde::Opc{
	using namespace std::chrono;
	//The vendor's own constant, not UA_DateTime_fromUnixTime(0):  that is an exported function, so this global was
	//dynamically initialised through the PLT - a static-initialisation-order hazard for any future global that touches a
	//UADateTime, and not usable in a constant expression.  The two agree by construction - fromUnixTime is documented as
	//`(unixDate * UA_DATETIME_SEC) + UA_DATETIME_UNIX_EPOCH` - and DateTimeTests pins that they still do.
	constexpr UA_DateTime _ua1970 = UA_DATETIME_UNIX_EPOCH;

	//The tick span that survives conversion into TimePoint's Duration.  duration_cast scales by
	//UATick::period/Duration::period, so a nanosecond clock (libstdc++) caps it at ~292 years either side of 1970, while
	//a 100ns (msvc stl) or microsecond (libc++) clock does not narrow it at all.  Deriving it beats the old #if on the
	//compiler: what matters is the clock's resolution, not who compiled it.
	using TickRatio = std::ratio_divide<UATick::period, Duration::period>;
	constexpr UA_Int64 _maxTicks = TickRatio::num>TickRatio::den //(numeric_limits<>::max) parenthesized defensively; this target sets NOMINMAX, so windows.h's macro is not actually in scope.
		? (UA_Int64)( (std::numeric_limits<Duration::rep>::max)()/(TickRatio::num/TickRatio::den) )
		: (std::numeric_limits<UA_Int64>::max)();
	constexpr UA_Int64 _minTicks = -_maxTicks;

	//Clamp rather than wrap.  An extreme UA_DateTime - INT64_MIN is a common "no value" sentinel - used to overflow in
	//`dt - _ua1970` before any conversion saw it, so INT64_MIN and INT64_MAX both came back as the same 2185-ish date.
	Ω toTicks( UA_DateTime dt )ι->UA_Int64{
		if( dt<0 && dt<std::numeric_limits<UA_Int64>::min()+_ua1970 )//the subtraction itself would wrap.
			return _minTicks;
		let ticks = dt-_ua1970;
		return ticks<_minTicks ? _minTicks : ticks>_maxTicks ? _maxTicks : ticks;
	}

	//Clamp on the way out too.  A TimePoint holds instants outside UA_DateTime's own range (~±9.1e11 s either side of
	//1970), and both the scale up to 100ns ticks and the +_ua1970 shift wrap there - seconds inside the TimePoint guard
	//came back as a date on the far side of INT64.  The two bounds are not symmetric: UA_DateTime counts from 1601, so
	//1970 is not centred in it.  duration_cast truncates toward zero, so each bound is conservative by under a tick.
	Ω fromTimePoint( const TimePoint& tp )ι->UA_DateTime{
		using Limits = std::numeric_limits<UA_Int64>;
		let sinceEpoch = tp-Chrono::Epoch();
		constexpr Duration maxSince = duration_cast<Duration>( UATick{(Limits::max)()-_ua1970} );
		constexpr Duration minSince = duration_cast<Duration>( UATick{(Limits::min)()+_ua1970} );
		return sinceEpoch>=maxSince ? (Limits::max)()
			: sinceEpoch<=minSince ? (Limits::min)()
			: duration_cast<UATick>( sinceEpoch ).count()+_ua1970;
	}

	constexpr _int _nanosPerSecond = 1'000'000'000;

	UADateTime::UADateTime( const UA_DateTime& dt )ι:
		_time{ Chrono::Epoch()+duration_cast<Duration>(UATick{toTicks(dt)}) }
	{}

	UADateTime::UADateTime( const jvalue& v, SL sl )ε{
		let at = [&]( _int s, _int ns )ε->TimePoint{
			if( s>Protobuf::MaxSeconds || s<-Protobuf::MaxSeconds )
				throw Exception{ sl, (ELogTags)EOpcLogTags::Opc, "DateTime seconds {} is outside the ±{} a TimePoint holds: {}", s, Protobuf::MaxSeconds, serialize(v) };
			//Bounding the seconds alone left the sum overflowing at the bound; a protobuf Timestamp's nanos are defined
			//to be in [0,1e9) anyway, so anything else is malformed rather than "extra seconds".
			if( ns>=_nanosPerSecond || ns<=-_nanosPerSecond )
				throw Exception{ sl, (ELogTags)EOpcLogTags::Opc, "DateTime nanos {} is outside ±1e9: {}", ns, serialize(v) };
			return Protobuf::ToTimePoint( s, ns );
		};
		if( v.is_object() ){
			let& o = v.get_object();
			let* secondsPtr = o.if_contains( "seconds" );
			//The seconds are a plain number, as ToJson emits them, or the protobufjs Long {high,low} a client's int64 arrives
			//as - both through `at`'s guard:  the Long form once skipped it, so {high:2147483647,low:-1}, what protobufjs
			//emits for INT64_MAX, wrapped to 1969-12-31T23:59:59Z and was written silently.
			if( secondsPtr && (secondsPtr->is_number() || secondsPtr->is_object()) ){
				let seconds = Json::AsNumber<int64_t>( secondsPtr->is_object() ? FromLong(secondsPtr->get_object(), sl) : *secondsPtr, sl );
				let* nanos = o.if_contains( "nanos" );
				_time = at( seconds, nanos && nanos->is_number() ? Json::AsNumber<int64_t>(*nanos, sl) : 0 );
			}
			else
				throw Exception{ sl, {(ELogTags)EOpcLogTags::Opc}, "Invalid DateTime object: {}", serialize(v) };
		}
		else if( v.is_string() )
			_time = Chrono::ToTimePoint( string{v.get_string()} );
		else if( v.is_number() ){
			let ms = Json::AsNumber<_int>( v, sl );//a bare number is milliseconds since the epoch.  AsNumber, per the note above.
			_time = at( ms/1000, (ms%1000)*1'000'000 );
		}
		else
			throw Exception{ sl, {(ELogTags)EOpcLogTags::Opc}, "Invalid DateTime object: {}", serialize(v) };
	}
	UADateTime::UADateTime( const google::protobuf::Timestamp& timestamp )ι:
		_time{ Protobuf::ToTimePoint(timestamp) }
	{}

	α UADateTime::ToJson()Ι->jobject{
		let t = ToProto();
		return jobject{ {"seconds", t.seconds()}, {"nanos", t.nanos()} };
	}
	α UADateTime::ToProto()Ι->google::protobuf::Timestamp{
		return Protobuf::ToTimestamp( _time );
	}
	α UADateTime::UA()Ι->UA_DateTime{
		return fromTimePoint( _time );
	}
}