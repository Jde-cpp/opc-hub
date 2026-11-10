#include "OeeRows.h"
#include <jde/db/generators/WhereClause.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/oee/rules.h>

#define let const auto
namespace Jde::Opc::Gateway{
	using std::chrono::milliseconds;
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	Ω select( sv table, vector<string> columns, DB::WhereClause&& where, SL sl )ε->DB::Statement{
		let view = GetViewPtr( table );
		return DB::Statement{ {view->GetColumns(move(columns), sl)}, {view}, move(where) };
	}
	Ω name( sv table )ε->string{ return GetViewPtr( table )->SqlName(); }
	//ms is milliseconds and start UA ticks, a hundred nanoseconds.  The factor is a long parameter:  SQL Server makes ms an int,
	//and an int times the literal 10000 overflows from a 4-minute row.
	Ω intervalsOver( OeeMachinePK machine, Oee::Span span, sv columns, sv order )ε->DB::Sql{
		return { Ƒ("select {} from {} where oee_machine_id=? and start<=? and start+ms*?>?{}", columns, name(OeeTable::Intervals), order), {DB::Value{(uint)machine}, OeeTicks(span.End), DB::Value{(_int)10000}, OeeTicks(span.Start)} };
	}
	Ω fromMs( uint32 ms )ι->Duration{ return milliseconds{ ms }; }
	Ω toMs( Duration d )ι->uint32{ return (uint32)std::max<int64_t>( std::chrono::round<milliseconds>(d).count(), 0 ); }
	//A dateTime cell:  a time from a statement of the caller's, epoch seconds from a generated select on mysql and sqlite.
	Ω timeCell( const DB::Row& r, uint i )ι->optional<TimePoint>{
		if( r.IsNull(i) )
			return nullopt;
		return r[i].Type()==DB::EValue::Time ? r[i].get_time() : Chrono::Epoch()+std::chrono::seconds{ r[i].get_number<int64_t>() };
	}
	Ω machineRow( DB::Row&& r )->OeeMachineRow{
		let ideal = r.GetOpt<double>( 3 );
		let idle = r.GetOpt<uint32>( 5 );
		return {
			.Id = r.Get<uint32>( 0 ),
			.Group = r.Get<uint32>( 1 ),
			.Calendar = r.GetOpt<uint32>( 2 ),
			.IdealCycle = ideal ? Hist::ToDuration( *ideal ) : nullopt,
			.SmallStop = fromMs( r.Get<uint32>(4) ),
			.IdleTimeout = idle ? optional<Duration>{ fromMs(*idle) } : nullopt,
			.Width = fromMs( r.Get<uint32>(6) ),
			.Processed = timeCell( r, 7 ),
			.Name = r.TakeString( 8 ),
			.Deleted = !r.IsNull( 9 )
		};
	}
	Ω signalRow( DB::Row&& r )->OeeSignalRow{
		OeeSignalRow y{ .Id=r.Get<uint32>(0), .Machine=r.Get<uint32>(1), .Role=Oee::ToRole(r.GetString(2)), .Node=r.Get<uint32>(3), .Running={}, .RatePeriod=fromMs(r.Get<uint32>(5)) };
		try{
			y.Running = Oee::ParseRunning( r.IsNull(4) ? jvalue{} : jvalue{r.GetString(4)} );
		}
		catch( const std::exception& e ){
			WARN( "oee_machine_signals row {}'s running isn't readable:  {}", y.Id, e.what() );
		}
		return y;
	}
	Ω ticks( const DB::Row& r, uint i )ι->TimePoint{ return UADateTime{ UA_DateTime{r.Get<int64_t>(i)} }.Time(); }
}
namespace Jde::Opc{
	α Gateway::ToSignals( const vector<OeeSignalRow>& rows )ι->vector<Oee::Signal>{
		auto y = Reserve<Oee::Signal>( rows.size() );
		for( let& row : rows ){
			if( row.Role )
				y.push_back( {*row.Role, row.Node, row.Running, row.RatePeriod} );
		}
		return y;
	}
	α Gateway::SelectOeeMachines( OeeMachineFilter filter, SL sl )ι->HistRowsAwait<OeeMachineRow>{
		let view = GetViewPtr( OeeTable::Machines );
		DB::WhereClause where;
		if( !filter.IncludeDeleted )
			where.Add( view->GetColumnPtr("deleted"), nullptr );
		if( filter.Key )
			where.Add( view->GetColumnPtr(filter.Key->IsPK() ? "oee_machine_id" : "slug"), filter.Key->IsPK() ? DB::Value{filter.Key->PK()} : DB::Value{filter.Key->NK()} );
		return { select(OeeTable::Machines, {"oee_machine_id", "hist_group_id", "calendar_id", "ideal_cycle", "small_stop", "idle_timeout", "width", "processed", "name", "deleted"}, move(where), sl), machineRow, sl };
	}
	α Gateway::SelectOeeSignals( OeeSignalFilter filter, SL sl )ι->HistRowsAwait<OeeSignalRow>{
		let view = GetViewPtr( OeeTable::Signals );
		DB::WhereClause where;
		if( filter.Id )
			where.Add( view->GetColumnPtr("oee_machine_signal_id"), (uint)*filter.Id );
		if( filter.Machine )
			where.Add( view->GetColumnPtr("oee_machine_id"), (uint)*filter.Machine );
		return { select(OeeTable::Signals, {"oee_machine_signal_id", "oee_machine_id", "role", "hist_group_node_id", "running", "rate_period"}, move(where), sl), signalRow, sl };
	}
	α Gateway::OeeTicks( TimePoint t )ι->DB::Value{ return DB::Value{ (_int)UADateTime{t}.UA() }; }
	α Gateway::OeeSecond( TimePoint t )ι->TimePoint{ return std::chrono::floor<std::chrono::seconds>( t ); }

	α Gateway::OeeSql::InsertIntervals( OeeMachinePK machine, std::span<const Oee::Interval> rows )ε->DB::Sql{
		string sql = Ƒ( "insert into {}( oee_machine_id, start, ms, run, stop, unknown, total, good, reject ) values", name(OeeTable::Intervals) );
		vector<DB::Value> params; params.reserve( rows.size()*9 );
		for( let& row : rows ){
			sql += params.empty() ? " ( ?, ?, ?, ?, ?, ?, ?, ?, ? )" : ", ( ?, ?, ?, ?, ?, ?, ?, ?, ? )";
			//Milliseconds that sum to the width:  what rounding leaves over goes to the longest of the three.
			let width = toMs( row.Width );
			std::array<uint32,3> times{ toMs(row.Run), toMs(row.Stop), toMs(row.Unknown) };
			auto& longest = *std::ranges::max_element( times );
			let others = times[0]+times[1]+times[2]-longest;
			longest = width>others ? width-others : 0;
			params.emplace_back( (uint)machine );
			params.push_back( OeeTicks(row.Start) );
			for( let ms : {width, times[0], times[1], times[2]} )
				params.emplace_back( (uint)ms );
			for( let count : {row.Total, row.Good, row.Reject} )
				params.emplace_back( count );
		}
		return { move(sql), move(params) };
	}
	α Gateway::OeeSql::SetProcessed( OeeMachinePK machine, TimePoint processed )ε->DB::Sql{
		return { Ƒ("update {} set processed=? where oee_machine_id=?", name(OeeTable::Machines)), {DB::Value{DB::DBTimePoint{processed}}, DB::Value{(uint)machine}} };
	}
	α Gateway::OeeSql::InsertStop( OeeMachinePK machine, const Oee::Stop& stop )ε->DB::Sql{
		return { Ƒ("insert into {}( oee_machine_id, start, finish, code, unknown ) values( ?, ?, ?, ?, ? )", name(OeeTable::Stops)),
			{DB::Value{(uint)machine}, OeeTicks(stop.Start), stop.End ? OeeTicks(*stop.End) : DB::Value{}, stop.Code ? DB::Value{(_int)*stop.Code} : DB::Value{}, DB::Value{stop.Unknown}} };
	}
	α Gateway::OeeSql::UpdateStop( OeeMachinePK machine, const Oee::Stop& stop )ε->DB::Sql{
		return { Ƒ("update {} set finish=?, code=?, unknown=? where oee_machine_id=? and start=?", name(OeeTable::Stops)),
			{stop.End ? OeeTicks(*stop.End) : DB::Value{}, stop.Code ? DB::Value{(_int)*stop.Code} : DB::Value{}, DB::Value{stop.Unknown}, DB::Value{(uint)machine}, OeeTicks(stop.Start)} };
	}
	α Gateway::OeeSql::DeleteStop( OeeMachinePK machine, TimePoint start )ε->DB::Sql{
		return { Ƒ("delete from {} where oee_machine_id=? and start=?", name(OeeTable::Stops)), {DB::Value{(uint)machine}, OeeTicks(start)} };
	}
	α Gateway::OeeSql::ClearStopFinish( OeeMachinePK machine, TimePoint start )ε->DB::Sql{
		return { Ƒ("update {} set finish=null where oee_machine_id=? and start=?", name(OeeTable::Stops)), {DB::Value{(uint)machine}, OeeTicks(start)} };
	}
	α Gateway::OeeSql::DeleteStopsFrom( OeeMachinePK machine, TimePoint from )ε->DB::Sql{
		return { Ƒ("delete from {} where oee_machine_id=? and start>=?", name(OeeTable::Stops)), {DB::Value{(uint)machine}, OeeTicks(from)} };
	}
	α Gateway::OeeSql::DeleteIntervalsFrom( OeeMachinePK machine, TimePoint from )ε->DB::Sql{
		return { Ƒ("delete from {} where oee_machine_id=? and start>=?", name(OeeTable::Intervals)), {DB::Value{(uint)machine}, OeeTicks(from)} };
	}
	α Gateway::OeeSql::DeleteStopsBetween( OeeMachinePK machine, TimePoint from, TimePoint to )ε->DB::Sql{
		return { Ƒ("delete from {} where oee_machine_id=? and start>=? and start<?", name(OeeTable::Stops)), {DB::Value{(uint)machine}, OeeTicks(from), OeeTicks(to)} };
	}
	α Gateway::OeeSql::DeleteIntervalsBetween( OeeMachinePK machine, TimePoint from, TimePoint to )ε->DB::Sql{
		return { Ƒ("delete from {} where oee_machine_id=? and start>=? and start<?", name(OeeTable::Intervals)), {DB::Value{(uint)machine}, OeeTicks(from), OeeTicks(to)} };
	}
	α Gateway::OeeSql::SelectIntervalsOver( OeeMachinePK machine, Oee::Span span )ε->DB::Sql{ return intervalsOver( machine, span, "start, ms", " order by start" ); }
	α Gateway::OeeSql::SelectFirstIntervalOver( OeeMachinePK machine, Oee::Span span )ε->DB::Sql{ return intervalsOver( machine, span, "min(start)", {} ); }
	α Gateway::OeeSql::SelectStopOver( OeeMachinePK machine, TimePoint at )ε->DB::Sql{
		return { Ƒ("select start, finish, code, unknown, oee_stop_id from {} where oee_machine_id=? and start<? and ( finish is null or finish>? )", name(OeeTable::Stops)), {DB::Value{(uint)machine}, OeeTicks(at), OeeTicks(at)} };
	}
	α Gateway::OeeSql::SelectStopsOver( OeeMachinePK machine, TimePoint from, TimePoint to )ε->DB::Sql{
		return { Ƒ("select start, finish, code, unknown, oee_stop_id from {} where oee_machine_id=? and start<? and ( finish is null or finish>? ) order by start", name(OeeTable::Stops)), {DB::Value{(uint)machine}, OeeTicks(to), OeeTicks(from)} };
	}
	α Gateway::OeeSql::ToStop( const DB::Row& r )ι->Oee::Stop{
		return { .Start=ticks(r, 0), .End=r.IsNull(1) ? nullopt : optional<TimePoint>{ticks(r, 1)}, .Code=r.GetOpt<int64_t>(2), .Unknown=r.GetBit(3), .Id=r.Get<uint32>(4) };
	}
	α Gateway::OeeSql::ToTime( const DB::Row& r, uint i )ι->TimePoint{ return ticks( r, i ); }
	α Gateway::OeeSql::CountCodings( uint32 reason )ε->DB::Sql{
		return { Ƒ("select count(*) from {} where oee_reason_id=?", name(OeeTable::Codings)), {DB::Value{(uint)reason}} };
	}
	α Gateway::OeeSql::ReasonsCoded( int64_t code )ε->DB::Sql{
		return { Ƒ("select oee_reason_id, name, deleted from {} where code=?", name(OeeTable::Reasons)), {DB::Value{(_int)code}} };
	}
	α Gateway::OeeSql::ReasonId( const DB::Key& key )ε->DB::Sql{
		return { Ƒ("select oee_reason_id from {} where {}=?", name(OeeTable::Reasons), key.IsPK() ? "oee_reason_id" : "slug"), {DB::Value::FromKey(key)} };
	}
}