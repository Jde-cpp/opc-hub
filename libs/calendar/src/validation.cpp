#include <jde/calendar/validation.h>
#include <bit>
#include <jde/fwk/exceptions/refuse.h>

#define let const auto

namespace Jde{
	constexpr ELogTags _tags{ ELogTags::App };

	namespace Calendar{
		//A cycle day as the grid names it:  the weekday on the default cycle, `day n` otherwise.
		Ω dayName( const Pattern& p, uint8 cycleDay )ι->string{
			return p.CycleStart || p.CycleDays!=7 ? Ƒ("day {}", cycleDay) : std::format( "{:%A}", weekday{CycleMonday+days{cycleDay}} );
		}
	}

	α Calendar::ValidateZone( sv timeZone, SL sl )ε->const time_zone&{
		try{
			return *locate_zone( timeZone );
		}
		catch( const std::runtime_error& e ){
			REFUSE( "timeZone '{}' is not one the server knows:  {}", timeZone, e.what() );
		}
	}

	α Calendar::ValidateCalendar( sv timeZone, uint dayStart, SL sl )ε->const time_zone&{
		let& zone = ValidateZone( timeZone, sl );
		REFUSE_IF( dayStart>=1440, "dayStart {} is not under 1440, the minutes in a day.", dayStart );
		return zone;
	}

	α Calendar::ValidateShift( const Shift& s, uint8 cycleDays, SL sl )ε->void{
		REFUSE_IF( s.Start>=1440, "Shift '{}' starts at minute {}, which is not under 1440.", s.Name, s.Start );
		REFUSE_IF( s.Minutes<1 || s.Minutes>1440, "Shift '{}' runs {} minutes, which is not 1 to 1440.", s.Name, s.Minutes );
		if( s.Days ){
			let highest = std::bit_width( s.Days )-1;
			REFUSE_IF( highest>=cycleDays, "Shift '{}' has days bit {} set, beyond a {}-day cycle, whose days are 0 to {}.", s.Name, highest, cycleDays, cycleDays-1 );
		}
		auto breaks = s.Breaks;
		std::ranges::sort( breaks, []( const Break& a, const Break& b ){ return a.Start<b.Start; } );
		for( uint i=0; i<breaks.size(); ++i ){
			let& b = breaks[i];
			REFUSE_IF( b.Minutes<1, "Shift '{}' has a break at minute {} with no length.", s.Name, b.Start );
			REFUSE_IF( (uint)b.Start+b.Minutes>s.Minutes, "Shift '{}' has a break at minute {} for {} minutes, which leaves the {}-minute shift.", s.Name, b.Start, b.Minutes, s.Minutes );
			REFUSE_IF( i && b.Start<breaks[i-1].Start+breaks[i-1].Minutes, "Shift '{}' has breaks at minutes {} and {} that overlap.", s.Name, breaks[i-1].Start, b.Start );
		}
	}

	α Calendar::ValidatePattern( const Pattern& p, SL sl )ε->void{
		REFUSE_IF( p.CycleDays<1 || p.CycleDays>31, "cycleDays {} is not 1 to 31.", p.CycleDays );
		REFUSE_IF( p.CycleDays!=7 && !p.CycleStart, "A {}-day cycle needs a cycleStart, the date of its day 0;  only a 7-day cycle has a default, Monday.", p.CycleDays );
		for( let& s : p.Shifts )
			ValidateShift( s, p.CycleDays, sl );
		//Every run of every shift over one cycle, in minutes from the cycle's start, then each pair against the other as it is
		//and shifted a cycle either way, since a run that leaves the cycle's end comes back in at its start.
		struct Run{ const Shift* S; uint8 Day; int64_t Start; int64_t End; };
		vector<Run> runs;
		for( let& s : p.Shifts ){
			for( uint8 d=0; d<p.CycleDays; ++d ){
				if( s.Days & (1u<<d) )
					runs.push_back( Run{&s, d, (int64_t)(d*1440+s.Start), (int64_t)(d*1440+s.Start+s.Minutes)} );
			}
		}
		let cycle = (int64_t)p.CycleDays*1440;
		for( uint i=0; i<runs.size(); ++i ){
			for( uint j=i+1; j<runs.size(); ++j ){
				let& a = runs[i]; let& b = runs[j];
				for( let shift : {int64_t{0}, cycle, -cycle} ){
					REFUSE_IF( a.Start<b.End+shift && b.Start+shift<a.End, "Shifts '{}' on {} and '{}' on {} overlap.", a.S->Name, dayName(p, a.Day), b.S->Name, dayName(p, b.Day) );
				}
			}
		}
	}

	α Calendar::ValidateSeams( const Schedule& c, SL sl )ε->void{
		for( let& p : c.Patterns() ){
			if( !p.Effective )
				continue;
			let first = c.DayOf( *p.Effective );
			let last = year_month_day{ local_days{first}-days{1} };
			let previous = c.InEffect( last );
			if( !previous || c.InEffect(first)!=&p )//a tie's loser is never in effect.
				continue;
			let lastCycleDay = c.CycleDay( last, *previous ), firstCycleDay = c.CycleDay( first, p );
			for( let& before : previous->Shifts ){
				let into = (int64_t)before.Start+(int64_t)before.Minutes-1440;//uint16 is wider than int, so unsigned without the casts.
				if( into<=0 || !(before.Days & (1u<<lastCycleDay)) )
					continue;
				for( let& after : p.Shifts ){
					REFUSE_IF( (after.Days & (1u<<firstCycleDay)) && (int64_t)after.Start<into, "Shift '{}' runs {} minutes into {}, the first day of a new pattern, past the start of its shift '{}'.", before.Name, into, std::format("{:%F}", first), after.Name );
				}
			}
		}
	}

	α Calendar::ValidateException( const Override& e, SL sl )ε->void{
		REFUSE_IF( e.End<=e.Start, "Exception '{}' ends at {}, which is not after its start, {}.", e.Name, ToIsoString<seconds>(e.End), ToIsoString<seconds>(e.Start) );
	}
}