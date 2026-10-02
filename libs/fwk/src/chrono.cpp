#include <jde/fwk/chrono.h>
#include <absl/strings/numbers.h>
#include <absl/time/time.h>
#include <jde/fwk/utils/mathUtils.h>

#define let const auto
namespace Jde{
	α Chrono::LocalTimeMilli( TimePoint time, SL sl )ε->string{
		try{
			auto tp = std::chrono::current_zone()->to_local( time );
			return std::format( "{0:%H:%M:}{1:%S}", tp, time.time_since_epoch() );
		}
		catch( const std::format_error& e ){
			THROWSL( "Failed to convert time point to local time: {} - {}", ToIsoString(time), e.what() );
		}
	}
	α Chrono::ToDuration( string&& iso, SL sl )ε->Duration{
		std::istringstream is{ move(iso) };
		if( let ch = is.get(); ch!='P' ){
			string content{ (char)ch }; string line;
			while( std::getline(is,line) )
				content += line;
			throw Exception{ sl, ELogLevel::Debug, "Expected 'P' as first character in '{}{}'.", ch, content };
		}
		bool parsingTime = false;
		Duration duration{ Duration::zero() };
		while( true ){
			let c = is.peek();
			if( c==std::char_traits<char>::eof() )
				break;
			if( !parsingTime && c=='T' ){
				parsingTime = true;
				is.get();
				continue;
			}
			//scan the number digit-by-digit: istream>>double treats a trailing 'D' (a hex digit) as part of the
			//number candidate and then fails, silently swallowing the days token - so read only decimal chars.
			string num;
			if( c=='+' || c=='-' )
				num += (char)is.get();
			for( auto d=is.peek(); (d>='0'&&d<='9')||d=='.'; d=is.peek() )
				num += (char)is.get();
			let type = is.get();
			if( num.empty() || type==std::char_traits<char>::eof() )
				break;
			double value;
			if( !absl::SimpleAtod(num, &value) )
				throw Exception{ sl, ExceptionArgs{}, "Could not parse ISO duration token:  {}{}", num, type };
			//date units use the exact std::chrono period ratios so ToDuration is the inverse of ToString
			//(which emits years/months/days) - a month is ~730.5h, not 720h; a year 365.2425d, not 365.25d.
			if( type=='Y' )
				duration += duration_cast<Duration>( std::chrono::duration<double,years::period>{value} );
			else if( !parsingTime && type=='M' )
				duration += duration_cast<Duration>( std::chrono::duration<double,months::period>{value} );
			else if( type=='D' )
				duration += duration_cast<Duration>( std::chrono::duration<double,days::period>{value} );
			else if( type=='H' )
				duration += minutes( Round(value*60) );
			else if( type=='M' )
				duration += seconds( Round(value*60) );
			else if( type=='S' )
				duration += milliseconds( Round(value*1000) );
		}
		return duration;
	}
	α Chrono::TryToDuration( string&& iso, ELogLevel level, SL sl )ε->optional<Duration>{
		try{
			return ToDuration( move(iso), sl );
		}
		catch( Exception& e ){
			e.SetLevel( level );
		}
		return {};
	}

	α Chrono::ToTimePoint( string iso, SL sl )ε->TimePoint{
		//%E*S takes any number of fraction digits, %Ez a 'Z' or a ±hh[[:]mm] offset; no zone at all reads as UTC.
		absl::Time t; string error;
		let parsed = absl::ParseTime( "%Y-%m-%d%ET%H:%M:%E*S%Ez", iso, absl::UTCTimeZone(), &t, &error )
			|| absl::ParseTime( "%Y-%m-%d%ET%H:%M:%E*S", iso, absl::UTCTimeZone(), &t, &error );
		THROW_IFSL( !parsed, "Could not parse ISO time:  {} - {}", iso, error );
		return absl::ToChronoTime( t );
	}

	α Chrono::ToTimePoint( uint16_t y, uint8_t mnth, uint8_t dayOfMonth, uint8 h, uint8 mnt, uint8 scnd, Duration subseconds, SL sl )ε->TimePoint{
    auto ymd = year{y}/month{mnth}/day{dayOfMonth};
		THROW_IFSL( !ymd.ok(), "Invalid date: {}-{}-{}", y, mnth, dayOfMonth );
    auto tp = sys_days{ymd} + hours{h} + minutes{mnt} + seconds{scnd}+subseconds;
		return tp;
	}
	α Chrono::ToTimeZone( sv name, const std::chrono::time_zone& dflt, ELogLevel level, SL sl )ι->const std::chrono::time_zone&{
		try{
			return *std::chrono::locate_zone( name );
		}
		catch( const std::runtime_error& e ){
			LOGSL( level, sl, ELogTags::Parsing, "Time zone: '{}' not found: {}. Using default '{}'.", name, e.what(), dflt.name() );
		}
		return dflt;
	}
}