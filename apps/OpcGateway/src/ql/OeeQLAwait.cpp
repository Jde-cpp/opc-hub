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
		OeeMachineRow Row;
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
	//`by`, a duration raised to `widest`, the widest row in the range:  a bucket inside a row is that row's share, not a figure.
	//Counted from the range before a bucket is built, at the least:  a calendar's day is 25 hours at most, its week and month an
	//hour over 7 and 31 days, and shifts are walked a day at a time.
	struct By final{ optional<Calendar::BucketBy> Value; bool Shift{}; };
	Ω bucketBy( const QL::TableQL& q, Duration widest, Oee::Span range, uint limit, SL sl )ε->By{
		By y;
		let p = q.FindPtr( "by" );
		if( !p || p->is_null() )
			return y;
		REFUSE_IF( !p->is_string(), "by is shift, day, week, month or an ISO 8601 duration, not {}.", serialize(*p) );
		y.Value = Calendar::ParseBucketBy( p->get_string(), sl );
		Duration most;
		let width = std::get_if<Duration>( &*y.Value );
		if( width )
			most = *width = std::max( *width, widest );
		else{
			using enum Calendar::EBucket;
			let bucket = std::get<Calendar::EBucket>( *y.Value );
			y.Shift = bucket==Shift;
			most = bucket==Day ? hours{25} : bucket==Week ? hours{7*24+1} : bucket==Month ? hours{31*24+1} : hours{24};
		}
		let count = ( range.End-range.Start )/most;
		REFUSE_IF( (uint)count>limit, "by {} over {} to {} is {} {} or more, more than readLimit, {}:  ask for a shorter range, or a wider by.", width ? Chrono::ToString(*width) : string{p->get_string()}, OeeQL::Iso(range.Start), OeeQL::Iso(range.End), count, y.Shift ? "days" : "buckets", limit );
		return y;
	}
	Ω widest( const OeeRead& r )ι->Duration{
		Duration y{};
		for( let& row : r.Rows )
			y = std::max( y, row.Width );
		return y;
	}
	Ω figures( const QL::TableQL& q, const OeeRead& r, SL sl )ε->jvalue{
		let by = bucketBy( q, widest(r), r.Range, r.Limit, sl );
		let buckets = Oee::Figures( r.Machine, r.History(), r.Schedule.get(), r.Range.Start, r.Range.End, by.Value, sl );
		REFUSE_IF( buckets.size()>r.Limit, "{} buckets is more than readLimit, {}:  ask for a shorter range, or a wider by.", buckets.size(), r.Limit );
		jarray y; y.reserve( buckets.size() );
		for( let& bucket : buckets )
			y.emplace_back( q.TrimColumns(bucketJson(bucket, by.Shift)) );
		return y;
	}
	//A line's read:  the line, its own calendar's snapshot, and a read per machine it reaches that isn't deleted, in member order.
	struct OeeLineRead final{
		OeeHost::Reached Reached;
		sp<const Calendar::Schedule> Schedule;
		Oee::Span Range;
		uint Limit;
		vector<OeeRead> Machines;
	};
	Ω lineJson( const OeeLineRow& row )ι->jobject{ return { {"id", row.Id}, {"name", row.Name}, {"level", row.Level ? jvalue{Oee::ToString(*row.Level)} : jvalue{}} }; }
	Ω lineBucketJson( const Oee::Bucket& b, bool shift )ι->jobject{
		auto y = bucketJson( b, shift );
		y["machines"] = b.Machines;
		y["coverage"] = jobject{ {"performance", b.PerformanceCoverage}, {"quality", b.QualityCoverage} };
		return y;
	}
	Ω lineFigures( const QL::TableQL& q, const OeeLineRead& l, SL sl )ε->jvalue{
		Duration wide{};
		for( let& r : l.Machines )
			wide = std::max( wide, widest(r) );
		let by = bucketBy( q, wide, l.Range, l.Limit, sl );
		let& line = l.Reached.Line;
		let bounds = Oee::Buckets( l.Schedule.get(), l.Range.Start, l.Range.End, by.Value, line.Name, sl );
		REFUSE_IF( bounds.size()>l.Limit, "{} buckets is more than readLimit, {}:  ask for a shorter range, or a wider by.", bounds.size(), l.Limit );
		let asked = q.FindPtr( "members" );
		let members = asked && asked->is_bool() && asked->get_bool();
		//The direct members that stand, each with where its machines are among l.Machines.
		struct Member final{ jobject Of; vector<uint> Machines; };
		vector<Member> direct;
		if( members ){
			let index = [&]( OeeMachinePK id )ι->optional<uint>{
				let p = std::ranges::find( l.Machines, id, []( const OeeRead& r ){ return r.Row.Id; } );
				return p==l.Machines.end() ? nullopt : optional<uint>{ (uint)(p-l.Machines.begin()) };
			};
			let& lines = l.Reached.Lines;
			let graph = lines.Graph( {}, true );
			for( let& m : lines.Members ){
				if( m.Line!=line.Id )
					continue;
				if( m.Machine ){
					if( let i = index(*m.Machine); i )
						direct.push_back( {{{"machine", machineJson(l.Machines[*i].Row)}, {"line", nullptr}}, {*i}} );
				}
				else if( let under = m.MemberLine ? lines.Find( *m.MemberLine ) : nullptr; under && !under->Deleted ){
					Member member{ {{"machine", nullptr}, {"line", lineJson(*under)}}, {} };
					for( let id : Oee::Lines::Reach(graph, under->Id, sl) ){
						if( let i = index(id); i )
							member.Machines.push_back( *i );
					}
					direct.push_back( move(member) );
				}
			}
		}
		jarray y; y.reserve( bounds.size() );
		vector<uint> first( l.Machines.size() );//each machine's first stop that may reach the bucket:  the buckets are in order.
		vector<Oee::Bucket> buckets( l.Machines.size() );
		for( let& b : bounds ){
			for( uint i=0; i<l.Machines.size(); ++i ){
				let& r = l.Machines[i];
				//Each machine's planned time within the bucket is its own calendar's (spec *Lines*).  Its stops are narrowed to the
				//bucket's, and its rows passed whole, since an open stop runs to where they end.
				while( first[i]<r.Stops.size() && r.Stops[first[i]].End && *r.Stops[first[i]].End<=b.Start )
					++first[i];
				let from = r.Stops.begin()+first[i];
				let to = std::partition_point( from, r.Stops.end(), [&]( const Oee::Stop& s ){ return s.Start<b.End; } );
				let made = Oee::Figures( r.Machine, {r.Rows, {from, to}, r.Codings, r.Reasons}, r.Schedule.get(), b.Start, b.End, {}, sl );
				buckets[i] = made.empty() ? Oee::Bucket{ b.Start, b.End } : made.front();
			}
			auto bucket = lineBucketJson( Oee::Lines::Combine(b.Start, b.End, b.Label, buckets), by.Shift );
			if( members ){
				jarray listed;
				for( let& member : direct ){
					jobject of;
					if( !member.Of.at("machine").is_null() )
						of = lineBucketJson( buckets[member.Machines.front()], by.Shift );
					else{
						vector<Oee::Bucket> under;
						for( let i : member.Machines )
							under.push_back( buckets[i] );
						of = lineBucketJson( Oee::Lines::Combine(b.Start, b.End, b.Label, under), by.Shift );
					}
					for( let& [key,value] : member.Of )
						of[key] = value;
					listed.emplace_back( move(of) );
				}
				bucket["members"] = move( listed );
			}
			y.emplace_back( q.TrimColumns(move(bucket)) );
		}
		return y;
	}

	//oeeStops' paging:  its arguments, which the next page's must be, and the last stop the page before listed, its start and
	//its machine, since two of a line's machines may stop at one time.
	struct StopsPage final{
		bool UncodedOnly;
		string Key;
		optional<std::pair<TimePoint,OeeMachinePK>> After;
		//The stops the SQL reads start before this:  those at the cursor's start among them, for the machines after its own.
		α Before()Ι->optional<TimePoint>{ return After ? optional<TimePoint>{ After->first+TimePoint::duration{1} } : nullopt; }
	};
	//`of`:  m<id> for a machine's read, l<id> for a line's.
	Ω stopsPage( const QL::TableQL& q, string of, Oee::Span range, SL sl )ε->StopsPage{
		let asked = q.FindPtr( "uncoded" );
		StopsPage y{ .UncodedOnly=asked && asked->is_bool() && asked->get_bool(), .Key={}, .After={} };
		y.Key = Ƒ( "{}|{}|{}|{}", of, ticks(range.Start), ticks(range.End), y.UncodedOnly ? 1 : 0 );
		if( let p = q.FindPtr("continuation"); p && !p->is_null() ){
			string text;
			try{
				THROW_IF( !p->is_string(), "not text" );
				text = Str::Decode64( p->get_string(), true, sl );
			}
			catch( const std::exception& ){
				text.clear();
			}
			//key|start|machine.
			let number = []( sv x, auto& y ){ return x.size() && std::from_chars( x.data(), x.data()+x.size(), y ).ec==std::errc{}; };
			let machineBar = text.rfind( '|' );
			let startBar = machineBar==string::npos || !machineBar ? string::npos : text.rfind( '|', machineBar-1 );
			int64_t start{};
			OeeMachinePK machine{};
			let parsed = startBar!=string::npos && number( sv{text}.substr(startBar+1, machineBar-startBar-1), start ) && number( sv{text}.substr(machineBar+1), machine );
			REFUSE_IF( !parsed || sv{text}.substr(0, startBar)!=y.Key, "continuation is no page of this read:  one is good for the arguments it was answered to." );
			y.After = { UADateTime{UA_DateTime{start}}.Time(), machine };
		}
		return y;
	}
	//A stop as oeeStops lists it, and what orders it:  newest first, then by machine.
	struct ListedStop final{
		TimePoint Start;
		OeeMachinePK Machine;
		jobject Json;
	};
	//The machine's stops past the page's cursor, newest first, `most` at most.  `r.Stops` start before `page.Before()`, the SQL's bound.
	Ω stops( const OeeRead& r, const StopsPage& page, uint most, vector<ListedStop>& listed )ε->void{
		let machine = r.Row.Id;
		uint count{};
		for( auto p = r.Stops.rbegin(); p!=r.Stops.rend() && count<most; ++p ){
			let& stop = *p;
			if( stop.Unknown || (page.After && (stop.Start>page.After->first || (stop.Start==page.After->first && machine<=page.After->second))) )
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
			++count;
			listed.push_back( {stop.Start, machine, jobject{
				{"id", stop.Id}, {"start", OeeQL::Iso(stop.Start)}, {"end", stop.End ? jvalue{OeeQL::Iso(*stop.End)} : jvalue{}}, {"ms", ms(end-stop.Start)}, {"small", small}, {"open", !stop.End},
				{"code", stop.Code ? jvalue{*stop.Code} : jvalue{}}, {"uncoded", uncoded}, {"reason", OeeQL::ReasonJson(whole ? whole : coded)}, {"codings", move(codings)}, {"machine", machineJson(r.Row)}
			}} );
		}
	}
	//A page of the machines' stops:  each gives a stop more than the page holds, so the page is the newest of them all and
	//whether another follows is known.
	Ω stops( const QL::TableQL& q, std::span<const OeeRead> machines, const StopsPage& page, uint readLimit )ε->jvalue{
		let limit = [&]{ let n = q.TryNumber<uint>( "limit" ).value_or( 0 ); return n && n<readLimit ? n : readLimit; }();
		vector<ListedStop> found;
		for( let& r : machines )
			stops( r, page, limit+1, found );
		std::ranges::sort( found, []( const ListedStop& a, const ListedStop& b ){ return a.Start!=b.Start ? a.Start>b.Start : a.Machine<b.Machine; } );
		jarray listed;
		string continuation;
		if( found.size()>limit ){
			found.resize( limit );
			continuation = Str::Encode64( Ƒ("{}|{}|{}", page.Key, ticks(found.back().Start), found.back().Machine), true );
		}
		for( auto& stop : found )
			listed.emplace_back( move(stop.Json) );
		return q.TrimColumns( jobject{ {"stops", move(listed)}, {"continuation", continuation.empty() ? jvalue{} : jvalue{move(continuation)}} } );
	}

	//The machines' loss rows summed, longest first:  the reasons are the gateway's, so a row means the same across them.
	Ω losses( const QL::TableQL& q, std::span<const OeeRead> machines )ι->jvalue{
		vector<Oee::Loss> rows;
		for( let& r : machines ){
			for( auto& loss : Oee::Losses(r.Machine, r.History(), r.Schedule.get(), r.Range.Start, r.Range.End) ){
				auto row = std::ranges::find_if( rows, [&]( const Oee::Loss& x ){ return x.Kind==loss.Kind && (loss.Kind!=Oee::ELoss::Reason || x.Cause->Id==loss.Cause->Id); } );
				if( row==rows.end() )
					rows.push_back( move(loss) );
				else{
					row->Time += loss.Time;
					row->Stops += loss.Stops;
				}
			}
		}
		if( machines.size()>1 )
			std::ranges::stable_sort( rows, std::greater{}, &Oee::Loss::Time );
		jarray y;
		for( let& loss : rows ){
			let kind = loss.Kind==Oee::ELoss::Small ? "small" : loss.Kind==Oee::ELoss::Uncoded ? "uncoded" : "reason";
			y.emplace_back( q.TrimColumns(jobject{ {"kind", kind}, {"reason", OeeQL::ReasonJson(loss.Cause ? &*loss.Cause : nullptr)}, {"ms", ms(loss.Time)}, {"stops", loss.Stops} }) );
		}
		return y;
	}

	//One `{ machine planned segments }` per machine, refused above readLimit segments, or planned occurrences, in all.
	Ω timeline( const QL::TableQL& q, std::span<const OeeRead> machines, Oee::Span range, uint limit, SL sl )ε->jvalue{
		//The planned band walks the calendar a day at a time over the whole range, whatever the rows.
		let days = ( range.End-range.Start )/std::chrono::days{ 1 };
		let from = OeeQL::Iso( range.Start ), to = OeeQL::Iso( range.End );
		uint segmentCount{}, plannedCount{};
		jarray y;
		for( let& r : machines ){
			REFUSE_IF( r.Schedule && (uint)days>limit, "{} to {} is {} days of the calendar, more than readLimit, {}:  narrow the range.", from, to, days, limit );
			let drawn = Oee::Timeline( r.Machine, r.History(), r.Schedule.get(), range.Start, range.End );
			segmentCount += drawn.Segments.size();
			plannedCount += drawn.Planned.size();
			REFUSE_IF( segmentCount>limit, "{} to {} is {} segments, more than readLimit, {}:  narrow the range.", from, to, segmentCount, limit );
			REFUSE_IF( plannedCount>limit, "{} to {} is {} planned occurrences, more than readLimit, {}:  narrow the range.", from, to, plannedCount, limit );
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
			y.emplace_back( q.TrimColumns(jobject{ {"machine", machineJson(r.Row)}, {"planned", move(planned)}, {"segments", move(segments)} }) );
		}
		return y;
	}

	//`row`:  the table's now, whose `processed` an engine since ended has moved.
	Ω calendarState( optional<uint32> calendar )ι->sv{ return !calendar ? "none" : Calendar::Client::Calendars::Find( *calendar ) ? "found" : "missing"; }
	Ω status( const OeeHost::Machine& machine, const OeeMachineRow& row )ι->jobject{
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
		return {
			{"machine", machineJson(row)}, {"enabled", oee.Enabled()},
			{"calendar", calendarState(row.Calendar)}, {"calendarId", row.Calendar ? jvalue{*row.Calendar} : jvalue{}},
			{"processed", processed ? jvalue{OeeQL::Iso(*processed)} : jvalue{}}, {"engine", move(state)}, {"why", why.empty() ? jvalue{} : jvalue{move(why)}},
			{"state", live ? jvalue{Oee::ToString(live->State)} : jvalue{}}
		};
	}
	//oeeMachineStatus', past Read on the group's own row, which HistHost::Gate names only for a group it hosts:  past it, a
	//gateway without its historian, or one that doesn't host the group, is what the status says.  Into `y`.
	Ω machineStatus( const OeeHost::Machine& machine, UserPK user, jobject& y, SL sl )ι->AnyVoidTask{
		auto [groups, rows] = co_await All( SelectHistGroups({.Key=DB::Key{(uint)machine.Row.Group}, .IncludeDeleted=true}), SelectOeeMachines({.Key=DB::Key{(uint)machine.Row.Id}, .IncludeDeleted=true}) );
		AppClient()->Acl()->Test( "gateway", "histGroups", groups.empty() ? string{} : groups.front().Guid, Access::ERights::Read, user, sl );
		y = status( machine, rows.empty() ? machine.Row : rows.front() );//the table's row now, whose `processed` an engine since ended has moved.
	}
	//The machine's rows over r.Range into `r`, whose Row, Machine, Schedule, Range, Limit and Reasons are set.  `page`:  oeeStops',
	//which reads no interval.
	Ω load( OeeRead& r, const StopsPage* page )ι->AnyVoidTask{
		auto& oee = OeeHost::Instance();
		let id = r.Row.Id;
		//The engine's unwritten rows before the table's:  a row written between the two is then in both, and the table's is kept.
		let engine = oee.Engine( id );
		let live = engine ? engine->State() : optional<Oee::LiveState>{};
		let clock = oee.Time();
		r.Now = live ? live->At : clock ? clock->Now() : Clock::now();
		let before = page ? page->Before() : nullopt;
		auto stopsSql = OeeSql::SelectStopsOver( id, r.Range.Start, before ? std::min(r.Range.End, *before) : r.Range.End );
		if( page ){//with the machine's row:  with no engine to run an open stop on, it ends where the rows do, the table's processed, which the host's copy lags.
			auto [stopRows, machines] = co_await All( DS()->SelectAsync(move(stopsSql)), SelectOeeMachines({.Key=DB::Key{(uint)id}, .IncludeDeleted=true}) );
			for( let& row : stopRows )
				r.Stops.push_back( OeeSql::ToStop(row) );
			if( !live && machines.size() && machines.front().Processed )
				r.Now = *machines.front().Processed;
		}
		else{
			auto [stopRows, intervalRows] = co_await All( DS()->SelectAsync(move(stopsSql)), DS()->SelectAsync(OeeSql::SelectIntervalRows(id, r.Range)) );
			for( let& row : stopRows )
				r.Stops.push_back( OeeSql::ToStop(row) );
			for( let& row : intervalRows )
				r.Rows.push_back( OeeSql::ToInterval(row) );
			if( live )
				AppendUnwritten( r.Rows, live->Rows, r.Range );
		}
		auto coded = r.Range;//with the stops that overlap it, whole:  a stop is listed with every coding over it.
		for( let& stop : r.Stops ){
			coded.Start = std::min( coded.Start, stop.Start );
			coded.End = std::max( coded.End, stop.End.value_or(r.Now) );
		}
		for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectCodingsOver(id, coded))) ){
			r.CodingRows.push_back( OeeSql::ToCoding(row) );
			r.Codings.push_back( r.CodingRows.back().Value );
		}
	}
	Ω newRead( const OeeHost::Machine& machine, Oee::Span range, const vector<Oee::Reason>& reasons )ι->OeeRead{
		return {
			.Row=machine.Row, .Machine=ToMachine( machine.Row, machine.Signals ),
			.Schedule=machine.Row.Calendar ? Calendar::Client::Calendars::Find( *machine.Row.Calendar ) : nullptr,
			.Range=range, .Now={}, .Limit=HistQL::ReadLimit(), .Rows={}, .Stops={}, .CodingRows={}, .Codings={}, .Reasons=reasons
		};
	}
	α OeeQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			let sl = _sl;
			let& name = _query.JsonName;
			_query.CheckVariables( sl );//before an argument is read:  an unbound one would read as absent.
			let machineId = _query.TryNumber<OeeMachinePK>( "machine" );
			let lineId = _query.TryNumber<OeeLinePK>( "line" );
			REFUSE_IF( name==StatusCommand && !machineId, "{} needs machine, an oee_machines id.", name );
			REFUSE_IF( name==LineStatusCommand && !lineId, "{} needs line, an oee_lines id.", name );
			REFUSE_IF( name!=StatusCommand && name!=LineStatusCommand && machineId.has_value()==lineId.has_value(), "{} needs machine, an oee_machines id, or line, an oee_lines id:  one of the two.", name );
			auto& oee = OeeHost::Instance();
			if( name==LineStatusCommand ){
				let reached = oee.Reach( *lineId );
				if( !reached ){
					AppClient()->Acl()->Test( "gateway", "oeeLines", Access::ERights::Read, UserPK(), sl );//the table's, before a caller learns of an id.
					throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Line {} not found.", *lineId };
				}
				jarray machines;
				for( let& machine : reached->Machines ){
					jobject y;
					co_await machineStatus( machine, UserPK(), y, sl );
					machines.emplace_back( move(y) );
				}
				let& line = reached->Line;
				Resume( _query.TrimColumns(jobject{
					{"line", lineJson(line)}, {"enabled", oee.Enabled()}, {"calendar", calendarState(line.Calendar)}, {"calendarId", line.Calendar ? jvalue{*line.Calendar} : jvalue{}},
					{"deleted", line.Deleted}, {"machines", move(machines)}
				}) );
				co_return;
			}
			optional<OeeHost::Reached> reached;
			vector<OeeHost::Machine> machines;//the read's:  the machine, or those the line reaches that aren't deleted.
			if( lineId ){
				reached = oee.Reach( *lineId, UserPK(), sl );
				for( let& machine : reached->Machines ){
					if( !machine.Row.Deleted )
						machines.push_back( machine );
				}
			}
			else{
				let machine = oee.Find( *machineId );
				if( !machine ){
					AppClient()->Acl()->Test( "gateway", "histGroups", Access::ERights::Read, UserPK(), sl );//the table's, before a caller learns of an id.
					throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Machine {} not found.", *machineId };
				}
				if( name==StatusCommand ){
					jobject y;
					co_await machineStatus( *machine, UserPK(), y, sl );
					Resume( _query.TrimColumns(move(y)) );
					co_return;
				}
				HistHost::Instance().Gate( machine->Row.Group, Access::ERights::Read, false, UserPK(), sl );
				machines.push_back( *machine );
			}
			oee.TestEnabled( sl );
			const Oee::Span range{ OeeQL::Time(_query, "start", sl), OeeQL::Time(_query, "end", sl) };
			REFUSE_IF( range.Start>=range.End, "start, {}, must be before end, {}.", OeeQL::Iso(range.Start), OeeQL::Iso(range.End) );
			let page = name==StopsCommand ? optional<StopsPage>{ stopsPage(_query, lineId ? Ƒ("l{}", *lineId) : Ƒ("m{}", *machineId), range, sl) } : nullopt;
			vector<Oee::Reason> reasons;
			for( let& row : co_await Any(DS()->SelectAsync(OeeSql::SelectReasons())) )
				reasons.push_back( OeeSql::ToReason(row) );
			vector<OeeRead> reads; reads.reserve( machines.size() );
			for( let& machine : machines ){
				reads.push_back( newRead(machine, range, reasons) );
				co_await load( reads.back(), page ? &*page : nullptr );
			}
			let limit = HistQL::ReadLimit();
			jvalue y;
			if( name==StopsCommand )
				y = stops( _query, reads, *page, limit );
			else if( name==LossesCommand )
				y = losses( _query, reads );
			else if( name==TimelineCommand )
				y = timeline( _query, reads, range, limit, sl );
			else if( !reached )
				y = figures( _query, reads.front(), sl );
			else{
				let calendar = reached->Line.Calendar;
				y = lineFigures( _query, OeeLineRead{move(*reached), calendar ? Calendar::Client::Calendars::Find(*calendar) : nullptr, range, limit, move(reads)}, sl );
			}
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