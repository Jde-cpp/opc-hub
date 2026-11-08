#include <jde/calendar/Schedule.h>
#include <jde/fwk/str.h>
#include <jde/calendar/validation.h>
#include "refuse.h"

#define let const auto

namespace Jde{
	constexpr ELogTags _tags{ ELogTags::App };

	namespace Calendar{
		Ω number( const jobject& row, sv key, SL sl )ε->uint32{ return Json::AsNumber<uint32>( row, key, sl ); }
		Ω findTime( const jobject& row, sv key, SL sl )ε->optional<TimePoint>{
			let p = row.if_contains( key );
			return p && !p->is_null() ? optional<TimePoint>{ Chrono::ToTimePoint(Json::AsString(*p, sl), sl) } : nullopt;
		}
		Ω time( const jobject& row, sv key, SL sl )ε->TimePoint{ return Chrono::ToTimePoint( Json::AsString(row, key, sl), sl ); }
		Ω sortShifts( vector<Shift>& shifts )ι->void{
			std::ranges::sort( shifts, []( const Shift& a, const Shift& b ){ return std::tie(a.Start, a.Id)<std::tie(b.Start, b.Id); } );
		}
		//What is left of `from` once `cut` is taken out:  nothing, itself, one end or both.
		Ω subtract( Span from, Span cut )ι->vector<Span>{
			vector<Span> y;
			if( cut.End<=from.Start || cut.Start>=from.End )
				y.push_back( from );
			else{
				if( cut.Start>from.Start )
					y.push_back( {from.Start, cut.Start} );
				if( cut.End<from.End )
					y.push_back( {cut.End, from.End} );
			}
			return y;
		}
		Ω subtract( const vector<Span>& from, Span cut )ι->vector<Span>{
			vector<Span> y;
			for( let& span : from ){
				let left = subtract( span, cut );
				y.insert( y.end(), left.begin(), left.end() );
			}
			return y;
		}
		//The occurrence over one of its pieces, its breaks clipped to it.
		Ω piece( const Occurrence& o, Span span )ι->Occurrence{
			Occurrence y{ o.Shift, o.Name, o.Day, span.Start, span.End };
			for( let& b : o.Breaks ){
				let start = std::max( b.Start, span.Start ), end = std::min( b.End, span.End );
				if( end>start )
					y.Breaks.push_back( {start, end} );
			}
			return y;
		}
	}

	α Calendar::ParseBreaks( const jvalue& breaks, SL sl )ε->vector<Break>{
		vector<Break> y;
		if( breaks.is_null() || (breaks.is_string() && breaks.get_string().empty()) )
			return y;
		try{
			jvalue parsed;
			const jvalue* p = &breaks;
			if( breaks.is_string() ){
				parsed = Json::ParseValue( breaks.get_string(), sl );
				p = &parsed;
			}
			for( let& b : Json::AsArray(*p, sl) ){
				let& o = Json::AsObject( b, sl );
				y.push_back( {Json::AsNumber<uint16>(o, "start", sl), Json::AsNumber<uint16>(o, "minutes", sl)} );
			}
		}
		catch( const Jde::Exception& e ){
			REFUSE( "breaks must be a json list of {{start, minutes}}, in minutes from the shift's start, not {}:  {}", serialize(breaks), e.what() );
		}
		return y;
	}

	α Calendar::ParseBucketBy( sv by, SL sl )ε->BucketBy{
		let lower = Str::ToLower( by );
		if( lower=="shift" ) return EBucket::Shift;
		if( lower=="day" ) return EBucket::Day;
		if( lower=="week" ) return EBucket::Week;
		if( lower=="month" ) return EBucket::Month;
		let d = Chrono::TryToDuration( by, ELogLevel::Debug, sl );
		REFUSE_IF( !d, "by '{}' is not shift, day, week, month or an ISO 8601 duration.", by );
		REFUSE_IF( *d<minutes{1}, "by '{}' is under a minute, the smallest bucket.", by );
		return *d;
	}

	α Calendar::ToJson( const Occurrence& o )ι->jobject{
		jarray breaks;
		for( let& b : o.Breaks )
			breaks.push_back( jobject{ {"start", ToIsoString<seconds>(b.Start)}, {"end", ToIsoString<seconds>(b.End)} } );
		return jobject{
			{"shift", o.Shift ? jvalue{*o.Shift} : jvalue{}},
			{"name", o.Name},
			{"day", std::format("{:%F}", o.Day)},
			{"start", ToIsoString<seconds>(o.Start)},
			{"end", ToIsoString<seconds>(o.End)},
			{"breaks", move(breaks)}
		};
	}
	Calendar::Schedule::Schedule( const jobject& row, const jarray& patterns, const jarray& shifts, const jarray& exceptions, SL sl )ε:
		_id{ number(row, "id", sl) },
		_name{ Json::FindDefaultSV(row, "name") },
		_slug{ Json::FindDefaultSV(row, "slug") },
		_zone{ &ValidateZone(Json::AsSV(row, "timeZone", sl), sl) },
		_dayStart{ Json::FindNumber<uint16>(row, "dayStart").value_or(0) }{
		for( let& pv : patterns ){
			let& p = Json::AsObject( pv, sl );
			if( number(p, "calendarId", sl)!=_id )
				continue;
			Pattern pattern{ number(p, "id", sl), _id, findTime(p, "effective", sl), Json::FindNumber<uint8>(p, "cycleDays").value_or(7), findTime(p, "cycleStart", sl) };
			for( let& shift : shifts ){
				let& s = Json::AsObject( shift, sl );
				if( number(s, "calendarPatternId", sl)!=pattern.Id )
					continue;
				let breaks = s.if_contains( "breaks" );
				pattern.Shifts.push_back( Shift{ number(s, "id", sl), pattern.Id, Json::AsString(s, "name", sl), number(s, "days", sl),
					Json::AsNumber<uint16>(s, "start", sl), Json::AsNumber<uint16>(s, "minutes", sl), breaks ? ParseBreaks(*breaks, sl) : vector<Break>{} } );
			}
			_patterns.push_back( move(pattern) );
		}
		for( let& ev : exceptions ){
			let& e = Json::AsObject( ev, sl );
			if( number(e, "calendarId", sl)==_id )
				_exceptions.push_back( Override{ number(e, "id", sl), _id, Json::AsString(e, "name", sl), time(e, "start", sl), time(e, "finish", sl), Json::FindBool(e, "isOpen").value_or(false)} );//finish and isOpen, not end and open, which SQL Server reserves (app-meta.jsonnet).
		}
		Finish();
	}

	Calendar::Schedule::Schedule( uint32 id, string name, string slug, const time_zone& zone, uint16 dayStart, vector<Pattern> patterns, vector<Override> exceptions )ι:
		_id{ id },
		_name{ move(name) },
		_slug{ move(slug) },
		_zone{ &zone },
		_dayStart{ dayStart },
		_patterns{ move(patterns) },
		_exceptions{ move(exceptions) }{
		Finish();
	}

	α Calendar::Schedule::Finish()ι->void{
		for( auto& p : _patterns ){
			if( p.Effective )
				p.Effective = Normalize( *p.Effective );
			if( p.CycleStart ){
				p.CycleStart = Normalize( *p.CycleStart );
				p.Anchor = local_days{ DayOf(*p.CycleStart) };
			}
			for( auto& s : p.Shifts )
				std::ranges::sort( s.Breaks, {}, &Break::Start );
			sortShifts( p.Shifts );
		}
		for( auto& e : _exceptions ){//whole seconds, which is all calendarOccurrences can say.
			e.Start = floor<seconds>( e.Start );
			e.End = floor<seconds>( e.End );
		}
		std::ranges::sort( _patterns, []( const Pattern& a, const Pattern& b ){ return std::tie(a.Effective, a.Id)<std::tie(b.Effective, b.Id); } );//an empty optional sorts first:  the first pattern.
		std::ranges::sort( _exceptions, []( const Override& a, const Override& b ){ return std::tie(a.Start, a.Id)<std::tie(b.Start, b.Id); } );
	}

	α Calendar::Schedule::Local( year_month_day day, minutes afterDayStart )Ι->TimePoint{
		return _zone->to_sys( local_days{day}+_dayStart+afterDayStart, choose::earliest );
	}
	α Calendar::Schedule::DayStart( year_month_day day )Ι->TimePoint{
		return Local( day, minutes::zero() );
	}
	α Calendar::Schedule::DayOf( TimePoint t )Ι->year_month_day{
		let day = Chrono::LocalYMD( t, *_zone );
		return DayStart( day )<=t ? day : year_month_day{ local_days{day}-days{1} };
	}
	α Calendar::Schedule::CycleDay( year_month_day day, const Pattern& pattern )Ι->uint8{
		let cycle = std::max<int64_t>( pattern.CycleDays, 1 );
		let n = (local_days{day}-pattern.Anchor).count();
		return (uint8)( ((n%cycle)+cycle)%cycle );
	}
	α Calendar::Schedule::InEffect( year_month_day day )Ι->const Pattern*{
		let start = DayStart( day );
		const Pattern* y{};
		for( let& p : _patterns ){//by Effective then Id, so the first of a tie is the lowest id and a later one only replaces it with a later date.
			if( p.Effective && *p.Effective>start )
				break;
			if( !y || p.Effective!=y->Effective )
				y = &p;
		}
		return y;
	}
	α Calendar::Schedule::ToTimePoint( string iso, SL sl )Ε->TimePoint{
		return Chrono::ToTimePoint( move(iso), *_zone, sl );
	}

	α Calendar::Schedule::Occurrences( TimePoint start, TimePoint end )Ι->vector<Occurrence>{
		vector<Occurrence> y;
		if( end<=start )
			return y;
		auto first = local_days{ DayOf(start) }-days{1};
		auto last = local_days{ DayOf(end-Duration{1}) };
		vector<const Override*> closed, open;
		for( let& e : _exceptions ){
			if( e.End<=e.Start )
				continue;
			if( !e.Open )
				closed.push_back( &e );
			else if( e.End>start && e.Start<end ){//the shifts under an open exception decide what it adds, so its days are walked too.
				open.push_back( &e );
				first = std::min( first, local_days{DayOf(e.Start)}-days{1} );
				last = std::max( last, local_days{DayOf(e.End-Duration{1})} );
			}
		}
		let walked = Span{ DayStart(year_month_day{first}), DayStart(year_month_day{last+days{2}}) };//the last day's latest shift ends before the day after next.
		std::erase_if( closed, [&]( const Override* e ){ return e->End<=walked.Start || e->Start>=walked.End; } );
		for( auto d=first; d<=last; d+=days{1} ){
			let day = year_month_day{ d };
			let pattern = InEffect( day );
			if( !pattern )
				continue;
			let cycleDay = CycleDay( day, *pattern );
			for( let& s : pattern->Shifts ){
				if( !(s.Days & (1u<<cycleDay)) )
					continue;
				Occurrence o{ s.Id, s.Name, day, Local(day, minutes{s.Start}), Local(day, minutes{s.Start+s.Minutes}) };
				if( o.End<=o.Start )//both ends in the hour the clocks skipped.
					continue;
				for( let& b : s.Breaks ){
					Span span{ Local(day, minutes{s.Start+b.Start}), Local(day, minutes{s.Start+b.Start+b.Minutes}) };
					if( span.End>span.Start )
						o.Breaks.push_back( span );
				}
				y.push_back( move(o) );
			}
		}
		for( let e : closed ){
			auto touches = [e]( const Occurrence& o ){ return o.Start<e->End && e->Start<o.End; };
			if( std::ranges::none_of(y, touches) )
				continue;
			vector<Occurrence> cut;
			for( auto& o : y ){
				if( !touches(o) ){
					cut.push_back( move(o) );
					continue;
				}
				for( let& span : subtract(Span{o.Start, o.End}, Span{e->Start, e->End}) )
					cut.push_back( piece(o, span) );
			}
			y = move( cut );
		}
		for( let e : open ){
			vector<Span> left{ Span{e->Start, e->End} };
			for( let c : closed )
				left = subtract( left, Span{c->Start, c->End} );
			for( let& o : y )
				left = subtract( left, Span{o.Start, o.End} );
			for( let& span : left )
				y.push_back( Occurrence{nullopt, e->Name, DayOf(span.Start), span.Start, span.End} );
		}
		std::erase_if( y, [&]( const Occurrence& o ){ return o.End<=start || o.Start>=end; } );
		std::ranges::stable_sort( y, []( const Occurrence& a, const Occurrence& b ){ return a.Start<b.Start; } );
		return y;
	}

	α Calendar::Schedule::Planned( TimePoint start, TimePoint end )Ι->PlannedTime{
		vector<Span> spans;
		for( let& o : Occurrences(start, end) ){
			auto cursor = o.Start;
			for( let& b : o.Breaks ){
				if( b.Start>cursor )
					spans.push_back( {cursor, b.Start} );
				cursor = std::max( cursor, b.End );
			}
			if( o.End>cursor )
				spans.push_back( {cursor, o.End} );
		}
		std::ranges::sort( spans, []( const Span& a, const Span& b ){ return a.Start<b.Start; } );
		PlannedTime y;
		for( let& span : spans ){
			let s = std::max( span.Start, start ), e = std::min( span.End, end );
			if( e<=s )
				continue;
			if( !y.Spans.empty() && s<=y.Spans.back().End )
				y.Spans.back().End = std::max( y.Spans.back().End, e );
			else
				y.Spans.push_back( {s, e} );
		}
		for( let& span : y.Spans )
			y.Total += span.End-span.Start;
		return y;
	}

	α Calendar::Schedule::At( TimePoint t )Ι->optional<Occurrence>{
		auto occurrences = Occurrences( t, t+Duration{1} );//overlapping [t, t+1) means Start<=t<End.
		return occurrences.empty() ? nullopt : optional<Occurrence>{ move(occurrences.front()) };
	}

	α Calendar::Schedule::Next( TimePoint t )Ι->optional<TimePoint>{
		optional<TimePoint> y;
		auto consider = [&]( TimePoint boundary ){
			if( boundary>t && (!y || boundary<*y) )
				y = boundary;
		};
		//Nothing overlaps, so when an occurrence holds t its own edges are the candidates, and otherwise the first to start
		//inside the window is - a window with any occurrence in it has the answer.
		for( auto window=days{1}; window<=days{512} && !y; window*=2 ){
			for( let& o : Occurrences(t, t+window) ){
				consider( o.Start );
				consider( o.End );
				for( let& b : o.Breaks ){
					consider( b.Start );
					consider( b.End );
				}
			}
		}
		return y;
	}

	α Calendar::Schedule::Buckets( TimePoint start, TimePoint end, BucketBy by, SL sl )Ε->vector<Bucket>{
		vector<Bucket> y;
		let d = std::get_if<Duration>( &by );
		REFUSE_IF( d && *d<minutes{1}, "by {} is under a minute, the smallest bucket.", Chrono::ToString(*d) );
		if( end<=start )
			return y;
		auto clip = [&]( TimePoint s, TimePoint e, string label ){
			s = std::max( s, start ); e = std::min( e, end );
			if( e>s )
				y.push_back( Bucket{s, e, move(label)} );
		};
		if( d ){
			for( auto s=start; ; s+=*d ){
				let last = end-s<=*d;//not s+*d>=end, which a long `by` overflows.
				clip( s, last ? end : s+*d, ToIsoString<seconds>(s) );
				if( last )
					break;
			}
			return y;
		}
		let firstDay = local_days{ DayOf(start) };
		switch( std::get<EBucket>(by) ){
		case EBucket::Shift:
			for( let& o : Occurrences(start, end) )
				clip( o.Start, o.End, o.Name );
			break;
		case EBucket::Day:
			for( auto d=firstDay; DayStart(year_month_day{d})<end; d+=days{1} )
				clip( DayStart(year_month_day{d}), DayStart(year_month_day{d+days{1}}), std::format("{:%F}", year_month_day{d}) );
			break;
		case EBucket::Week:
			for( auto w=firstDay-days{weekday{firstDay}.iso_encoding()-1}; DayStart(year_month_day{w})<end; w+=days{7} )
				clip( DayStart(year_month_day{w}), DayStart(year_month_day{w+days{7}}), std::format("{:%G-W%V}", w) );
			break;
		case EBucket::Month:
			for( auto m=year_month{year_month_day{firstDay}.year(), year_month_day{firstDay}.month()}; DayStart(m/1)<end; m+=months{1} )
				clip( DayStart(m/1), DayStart((m+months{1})/1), std::format("{:%Y-%m}", m) );
			break;
		}
		return y;
	}
	α Calendar::ReanchorDay( TimePoint dayStart, const Schedule& from, const Schedule& to )ι->TimePoint{
		return to.DayStart( from.DayOf(dayStart) );
	}
	α Calendar::ReanchorLocal( TimePoint t, const Schedule& from, const Schedule& to )ι->TimePoint{
		if( from.Zone()==to.Zone() )//the round trip would move an instant in the second pass of a repeated hour to its first.
			return t;
		return to.Zone().to_sys( from.Zone().to_local(t), choose::earliest );
	}
}