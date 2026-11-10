#include "OeeQLAwait.h"
#include <charconv>
#include <jde/fwk/str.h>
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

	Ω isRead( sv name )ι->bool{ return std::ranges::find( OeeQLAwait::Tables, name )!=OeeQLAwait::Tables.end(); }
	Ω engineTable( sv name )ι->bool{ return name=="oeeIntervals" || name=="oeeInterval" || name=="oeeCodings" || name=="oeeCoding" || name=="oeeStops" || name=="oeeStop"; }
	//An engine table selected under `q`, at any depth.
	Ω nested( const QL::TableQL& q )ι->const QL::TableQL*{
		for( let children : {&q.Tables, &q.InlineFragments} ){
			for( let& child : *children ){
				if( engineTable(child.JsonName) )
					return &child;
				if( let p = nested(child); p )
					return p;
			}
		}
		return nullptr;
	}
	α OeeQLAwait::IsApplicable( const QL::TableQL& q )ι->bool{
		return isRead( q.JsonName ) || engineTable( q.JsonName ) || nested( q );
	}

	Ω ms( Duration d )ι->int64_t{ return std::chrono::duration_cast<std::chrono::milliseconds>( d ).count(); }
	Ω ticks( TimePoint t )ι->int64_t{ return UADateTime{ t }.UA(); }
	Ω number( optional<double> v )ι->jvalue{ return v ? jvalue{ *v } : jvalue{}; }
	Ω machineJson( const OeeMachineRow& row )ι->jobject{ return { {"id", row.Id}, {"name", row.Name} }; }

	//What a read is made from:  the machine as the formulas take it, its calendar's snapshot, and the range's rows.
	struct OeeRead final{
		Oee::Machine Machine;
		sp<const Calendar::Schedule> Schedule;//none without a calendar, or with one the client doesn't hold:  every minute planned.
		Oee::Span Range;
		TimePoint Now;//the engine's time, which an open stop runs to.
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
		if( let p = q.FindPtr("by"); p && !p->is_null() ){
			REFUSE_IF( !p->is_string(), "by is shift, day, week, month or an ISO 8601 duration, not {}.", serialize(*p) );
			by = Calendar::ParseBucketBy( p->get_string(), sl );
			if( let width = std::get_if<Duration>(&*by); width ){
				for( let& row : r.Rows )//no narrower than the widest row:  a bucket inside a row is that row's share, not a figure.
					*width = std::max( *width, row.Width );
				let count = ( r.Range.End-r.Range.Start ) / *width;
				REFUSE_IF( (uint)count>r.Limit, "by {} over {} to {} is over {} buckets, more than readLimit, {}:  ask for a shorter range, or a wider by.", Chrono::ToString(*width), OeeQL::Iso(r.Range.Start), OeeQL::Iso(r.Range.End), count, r.Limit );
			}
		}
		let shift = by && std::holds_alternative<Calendar::EBucket>( *by ) && std::get<Calendar::EBucket>( *by )==Calendar::EBucket::Shift;
		let buckets = Oee::Figures( r.Machine, r.History(), r.Schedule.get(), r.Range.Start, r.Range.End, by, sl );
		REFUSE_IF( buckets.size()>r.Limit, "{} buckets is more than readLimit, {}:  ask for a shorter range, or a wider by.", buckets.size(), r.Limit );
		jarray y; y.reserve( buckets.size() );
		for( let& bucket : buckets )
			y.emplace_back( OeeQL::Trim(q, bucketJson(bucket, shift)) );
		return y;
	}

	Ω stops( const QL::TableQL& q, const OeeRead& r, const OeeMachineRow& machine, SL sl )ε->jvalue{
		let asked = q.FindPtr( "uncoded" );
		let uncodedOnly = asked && asked->is_bool() && asked->get_bool();
		let limit = [&]{ let n = q.TryNumber<uint>( "limit" ).value_or( 0 ); return n && n<r.Limit ? n : r.Limit; }();
		//The continuation:  the read's arguments, which the next page's must be, and the start of the last stop listed.
		let key = Ƒ( "{}|{}|{}|{}", machine.Id, ticks(r.Range.Start), ticks(r.Range.End), uncodedOnly ? 1 : 0 );
		optional<TimePoint> before;
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
			REFUSE_IF( !parsed || sv{text}.substr(0, bar)!=key, "continuation is no page of this read:  one is good for the arguments it was answered to." );
			before = UADateTime{ UA_DateTime{start} }.Time();
		}
		jarray listed;
		string continuation;
		TimePoint last;
		for( auto p = r.Stops.rbegin(); p!=r.Stops.rend(); ++p ){
			let& stop = *p;
			if( stop.Unknown || (before && stop.Start>=*before) )
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
			for( let& coding : r.CodingRows ){
				if( coding.Value.End<=stop.Start || coding.Value.Start>=end )
					continue;
				gap = gap || coding.Value.Start>covered;
				covered = std::max( covered, coding.Value.End );
				let reason = r.Reason( coding.Value.ReasonId );
				if( coding.Value.Start<=stop.Start && coding.Value.End>=end )
					whole = reason;
				codings.emplace_back( OeeQL::CodingJson(coding, reason) );
			}
			gap = gap || covered<end;
			let uncoded = !small && gap && !coded;
			if( uncodedOnly && !uncoded )
				continue;
			if( listed.size()==limit ){
				continuation = Str::Encode64( Ƒ("{}|{}", key, ticks(last)), true );
				break;
			}
			listed.emplace_back( jobject{
				{"id", stop.Id}, {"start", OeeQL::Iso(stop.Start)}, {"end", stop.End ? jvalue{OeeQL::Iso(*stop.End)} : jvalue{}}, {"ms", ms(end-stop.Start)}, {"small", small}, {"open", !stop.End},
				{"code", stop.Code ? jvalue{*stop.Code} : jvalue{}}, {"uncoded", uncoded}, {"reason", OeeQL::ReasonJson(whole ? whole : coded)}, {"codings", move(codings)}, {"machine", machineJson(machine)}
			} );
			last = stop.Start;
		}
		return OeeQL::Trim( q, jobject{ {"stops", move(listed)}, {"continuation", continuation.empty() ? jvalue{} : jvalue{move(continuation)}} } );
	}

	Ω losses( const QL::TableQL& q, const OeeRead& r )ι->jvalue{
		jarray y;
		for( let& loss : Oee::Losses(r.Machine, r.History(), r.Schedule.get(), r.Range.Start, r.Range.End) ){
			let kind = loss.Kind==Oee::ELoss::Small ? "small" : loss.Kind==Oee::ELoss::Uncoded ? "uncoded" : "reason";
			y.emplace_back( OeeQL::Trim(q, jobject{ {"kind", kind}, {"reason", OeeQL::ReasonJson(loss.Cause ? &*loss.Cause : nullptr)}, {"ms", ms(loss.Time)}, {"stops", loss.Stops} }) );
		}
		return y;
	}

	Ω timeline( const QL::TableQL& q, const OeeRead& r, const OeeMachineRow& machine, SL sl )ε->jvalue{
		let drawn = Oee::Timeline( r.Machine, r.History(), r.Schedule.get(), r.Range.Start, r.Range.End );
		REFUSE_IF( drawn.Segments.size()>r.Limit, "{} to {} is {} segments, more than readLimit, {}:  narrow the range.", OeeQL::Iso(r.Range.Start), OeeQL::Iso(r.Range.End), drawn.Segments.size(), r.Limit );
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
		return jarray{ OeeQL::Trim(q, jobject{ {"machine", machineJson(machine)}, {"planned", move(planned)}, {"segments", move(segments)} }) };
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
		return OeeQL::Trim( q, jobject{
			{"machine", machineJson(row)}, {"enabled", oee.Enabled()},
			{"calendar", !row.Calendar ? "none" : Calendar::Client::Calendars::Find(*row.Calendar) ? "found" : "missing"}, {"calendarId", row.Calendar ? jvalue{*row.Calendar} : jvalue{}},
			{"processed", processed ? jvalue{OeeQL::Iso(*processed)} : jvalue{}}, {"engine", move(state)}, {"why", why.empty() ? jvalue{} : jvalue{move(why)}},
			{"state", live ? jvalue{Oee::ToString(live->State)} : jvalue{}}
		} );
	}
	//Read on the machine's group, as a read of its history asks it.  `lenient`, the status read's:  past the group's rights, a
	//gateway without its historian, or one that doesn't host the group, is what the status says.
	Ω gate( const OeeMachineRow& row, bool lenient, UserPK user, SL sl )ε->void{
		try{
			HistHost::Instance().Gate( row.Group, Access::ERights::Read, false, user, sl );
		}
		catch( Exception& e ){
			if( !lenient || (e.HttpStatus()!=EHttpStatus::ServiceUnavailable && e.HttpStatus()!=EHttpStatus::NotFound) )
				throw;
			e.SetLevel( ELogLevel::NoLog );
		}
	}

	α OeeQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			let sl = _sl;
			let& name = _query.JsonName;
			if( let inner = nested(_query); inner || !isRead(name) ){
				let table = inner ? inner : &_query;
				REFUSE( "'{}' takes no generic query:  the OEE engine's rows are read through oee, oeeStops, oeeLosses and oeeTimeline, each past its machine's group's gate.", table->JsonName );
			}
			let machineId = _query.TryNumber<OeeMachinePK>( "machine" );
			REFUSE_IF( !machineId, "{} needs machine, an oee_machines id.", name );
			auto& oee = OeeHost::Instance();
			let machine = oee.Find( *machineId );
			if( !machine ){
				AppClient()->Acl()->Test( "gateway", "histGroups", Access::ERights::Read, UserPK(), sl );//the table's, before a caller learns of an id.
				throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Machine {} not found.", *machineId };
			}
			if( name==StatusCommand ){
				gate( machine->Row, true, UserPK(), sl );
				auto rows = co_await Any( SelectOeeMachines({.Key=DB::Key{(uint)*machineId}, .IncludeDeleted=true}) );
				Resume( status(_query, *machine, rows.empty() ? machine->Row : rows.front()) );
				co_return;
			}
			gate( machine->Row, false, UserPK(), sl );
			oee.TestEnabled( sl );
			let signals = ToSignals( machine->Signals );
			OeeRead r{
				.Machine={ machine->Row.Id, machine->Row.Name, machine->Row.IdealCycle, machine->Row.SmallStop, Oee::HasCount(signals), Oee::MeasuresQuality(signals) },
				.Schedule=machine->Row.Calendar ? Calendar::Client::Calendars::Find( *machine->Row.Calendar ) : nullptr,
				.Range={ OeeQL::Time(_query, "start", sl), OeeQL::Time(_query, "end", sl) },
				.Now={}, .Limit=HistQL::ReadLimit(), .Rows={}, .Stops={}, .CodingRows={}, .Codings={}, .Reasons={}
			};
			REFUSE_IF( r.Range.Start>=r.Range.End, "start, {}, must be before end, {}.", OeeQL::Iso(r.Range.Start), OeeQL::Iso(r.Range.End) );
			//The engine's unwritten rows before the table's:  a row written between the two is then in both, and the table's is kept.
			let engine = oee.Engine( *machineId );
			let live = engine ? engine->State() : optional<Oee::LiveState>{};
			let clock = oee.Time();
			r.Now = live ? live->At : clock ? clock->Now() : Clock::now();
			for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectStopsOver(*machineId, r.Range.Start, r.Range.End))) )
				r.Stops.push_back( OeeSql::ToStop(row) );
			auto coded = r.Range;//with the stops that overlap it, whole:  a stop is listed with every coding over it.
			for( let& stop : r.Stops ){
				coded.Start = std::min( coded.Start, stop.Start );
				coded.End = std::max( coded.End, stop.End.value_or(r.Now) );
			}
			for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectCodingsOver(*machineId, coded))) ){
				r.CodingRows.push_back( OeeSql::ToCoding(row) );
				r.Codings.push_back( r.CodingRows.back().Value );
			}
			for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectReasons())) )
				r.Reasons.push_back( OeeSql::ToReason(row) );
			if( name!=StopsCommand ){
				for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectIntervalRows(*machineId, r.Range))) )
					r.Rows.push_back( OeeSql::ToInterval(row) );
				if( live ){
					for( let& row : live->Rows ){
						if( row.Width>Duration::zero() && row.Start<r.Range.End && row.End()>r.Range.Start && (r.Rows.empty() || row.Start>=r.Rows.back().End()) )
							r.Rows.push_back( row );
					}
				}
			}
			jvalue y;
			if( name==FiguresCommand )
				y = figures( _query, r, sl );
			else if( name==StopsCommand )
				y = stops( _query, r, machine->Row, sl );
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
	α OeeQL::Trim( const QL::TableQL& q, const jobject& full )ι->jobject{
		jobject y;
		for( let& column : q.Columns ){
			if( let p = full.if_contains(column.JsonName); p )
				y[column.JsonName] = *p;
		}
		for( let& table : q.Tables ){
			let p = full.if_contains( table.JsonName );
			if( !p )
				continue;
			if( let o = p->if_object(); o )
				y[table.JsonName] = Trim( table, *o );
			else if( let a = p->if_array(); a ){
				jarray rows; rows.reserve( a->size() );
				for( let& row : *a )
					rows.emplace_back( row.is_object() ? jvalue{Trim(table, row.get_object())} : row );
				y[table.JsonName] = move( rows );
			}
			else
				y[table.JsonName] = *p;
		}
		return y;
	}
	α OeeQL::ReasonJson( const Oee::Reason* reason )ι->jvalue{
		return reason ? jvalue{ jobject{{"id", reason->Id}, {"name", reason->Name}, {"category", Oee::ToString(reason->Category)}} } : jvalue{};
	}
	α OeeQL::CodingJson( const OeeCodingRow& coding, const Oee::Reason* reason )ι->jobject{
		let& c = coding.Value;
		return { {"id", c.Id}, {"start", Iso(c.Start)}, {"end", Iso(c.End)}, {"reason", ReasonJson(reason)}, {"comment", c.Comment.empty() ? jvalue{} : jvalue{c.Comment}}, {"userName", c.UserName}, {"time", ToIsoString<std::chrono::seconds>(coding.Created)} };
	}
}