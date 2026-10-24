#pragma once
#ifndef CHRONO_H
#define CHRONO_H
#include <chrono>
#include <expected>

#define Φ Γ auto
namespace Jde{
	using namespace std::chrono;

	//Precision is the chrono duration to truncate to - ToIsoString<seconds>(tp) - and may be days, hours, minutes, or
	//any seconds-or-finer duration.  void, the default, renders the time_point's own precision, untouched.
	template<class Precision=void, class T=TimePoint>
	α ToIsoString( T timePoint )ι->string;
}
namespace Jde::Chrono{
	using namespace std::chrono;
	Ξ Epoch()ι->TimePoint{ return Clock::from_time_t(0); }
	Φ LocalTimeMilli( TimePoint time, SRCE )ε->string;
	Ξ LocalYMD( TimePoint time, const time_zone& tz )ι->year_month_day{ return year_month_day{floor<days>(tz.to_local(time))}; };
	Φ ToDuration( sv iso, SRCE )ε->Duration;//ISO 8601, as ToString writes it:  anything else throws.
	Φ TryToDuration( sv iso, ELogLevel level=ELogLevel::Debug, SRCE )ι->std::expected<Duration,Exception>;
	Φ ToTimePoint( string iso, SRCE )ε->TimePoint;
	Φ ToTimePoint( uint16_t year, uint8 month, uint8 day, uint8 hour=0, uint8 minute=0, uint8 second=0, Duration subseconds=Duration::zero(), SRCE )ε->TimePoint;
	Φ ToTimeZone( sv name, const std::chrono::time_zone& dflt, ELogLevel level=ELogLevel::Debug, SRCE )ι->const std::chrono::time_zone&;
	template<class T=Duration>
	α ToString( T duration )ι->string;
	template<class To,class From>
	α ToClock( typename From::time_point from )ι->typename To::time_point{ return To::now()-milliseconds{duration_cast<milliseconds>(From::time_point::clock::now()-from)}; }
}

//the single definition, so steady_clock is normalized in one place - no explicit specialization that would have to
//precede every instantiation.
template<class Precision, class T>
auto Jde::ToIsoString( T timePoint )ι->string{
	using namespace std::chrono;
	if constexpr( std::same_as<T,steady_clock::time_point> )
		return ToIsoString<Precision>( Chrono::ToClock<Clock, steady_clock>(timePoint) );
	//format the *truncated type*, not a truncated value: %T's subsecond digit count comes from the time_point's
	//period, so assigning back to T would print the dropped digits as zeros (…T00:00:00.123000000Z).  Each step
	//drops one field rather than zero-filling it, so the string narrows with the precision.
	//floor, not round: these render expirations and 'the day/hour it falls in' never advances to the next one.
	else if constexpr( std::same_as<Precision,void> )
		return std::format( "{:%FT%T}Z", timePoint );//no truncation - whatever precision T already carries.
	else if constexpr( std::same_as<Precision,days> )
		return std::format( "{:%F}", floor<days>(timePoint) );//a bare date has no zone to designate - no 'Z'.
	else if constexpr( std::same_as<Precision,hours> )
		return std::format( "{:%FT%H}Z", floor<hours>(timePoint) );
	else if constexpr( std::same_as<Precision,minutes> )
		return std::format( "{:%FT%R}Z", floor<minutes>(timePoint) );
	else{
		static_assert( Precision::period::num==1,
			"ToIsoString<Precision> takes days, hours, minutes, or a seconds-or-finer duration - a 12-hour or 5-minute period has no ISO-8601 rendering." );
		return std::format( "{:%FT%T}Z", floor<Precision>(timePoint) );
	}
}
namespace Jde{
	Ŧ Chrono::ToString( T d )ι->string{
		//seconds-or-finer only: for coarser T (minutes, hours…) `d %= years{1}` in the output() macro is ill-formed,
		//since years/months aren't an exact multiple of T's period.  Every caller passes Duration/steady_clock ticks.
		static_assert( T::period::num==1, "Chrono::ToString requires a seconds-or-finer duration (T::period::num==1)." );
		if( d==T::zero() )
			return "PT0S";//ISO's zero:  a bare P names no component, and ToDuration refuses it.
		std::ostringstream os;
		os << 'P';
		//date units use the exact std::chrono period ratios so ToDuration is the inverse - a month is ~730.5h, not 720h.
		#define output( period,suffix ) if( d>=period{1} || d<=period{-1} ){ os << duration_cast<period>(d).count() << suffix; d%=period{1}; }
		output( years, "Y" )
		output( months, "M" )
		output( days, "D" )
		if( d!=Duration::zero() ){
			os << "T";
			output( hours, "H" );
			output( minutes, "M" );
			if( d!=Duration::zero() ){
				auto ms = duration_cast<milliseconds>( d ).count(); //remainder <1min: emit as fractional seconds - ToDuration reads 'S' decimals, but drops a unitless tail.
				if( ms<0 ){ os << '-'; ms = -ms; }
				os << ms/1000;
				if( auto frac = ms%1000; frac ){
					os << '.';
					if( frac<100 ) os << '0';
					if( frac<10 ) os << '0';
					while( frac%10==0 ) frac/=10;
					os << frac;
				}
				os << 'S';
			}
		}
		return os.str();
	}
}
#undef output
#undef Φ
#endif