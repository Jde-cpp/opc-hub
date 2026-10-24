#include <jde/fwk/chrono.h>
#include <absl/strings/numbers.h>
#include <absl/time/time.h>

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
	//ISO 8601:  P, the date designators in order, then T and the time ones, each at most once and a T followed by one;  a sign
	//on a component, as ToString writes a negative.  Every component converts exactly, a month the ~730.5h ToString writes,
	//and one past Duration's range fails rather than wrapping.
	α Chrono::ToDuration( sv iso, SL sl )ε->Duration{
		auto y = TryToDuration( iso, ELogLevel::Debug, sl );
		if( !y )
			y.error().Throw();
		return *y;
	}
	α Chrono::TryToDuration( sv iso, ELogLevel level, SL sl )ι->std::expected<Duration,Exception>{
		if( !iso.starts_with('P') )
			return std::unexpected{ Exception{sl, level, "Expected 'P' as first character in '{}'.", iso} };
		struct Unit final{ char Designator; bool Time; int64_t Seconds; };
		constexpr std::array<Unit,7> units{ Unit{'Y', false, years::period::num}, Unit{'M', false, months::period::num}, Unit{'W', false, weeks::period::num},
			Unit{'D', false, days::period::num}, Unit{'H', true, hours::period::num}, Unit{'M', true, minutes::period::num}, Unit{'S', true, seconds::period::num} };
		constexpr double maxSeconds{ std::chrono::duration<double>(Duration::max()).count() };
		Duration y{ Duration::zero() };
		bool time{}, any{};
		uint next{};//the first of units a component may still name.
		for( uint i=1; i<iso.size(); ++i ){
			if( iso[i]=='T' ){
				if( time )
					return std::unexpected{ Exception{sl, level, "'{}' - a second 'T' at {}.", iso, i} };
				if( i+1==iso.size() )
					return std::unexpected{ Exception{sl, level, "'{}' - a 'T' with no time after it.", iso} };
				time = true;
				continue;
			}
			let start = i;
			if( iso[i]=='+' || iso[i]=='-' )
				++i;
			while( i<iso.size() && ((iso[i]>='0' && iso[i]<='9') || iso[i]=='.') )
				++i;
			let number = iso.substr( start, i-start );
			if( i==iso.size() )
				return std::unexpected{ Exception{sl, level, "'{}' - '{}' has no designator.", iso, number} };
			let designator = iso[i];
			double value;
			if( !absl::SimpleAtod(number, &value) )
				return std::unexpected{ Exception{sl, level, "'{}' - no number before '{}' at {}.", iso, designator, i} };
			let unit = std::ranges::find_if( units, [=]( const Unit& u ){ return u.Designator==designator && u.Time==time; } );
			if( unit==units.end() )
				return std::unexpected{ Exception{sl, level, "'{}' - '{}' at {} is no {} designator.", iso, designator, i, time ? "time" : "date"} };
			let index = (uint)( unit-units.begin() );
			if( index<next )
				return std::unexpected{ Exception{sl, level, "'{}' - '{}' at {} is repeated or out of order:  Y M W D, then T and H M S.", iso, designator, i} };
			next = index+1;
			let seconds = value*(double)unit->Seconds;
			if( std::abs(seconds)>=maxSeconds )
				return std::unexpected{ Exception{sl, level, "'{}' - '{}{}' is longer than a duration holds.", iso, number, designator} };
			let part = std::chrono::round<Duration>( std::chrono::duration<double>{seconds} );
			if( (part>Duration::zero() && y>Duration::max()-part) || (part<Duration::zero() && y<Duration::min()-part) )
				return std::unexpected{ Exception{sl, level, "'{}' is longer than a duration holds.", iso} };
			y += part;
			any = true;
		}
		if( !any )
			return std::unexpected{ Exception{sl, level, "'{}' has no component - zero is PT0S.", iso} };
		return y;
	}

	α Chrono::ToTimePoint( string iso, SL sl )ε->TimePoint{
		//%E*S takes any number of fraction digits, %Ez a 'Z' or a ±hh[[:]mm] offset; no zone at all reads as UTC.  Past the 'T' only a
		//zone holds Z, + or -, so the format is picked up front rather than by a failed parse.
		let time = iso.find_first_of( "Tt" );
		let zoned = time!=string::npos && iso.find_first_of( "Zz+-", time )!=string::npos;
		absl::Time t; string error;
		let parsed = absl::ParseTime( zoned ? "%Y-%m-%d%ET%H:%M:%E*S%Ez" : "%Y-%m-%d%ET%H:%M:%E*S", iso, absl::UTCTimeZone(), &t, &error );
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