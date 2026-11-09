#include <jde/oee/Figures.h>
#include <jde/fwk/exceptions/refuse.h>

#define let const auto

namespace Jde::Opc{
	constexpr ELogTags _tags{ ELogTags::App };

	namespace Oee{
		using Calendar::Schedule;

		//A machine with no calendar:  every minute planned, the days from midnight UTC.
		Ω utc()ε->const Schedule&{
			static const Schedule y{ 0, {}, {}, *std::chrono::locate_zone("UTC"), 0 };
			return y;
		}
		Ω length( Span span )ι->Duration{ return span.End>span.Start ? span.End-span.Start : Duration::zero(); }
		Ω clip( Span a, Span b )ι->Span{ return { std::max(a.Start, b.Start), std::min(a.End, b.End) }; }
		//The parts of x inside spans, which are in order and apart.
		Ω inside( const vector<Span>& spans, Span x )ι->vector<Span>{
			vector<Span> y;
			for( auto p = std::ranges::upper_bound(spans, x.Start, {}, &Span::End); p!=spans.end() && p->Start<x.End; ++p )
				y.push_back( clip(*p, x) );
			return y;
		}
		Ω overlap( const vector<Span>& spans, Span x )ι->Duration{
			Duration y{};
			for( auto p = std::ranges::upper_bound(spans, x.Start, {}, &Span::End); p!=spans.end() && p->Start<x.End; ++p )
				y += length( clip(*p, x) );
			return y;
		}
		Ω subtract( const vector<Span>& from, Span cut )ι->vector<Span>{
			vector<Span> y;
			for( let& span : from ){
				if( cut.End<=span.Start || cut.Start>=span.End ){
					y.push_back( span );
					continue;
				}
				if( cut.Start>span.Start )
					y.push_back( {span.Start, cut.Start} );
				if( cut.End<span.End )
					y.push_back( {cut.End, span.End} );
			}
			return y;
		}
		Ω share( Duration part, Duration whole )ι->double{ return (double)part.count()/(double)whole.count(); }
		Ω ratio( Duration n, Duration d )ι->optional<double>{ return d>Duration::zero() ? optional<double>{ share(n, d) } : nullopt; }
		Ω times( double count, Duration cycle )ι->Duration{ return Duration{ (Duration::rep)std::llround(count*(double)cycle.count()) }; }

		//What the rows cover:  the time the figures are over.
		Ω covered( const History& h )ι->Span{
			return h.Rows.empty() ? Span{} : Span{ h.Rows.front().Start, h.Rows.back().End() };
		}
		Ω planned( const Schedule* schedule, Span at )ι->vector<Span>{
			if( at.End<=at.Start )
				return {};
			return schedule ? schedule->Planned( at.Start, at.End ).Spans : vector<Span>{ at };
		}

		//A stop's time under one reason:  a coding's, else its code's, else none, which is uncoded.
		struct Piece final{ Span At; const Stop* Of; const Reason* Cause; const Coding* By; bool Small; };
		enum class EClass : uint8{ Excluded, Small, Planned, Unplanned };
		Ω classOf( const Piece& p )ι->EClass{
			if( p.Cause && p.Cause->Category==ECategory::Excluded )
				return EClass::Excluded;
			if( p.Small )//whatever its coding, bar Excluded.
				return EClass::Small;
			return p.Cause && p.Cause->Category==ECategory::Planned ? EClass::Planned : EClass::Unplanned;
		}
		//The stops' time inside `at`, in order, cut where the codings split it.  A stop is small by its whole length, an
		//open one's to `now`, where the rows end.
		Ω pieces( const Machine& machine, const History& h, Span at, TimePoint now )ι->vector<Piece>{
			vector<Piece> y;
			auto reason = [&h]( uint32 id )ι->const Reason*{
				let p = std::ranges::find( h.Reasons, id, &Reason::Id );
				return p==h.Reasons.end() ? nullptr : &*p;
			};
			for( let& stop : h.Stops ){
				if( stop.Unknown )
					continue;
				let end = stop.End.value_or( now );
				let in = clip( {stop.Start, end}, at );
				if( in.End<=in.Start )
					continue;
				let small = end-stop.Start<machine.SmallStop;
				const Reason* coded{};
				if( stop.Code ){
					if( let p = std::ranges::find(h.Reasons, stop.Code, &Reason::Code); p!=h.Reasons.end() )
						coded = &*p;
				}
				auto cursor = in.Start;
				for( let& coding : h.Codings ){
					if( coding.End<=cursor )
						continue;
					if( coding.Start>=in.End )
						break;
					if( coding.Start>cursor )
						y.push_back( {{cursor, coding.Start}, &stop, coded, nullptr, small} );
					let to = std::min( coding.End, in.End );
					y.push_back( {{std::max(cursor, coding.Start), to}, &stop, reason(coding.ReasonId), &coding, small} );
					cursor = to;
				}
				if( cursor<in.End )
					y.push_back( {{cursor, in.End}, &stop, coded, nullptr, small} );
			}
			return y;
		}

		//OEE is the product of the factors there are, and TEEP it times planned over the whole.
		Ω product( Bucket& y )ι->void{
			y.OEE = y.Availability;
			if( y.OEE && y.Performance )
				y.OEE = *y.OEE**y.Performance;
			if( y.OEE && y.Quality )
				y.OEE = *y.OEE**y.Quality;
			if( y.OEE && y.Whole>Duration::zero() )
				y.TEEP = *y.OEE*share( y.Planned, y.Whole );
		}
		Ω figure( const Machine& machine, const History& h, const Schedule* schedule, const vector<Piece>& stops, Span cover, Calendar::Bucket&& bounds )ι->Bucket{
			Bucket y{ bounds.Start, bounds.End, move(bounds.Label) };
			let at = clip( {bounds.Start, bounds.End}, cover );
			y.Partial = at.End<=at.Start || at.Start!=bounds.Start || at.End!=bounds.End;
			y.Whole = length( at );
			let shifts = planned( schedule, at );
			Duration calendar{};
			for( let& span : shifts )
				calendar += length( span );
			auto counted = shifts;//planned time less the Excluded stops:  what the counts are within.
			for( auto p = std::ranges::upper_bound(stops, at.Start, {}, []( const Piece& x ){ return x.At.End; }); p!=stops.end() && p->At.Start<at.End; ++p ){
				let kind = classOf( *p );
				for( let& part : inside(shifts, p->At) ){
					let d = length( part );
					switch( kind ){
					case EClass::Excluded: y.Excluded += d; counted = subtract( counted, part ); break;
					case EClass::Small: y.Small += d; break;
					case EClass::Planned: y.PlannedStop += d; break;
					case EClass::Unplanned: y.Unplanned += d; break;
					}
				}
			}
			double unknown{};
			for( auto row = std::ranges::upper_bound(h.Rows, at.Start, {}, &Interval::End); row!=h.Rows.end() && row->Start<at.End; ++row ){
				if( row->Width<=Duration::zero() )
					continue;
				//A row across an edge is split by time, its counts and its unknown time taken as even across it.
				const Span span{ row->Start, row->End() };
				let in = share( overlap(counted, span), row->Width );
				unknown += (double)row->Unknown.count()*share( overlap(shifts, span), row->Width );
				y.Total += row->Total*in;
				y.Good += row->Good*in;
				y.Reject += row->Reject*in;
				y.Outside += row->Total*( share(length(clip(span, at)), row->Width)-in );
			}
			//Here, not on a row:  a count and its reject can land in different rows, and it's the negative row that nets them.
			y.Good = std::max( std::min(y.Good, y.Total), 0.0 );
			y.Reject = std::max( std::min(y.Reject, y.Total), 0.0 );
			y.Unknown = Duration{ (Duration::rep)std::llround(unknown) };
			y.Partial = y.Partial || y.Unknown>Duration::zero();
			y.Planned = std::max( calendar-y.Excluded-y.Unknown, Duration::zero() );
			y.Run = std::max( y.Planned-y.Unplanned-y.PlannedStop, Duration::zero() );

			y.Availability = ratio( y.Run, y.Planned );
			if( y.Availability )
				y.Measured = EFactors::Availability;
			if( machine.Counts && machine.IdealCycle ){
				y.Ideal = times( y.Total, *machine.IdealCycle );
				y.PacedRun = y.Run;
				y.PerformanceCoverage = 1;
				y.Performance = ratio( y.Ideal, y.Run );//above one as it is:  it says the cycle is wrong.
				if( y.Availability && y.Performance )
					y.Measured = y.Measured|EFactors::Performance;
			}
			if( machine.Quality ){
				if( y.Total>0 )
					y.Quality = y.Good/y.Total;
				if( machine.IdealCycle ){
					y.GoodIdeal = times( y.Good, *machine.IdealCycle );
					y.GradedIdeal = times( y.Total, *machine.IdealCycle );
					y.QualityCoverage = 1;
				}
				if( y.Availability && y.Quality )
					y.Measured = y.Measured|EFactors::Quality;
			}
			product( y );
			return y;
		}
	}

	α Oee::Figures( const Machine& machine, const History& history, const Calendar::Schedule* schedule, TimePoint start, TimePoint end, optional<Calendar::BucketBy> by, SL sl )ε->vector<Bucket>{
		vector<Bucket> y;
		if( end<=start )
			return y;
		vector<Calendar::Bucket> buckets;
		if( by ){
			let shift = std::get_if<Calendar::EBucket>( &*by );
			REFUSE_IF( !schedule && shift && *shift==Calendar::EBucket::Shift, "'{}' has no calendar, so no shifts:  by shift needs one.", machine.Name );
			buckets = ( schedule ? *schedule : utc() ).Buckets( start, end, *by, sl );
		}
		else
			buckets.push_back( {start, end, {}} );
		let cover = covered( history );
		let stops = pieces( machine, history, clip({start, end}, cover), cover.End );
		y.reserve( buckets.size() );
		for( auto& bounds : buckets )
			y.push_back( figure(machine, history, schedule, stops, cover, move(bounds)) );
		return y;
	}

	α Oee::Losses( const Machine& machine, const History& history, const Calendar::Schedule* schedule, TimePoint start, TimePoint end )ι->vector<Loss>{
		struct Row final{ Loss Value; const Stop* Last{}; };
		vector<Row> rows;
		let cover = covered( history );
		let at = clip( {start, end}, cover );
		let shifts = planned( schedule, at );
		for( let& p : pieces(machine, history, at, cover.End) ){
			let d = overlap( shifts, p.At );
			if( d==Duration::zero() )
				continue;
			let kind = classOf( p );
			let loss = kind==EClass::Small ? ELoss::Small : p.Cause ? ELoss::Reason : ELoss::Uncoded;
			auto row = std::ranges::find_if( rows, [&]( const Row& r ){ return r.Value.Kind==loss && (loss!=ELoss::Reason || r.Value.Cause->Id==p.Cause->Id); } );
			if( row==rows.end() )
				row = rows.insert( rows.end(), Row{{loss, loss==ELoss::Reason ? optional<Reason>{*p.Cause} : nullopt}} );
			row->Value.Time += d;
			if( std::exchange(row->Last, p.Of)!=p.Of )
				++row->Value.Stops;
		}
		std::ranges::stable_sort( rows, []( const Row& a, const Row& b ){ return a.Value.Time>b.Value.Time; } );
		vector<Loss> y;
		y.reserve( rows.size() );
		for( auto& row : rows )
			y.push_back( move(row.Value) );
		return y;
	}

	α Oee::Timeline( const Machine& machine, const History& history, const Calendar::Schedule* schedule, TimePoint start, TimePoint end )ι->MachineTimeline{
		MachineTimeline y;
		if( end<=start )
			return y;
		if( schedule )
			y.Planned = schedule->Occurrences( start, end );
		else
			y.Planned.push_back( {nullopt, {}, year_month_day{floor<days>(start)}, start, end, {}} );
		let cover = covered( history );
		let at = clip( {start, end}, cover );
		if( at.End<=at.Start )
			return y;
		auto cursor = at.Start;
		auto running = [&]( TimePoint to ){
			if( to>cursor )
				y.Segments.push_back( {cursor, to, EState::Running} );
		};
		auto id = []( const Stop& stop ){ return stop.Id ? optional<uint32>{ stop.Id } : nullopt; };
		let stops = pieces( machine, history, at, cover.End );
		auto piece = stops.begin();
		for( let& stop : history.Stops ){
			if( stop.Unknown ){
				let in = clip( {stop.Start, stop.End.value_or(cover.End)}, at );
				if( in.End<=in.Start )
					continue;
				running( in.Start );
				y.Segments.push_back( {in.Start, in.End, EState::Unknown, id(stop)} );
				cursor = in.End;
			}
			else for( ; piece!=stops.end() && piece->Of==&stop; ++piece ){
				running( piece->At.Start );
				y.Segments.push_back( {piece->At.Start, piece->At.End, EState::Stopped, id(stop), piece->Cause ? optional<Reason>{*piece->Cause} : nullopt, piece->Small, !piece->Cause,
					piece->By ? optional<uint32>{piece->By->Id} : nullopt} );
				cursor = piece->At.End;
			}
		}
		running( at.End );
		return y;
	}
	namespace Oee{
	α Lines::Combine( TimePoint start, TimePoint end, string shift, std::span<const Bucket> members )ι->Bucket{
		Bucket y{ start, end, move(shift) };
		y.Machines = 0;
		for( let& m : members ){
			y.Planned += m.Planned; y.Run += m.Run; y.Unplanned += m.Unplanned; y.PlannedStop += m.PlannedStop;
			y.Small += m.Small; y.Excluded += m.Excluded; y.Unknown += m.Unknown; y.Whole += m.Whole;
			y.Total += m.Total; y.Good += m.Good; y.Reject += m.Reject; y.Outside += m.Outside;//as they are:  a sum across units means what the units let it.
			y.Ideal += m.Ideal; y.PacedRun += m.PacedRun; y.GoodIdeal += m.GoodIdeal; y.GradedIdeal += m.GradedIdeal;
			y.Machines += m.Machines; y.PerformanceCoverage += m.PerformanceCoverage; y.QualityCoverage += m.QualityCoverage;
			y.Partial = y.Partial || m.Partial;
		}
		//Each factor over the machines that measure it, so each is its members' weighted by the time it is over.
		y.Availability = ratio( y.Run, y.Planned );
		y.Performance = ratio( y.Ideal, y.PacedRun );
		y.Quality = ratio( y.GoodIdeal, y.GradedIdeal );
		if( y.Availability )
			y.Measured = EFactors::Availability;
		if( y.Availability && y.Performance && y.PerformanceCoverage==y.Machines )
			y.Measured = y.Measured|EFactors::Performance;
		if( y.Availability && y.Quality && y.QualityCoverage==y.Machines )
			y.Measured = y.Measured|EFactors::Quality;
		product( y );
		return y;
	}
}}