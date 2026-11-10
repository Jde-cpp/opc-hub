#include "OeeQLAwait.h"
#include <jde/fwk/str.h>
#include <jde/fwk/co/AllAwait.h>
#include <jde/fwk/exceptions/refuse.h>
#include <jde/access/Authorize.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/calendar/client/Calendars.h>
#include <jde/oee/Figures.h>
#include "../GatewayAppClient.h"
#include "../hist/HistHost.h"
#include "../oee/OeeHost.h"
#include "HistQL.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };
	using Ticks = std::chrono::duration<int64_t,std::ratio<1,10'000'000>>;//UA's.

	α OeeQLAwait::IsApplicable( const QL::TableQL& q )ι->bool{
		return std::ranges::find( Tables, q.JsonName )!=Tables.end();
	}

	Ω ms( Duration d )ι->int64_t{ return std::chrono::duration_cast<std::chrono::milliseconds>( d ).count(); }
	Ω ticks( TimePoint t )ι->int64_t{ return UADateTime{ t }.UA(); }
	Ω number( optional<double> v )ι->jvalue{ return v ? jvalue{ *v } : jvalue{}; }
	Ω machineJson( const OeeMachineRow& row )ι->jobject{ return { {"id", row.Id}, {"name", row.Name} }; }

	//What a read is made from:  the machine as the formulas take it, its calendar's snapshot, and the range's rows.
	struct OeeRead final{
		Oee::Machine Machine;
		sp<const Calendar::Schedule> Schedule;//none without a calendar, or with one the client doesn't hold:  every minute planned (spec *Calendars*).
		Oee::Span Range;
		TimePoint Now;//where oeeStops ends an open stop:  the engine's time, else the table's processed.
		uint Limit;//readLimit.
		vector<Oee::Interval> Rows;
		vector<Oee::Stop> Stops;
		vector<OeeCodingRow> CodingRows;
		vector<Oee::Coding> Codings;
		vector<Oee::Reason> Reasons;
		α History()Ι->Oee::History{ return { Rows, Stops, Codings, Reasons }; }
		α Reason( uint32 id )Ι->const Oee::Reason*{ let p = std::ranges::find( Reasons, id, &Oee::Reason::Id ); return p==Reasons.end() ? nullptr : &*p; }
	};

	Ω bucketJson( const Oee::Bucket& b, bool shift )ι->jobject{
		using enum Oee::EFactors;
		jarray measured;
		for( let& [factor, name] : {std::pair{Availability, "availability"}, std::pair{Performance, "performance"}, std::pair{Quality, "quality"}} ){
			if( b.Measured & factor )
				measured.emplace_back( name );
		}
		return {
			{"start", OeeQL::Iso(b.Start)}, {"end", OeeQL::Iso(b.End)}, {"shift", shift ? jvalue{b.Shift} : jvalue{}},
			{"planned", ms(b.Planned)}, {"run", ms(b.Run)}, {"unplanned", ms(b.Unplanned)}, {"plannedStop", ms(b.PlannedStop)}, {"small", ms(b.Small)}, {"excluded", ms(b.Excluded)}, {"unknown", ms(b.Unknown)},
			{"total", b.Total}, {"good", b.Good}, {"reject", b.Reject}, {"outside", b.Outside}, {"ideal", ms(b.Ideal)},
			{"availability", number(b.Availability)}, {"performance", number(b.Performance)}, {"quality", number(b.Quality)}, {"oee", number(b.OEE)}, {"teep", number(b.TEEP)},
			{"measured", move(measured)}, {"partial", b.Partial}
		};
	}
	Ω figures( const QL::TableQL& q, const OeeRead& r, SL sl )ε->jvalue{
		optional<Calendar::BucketBy> by;
		bool shift{};
		if( let p = q.FindPtr("by"); p && !p->is_null() ){
			REFUSE_IF( !p->is_string(), "by is shift, day, week, month or an ISO 8601 duration, not {}.", serialize(*p) );
			by = Calendar::ParseBucketBy( p->get_string(), sl );
			//Counted from the range before Figures builds them, at the least:  a calendar's day is 25 hours at most, its week and month
			//an hour over 7 and 31 days, and shifts are walked a day at a time.
			Duration most;
			let width = std::get_if<Duration>( &*by );
			if( width ){
				for( let& row : r.Rows )//no narrower than the widest row:  a bucket inside a row is that row's share, not a figure.
					*width = std::max( *width, row.Width );
				most = *width;
			}
			else{
				using enum Calendar::EBucket;
				let bucket = std::get<Calendar::EBucket>( *by );
				shift = bucket==Shift;
				most = bucket==Day ? hours{25} : bucket==Week ? hours{7*24+1} : bucket==Month ? hours{31*24+1} : hours{24};
			}
			let count = ( r.Range.End-r.Range.Start )/most;
			REFUSE_IF( (uint)count>r.Limit, "by {} over {} to {} is {} {} or more, more than readLimit, {}:  ask for a shorter range, or a wider by.", width ? Chrono::ToString(*width) : string{p->get_string()}, OeeQL::Iso(r.Range.Start), OeeQL::Iso(r.Range.End), count, shift ? "days" : "buckets", r.Limit );
		}
		let buckets = Oee::Figures( r.Machine, r.History(), r.Schedule.get(), r.Range.Start, r.Range.End, by, sl );
		REFUSE_IF( buckets.size()>r.Limit, "{} buckets is more than readLimit, {}:  ask for a shorter range, or a wider by.", buckets.size(), r.Limit );
		jarray y; y.reserve( buckets.size() );
		for( let& bucket : buckets )
			y.emplace_back( q.TrimColumns(bucketJson(bucket, shift)) );
		return y;
	}

	//oeeStops' paging:  its arguments, which the next page's must be, and the start of the last stop the page before listed.
	struct StopsPage final{
		bool UncodedOnly;
		string Key;
		optional<TimePoint> Before;
	};
	Ω stopsPage( const QL::TableQL& q, OeeMachinePK machine, Oee::Span range, SL sl )ε->StopsPage{
		let asked = q.FindPtr( "uncoded" );
		StopsPage y{ .UncodedOnly=asked && asked->is_bool() && asked->get_bool(), .Key={}, .Before={} };
		y.Key = Ƒ( "{}|{}|{}|{}", machine, ticks(range.Start), ticks(range.End), y.UncodedOnly ? 1 : 0 );
		if( let p = q.FindPtr("continuation"); p && !p->is_null() ){
			string text;
			try{
				THROW_IF( !p->is_string(), "not text" );
				text = Str::Decode64( p->get_string(), true, sl );
			}
			catch( const std::exception& ){
				text.clear();
			}
			let bar = text.rfind( '|' );
			int64_t start{};
			let cursor = bar==string::npos ? sv{} : sv{ text }.substr( bar+1 );
			let parsed = cursor.size() && std::from_chars( cursor.data(), cursor.data()+cursor.size(), start ).ec==std::errc{};
			REFUSE_IF( !parsed || sv{text}.substr(0, bar)!=y.Key, "continuation is no page of this read:  one is good for the arguments it was answered to." );
			y.Before = UADateTime{ UA_DateTime{start} }.Time();
		}
		return y;
	}
	//`r.Stops` start before `page.Before`, the SQL's bound.
	Ω stops( const QL::TableQL& q, const OeeRead& r, const OeeMachineRow& machine, const StopsPage& page )ε->jvalue{
		let limit = [&]{ let n = q.TryNumber<uint>( "limit" ).value_or( 0 ); return n && n<r.Limit ? n : r.Limit; }();
		jarray listed;
		string continuation;
		TimePoint last;
		for( auto p = r.Stops.rbegin(); p!=r.Stops.rend(); ++p ){
			let& stop = *p;
			if( stop.Unknown )
				continue;
			let end = stop.End.value_or( std::max(r.Now, stop.Start) );
			let small = end-stop.Start<r.Machine.SmallStop;
			const Oee::Reason* coded{};
			if( let cause = stop.Code ? std::ranges::find(r.Reasons, stop.Code, &Oee::Reason::Code) : r.Reasons.end(); cause!=r.Reasons.end() )
				coded = &*cause;
			jarray codings;
			const Oee::Reason* whole{};//the reason of a coding over the whole stop.
			bool gap{};
			auto covered = stop.Start;
			//A machine's codings don't overlap and are read by start, so their ends are in order too:  those over the stop are a run.
			let first = std::ranges::partition_point( r.CodingRows, [&]( const OeeCodingRow& c ){ return c.Value.End<=stop.Start; } );
			for( auto c = first; c!=r.CodingRows.end() && c->Value.Start<end; ++c ){
				let& coding = *c;
				gap = gap || coding.Value.Start>covered;
				covered = std::max( covered, coding.Value.End );
				let reason = r.Reason( coding.Value.ReasonId );
				if( coding.Value.Start<=stop.Start && coding.Value.End>=end )
					whole = reason;
				codings.emplace_back( OeeQL::CodingJson(coding, reason) );
			}
			gap = gap || covered<end;
			let uncoded = !small && gap && !coded;
			if( page.UncodedOnly && !uncoded )
				continue;
			if( listed.size()==limit ){
				continuation = Str::Encode64( Ƒ("{}|{}", page.Key, ticks(last)), true );
				break;
			}
			listed.emplace_back( jobject{
				{"id", stop.Id}, {"start", OeeQL::Iso(stop.Start)}, {"end", stop.End ? jvalue{OeeQL::Iso(*stop.End)} : jvalue{}}, {"ms", ms(end-stop.Start)}, {"small", small}, {"open", !stop.End},
				{"code", stop.Code ? jvalue{*stop.Code} : jvalue{}}, {"uncoded", uncoded}, {"reason", OeeQL::ReasonJson(whole ? whole : coded)}, {"codings", move(codings)}, {"machine", machineJson(machine)}
			} );
			last = stop.Start;
		}
		return q.TrimColumns( jobject{ {"stops", move(listed)}, {"continuation", continuation.empty() ? jvalue{} : jvalue{move(continuation)}} } );
	}

	Ω losses( const QL::TableQL& q, const OeeRead& r )ι->jvalue{
		jarray y;
		for( let& loss : Oee::Losses(r.Machine, r.History(), r.Schedule.get(), r.Range.Start, r.Range.End) ){
			let kind = loss.Kind==Oee::ELoss::Small ? "small" : loss.Kind==Oee::ELoss::Uncoded ? "uncoded" : "reason";
			y.emplace_back( q.TrimColumns(jobject{ {"kind", kind}, {"reason", OeeQL::ReasonJson(loss.Cause ? &*loss.Cause : nullptr)}, {"ms", ms(loss.Time)}, {"stops", loss.Stops} }) );
		}
		return y;
	}

	Ω timeline( const QL::TableQL& q, const OeeRead& r, const OeeMachineRow& machine, SL sl )ε->jvalue{
		//The planned band walks the calendar a day at a time over the whole range, whatever the rows.
		let days = ( r.Range.End-r.Range.Start )/std::chrono::days{ 1 };
		REFUSE_IF( r.Schedule && (uint)days>r.Limit, "{} to {} is {} days of the calendar, more than readLimit, {}:  narrow the range.", OeeQL::Iso(r.Range.Start), OeeQL::Iso(r.Range.End), days, r.Limit );
		let drawn = Oee::Timeline( r.Machine, r.History(), r.Schedule.get(), r.Range.Start, r.Range.End );
		REFUSE_IF( drawn.Segments.size()>r.Limit, "{} to {} is {} segments, more than readLimit, {}:  narrow the range.", OeeQL::Iso(r.Range.Start), OeeQL::Iso(r.Range.End), drawn.Segments.size(), r.Limit );
		REFUSE_IF( drawn.Planned.size()>r.Limit, "{} to {} is {} planned occurrences, more than readLimit, {}:  narrow the range.", OeeQL::Iso(r.Range.Start), OeeQL::Iso(r.Range.End), drawn.Planned.size(), r.Limit );
		jarray planned;
		for( let& occurrence : drawn.Planned )
			planned.emplace_back( Calendar::ToJson(occurrence) );
		jarray segments;
		for( let& s : drawn.Segments ){
			jvalue coding;
			if( let by = s.CodingId ? std::ranges::find(r.Codings, *s.CodingId, &Oee::Coding::Id) : r.Codings.end(); by!=r.Codings.end() )
				coding = jobject{ {"id", by->Id}, {"comment", by->Comment.empty() ? jvalue{} : jvalue{by->Comment}}, {"userName", by->UserName} };
			segments.emplace_back( jobject{
				{"start", OeeQL::Iso(s.Start)}, {"end", OeeQL::Iso(s.End)}, {"state", Oee::ToString(s.State)}, {"stop", s.StopId ? jvalue{*s.StopId} : jvalue{}},
				{"reason", OeeQL::ReasonJson(s.Cause ? &*s.Cause : nullptr)}, {"small", s.Small}, {"uncoded", s.Uncoded}, {"coding", move(coding)}
			} );
		}
		return jarray{ q.TrimColumns(jobject{ {"machine", machineJson(machine)}, {"planned", move(planned)}, {"segments", move(segments)} }) };
	}

	//`row`:  the table's now, whose `processed` an engine since ended has moved.
	Ω status( const QL::TableQL& q, const OeeHost::Machine& machine, const OeeMachineRow& row )ι->jvalue{
		auto& oee = OeeHost::Instance();
		let engine = oee.Engine( row.Id );
		let live = engine ? engine->State() : optional<Oee::LiveState>{};
		string state, why;
		if( row.Deleted )
			state = "deleted";
		else if( engine )
			state = engine->Live() ? "live" : "starting";
		else{
			why = oee.Blocked( machine );
			state = why.empty() ? "waiting" : "blocked";
		}
		let processed = live ? optional<TimePoint>{ live->Processed } : row.Processed;
		return q.TrimColumns( jobject{
			{"machine", machineJson(row)}, {"enabled", oee.Enabled()},
			{"calendar", !row.Calendar ? "none" : Calendar::Client::Calendars::Find(*row.Calendar) ? "found" : "missing"}, {"calendarId", row.Calendar ? jvalue{*row.Calendar} : jvalue{}},
			{"processed", processed ? jvalue{OeeQL::Iso(*processed)} : jvalue{}}, {"engine", move(state)}, {"why", why.empty() ? jvalue{} : jvalue{move(why)}},
			{"state", live ? jvalue{Oee::ToString(live->State)} : jvalue{}}
		} );
	}
	α OeeQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			let sl = _sl;
			let& name = _query.JsonName;
			_query.CheckVariables( sl );//before an argument is read:  an unbound one would read as absent.
			let machineId = _query.TryNumber<OeeMachinePK>( "machine" );
			REFUSE_IF( !machineId, "{} needs machine, an oee_machines id.", name );
			auto& oee = OeeHost::Instance();
			let machine = oee.Find( *machineId );
			if( !machine ){
				AppClient()->Acl()->Test( "gateway", "histGroups", Access::ERights::Read, UserPK(), sl );//the table's, before a caller learns of an id.
				throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Machine {} not found.", *machineId };
			}
			if( name==StatusCommand ){//Read on the group's own row, which HistHost::Gate names only for a group it hosts:  past it, a gateway without its historian, or one that doesn't host the group, is what the status says.
				auto [groups, rows] = co_await All( SelectHistGroups({.Key=DB::Key{(uint)machine->Row.Group}, .IncludeDeleted=true}), SelectOeeMachines({.Key=DB::Key{(uint)*machineId}, .IncludeDeleted=true}) );
				AppClient()->Acl()->Test( "gateway", "histGroups", groups.empty() ? string{} : groups.front().Guid, Access::ERights::Read, UserPK(), sl );
				Resume( status(_query, *machine, rows.empty() ? machine->Row : rows.front()) );
				co_return;
			}
			HistHost::Instance().Gate( machine->Row.Group, Access::ERights::Read, false, UserPK(), sl );
			oee.TestEnabled( sl );
			let signals = ToSignals( machine->Signals );
			OeeRead r{
				.Machine={ machine->Row.Id, machine->Row.Name, machine->Row.IdealCycle, machine->Row.SmallStop, Oee::HasCount(signals), Oee::MeasuresQuality(signals) },
				.Schedule=machine->Row.Calendar ? Calendar::Client::Calendars::Find( *machine->Row.Calendar ) : nullptr,
				.Range={ OeeQL::Time(_query, "start", sl), OeeQL::Time(_query, "end", sl) },
				.Now={}, .Limit=HistQL::ReadLimit(), .Rows={}, .Stops={}, .CodingRows={}, .Codings={}, .Reasons={}
			};
			REFUSE_IF( r.Range.Start>=r.Range.End, "start, {}, must be before end, {}.", OeeQL::Iso(r.Range.Start), OeeQL::Iso(r.Range.End) );
			let page = name==StopsCommand ? optional<StopsPage>{ stopsPage(_query, *machineId, r.Range, sl) } : nullopt;
			//The engine's unwritten rows before the table's:  a row written between the two is then in both, and the table's is kept.
			let engine = oee.Engine( *machineId );
			let live = engine ? engine->State() : optional<Oee::LiveState>{};
			let clock = oee.Time();
			r.Now = live ? live->At : clock ? clock->Now() : Clock::now();
			auto stopsSql = OeeSql::SelectStopsOver( *machineId, r.Range.Start, page && page->Before ? std::min(r.Range.End, *page->Before) : r.Range.End );
			auto take = [&]( const vector<DB::Row>& stopRows, const vector<DB::Row>& reasonRows ){
				for( let& row : stopRows )
					r.Stops.push_back( OeeSql::ToStop(row) );
				for( let& row : reasonRows )
					r.Reasons.push_back( OeeSql::ToReason(row) );
			};
			if( page ){//with the machine's row:  with no engine to run an open stop on, it ends where the rows do, the table's processed, which the host's copy lags.
				auto [stopRows, reasonRows, machines] = co_await All( DS()->SelectAsync(move(stopsSql)), DS()->SelectAsync(OeeSql::SelectReasons()), SelectOeeMachines({.Key=DB::Key{(uint)*machineId}, .IncludeDeleted=true}) );
				take( stopRows, reasonRows );
				if( !live && machines.size() && machines.front().Processed )
					r.Now = *machines.front().Processed;
			}
			else{
				auto [stopRows, reasonRows, intervalRows] = co_await All( DS()->SelectAsync(move(stopsSql)), DS()->SelectAsync(OeeSql::SelectReasons()), DS()->SelectAsync(OeeSql::SelectIntervalRows(*machineId, r.Range)) );
				take( stopRows, reasonRows );
				for( let& row : intervalRows )
					r.Rows.push_back( OeeSql::ToInterval(row) );
				if( live ){
					for( let& row : live->Rows ){
						if( row.Width>Duration::zero() && row.Start<r.Range.End && row.End()>r.Range.Start && (r.Rows.empty() || row.Start>=r.Rows.back().End()) )
							r.Rows.push_back( row );
					}
				}
			}
			auto coded = r.Range;//with the stops that overlap it, whole:  a stop is listed with every coding over it.
			for( let& stop : r.Stops ){
				coded.Start = std::min( coded.Start, stop.Start );
				coded.End = std::max( coded.End, stop.End.value_or(r.Now) );
			}
			for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectCodingsOver(*machineId, coded))) ){
				r.CodingRows.push_back( OeeSql::ToCoding(row) );
				r.Codings.push_back( r.CodingRows.back().Value );
			}
			jvalue y;
			if( name==FiguresCommand )
				y = figures( _query, r, sl );
			else if( name==StopsCommand )
				y = stops( _query, r, machine->Row, *page );
			else if( name==LossesCommand )
				y = losses( _query, r );
			else
				y = timeline( _query, r, machine->Row, sl );
			Resume( move(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
	}

	α OeeQL::Time( const QL::Input& input, sv name, SL sl )ε->TimePoint{
		let p = input.FindPtr( name );
		REFUSE_IF( !p || p->is_null(), "{} is required, a time such as 2026-11-09T06:00:00Z.", name );
		try{
			return UADateTime{ UA_DateTime{UADateTime{*p, sl}.UA()} }.Time();
		}
		catch( const std::exception& ){
			REFUSE( "{} is a time such as 2026-11-09T06:00:00Z, not {}.", name, serialize(*p) );
		}
	}
	α OeeQL::Iso( TimePoint t )ι->string{ return ToIsoString<Ticks>( t ); }
	α OeeQL::ReasonJson( const Oee::Reason* reason )ι->jvalue{
		return reason ? jvalue{ jobject{{"id", reason->Id}, {"name", reason->Name}, {"category", Oee::ToString(reason->Category)}} } : jvalue{};
	}
	α OeeQL::CodingJson( const OeeCodingRow& coding, const Oee::Reason* reason )ι->jobject{
		let& c = coding.Value;
		return { {"id", c.Id}, {"start", Iso(c.Start)}, {"end", Iso(c.End)}, {"reason", ReasonJson(reason)}, {"comment", c.Comment.empty() ? jvalue{} : jvalue{c.Comment}}, {"userName", c.UserName}, {"time", ToIsoString<std::chrono::seconds>(coding.Created)} };
	}
}