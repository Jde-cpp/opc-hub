#pragma once
#include <jde/oee/Engine.h>
#include "../hist/HistRows.h"

//OEE's tables (opcGateway-meta.jsonnet, docs/oee/spec.md *Machines*, *Stops*, *Intervals*) as rows:  the selects the host and
//OeeMAwait make of the admin tables, and the statements the engine's sink writes its own two with.
namespace Jde::Opc::Gateway{
	using OeeMachinePK = uint32;
	using OeeSignalPK = uint32;
	namespace OeeTable{ constexpr sv Machines{ "oee_machines" }, Signals{ "oee_machine_signals" }, Stops{ "oee_stops" }, Intervals{ "oee_intervals" }, Reasons{ "oee_reasons" }, Codings{ "oee_codings" }; }

	struct OeeMachineRow final{
		OeeMachinePK Id;
		HistGroupPK Group;
		optional<uint32> Calendar;
		optional<Duration> IdealCycle;
		Duration SmallStop;
		optional<Duration> IdleTimeout;
		Duration Width;
		optional<TimePoint> Processed;//none until its create's live side has seeded it.
		string Name;
		bool Deleted;
	};
	//Role is none for text ToRole doesn't know, and Running empty for json ParseRunning refuses:  a row the QL never wrote,
	//which the machine's validation then names.
	struct OeeSignalRow final{ OeeSignalPK Id; OeeMachinePK Machine; optional<Oee::ERole> Role; HistGroupNodePK Node; vector<Oee::StateValue> Running; Duration RatePeriod; };
	//The rows' signals, as the library takes them;  one with no role is left out.
	α ToSignals( const vector<OeeSignalRow>& rows )ι->vector<Oee::Signal>;

	//oee_machines:  by key, or all;  deleted rows only when asked.
	struct OeeMachineFilter final{ optional<DB::Key> Key; bool IncludeDeleted{}; };
	α SelectOeeMachines( OeeMachineFilter filter={}, SRCE )ι->HistRowsAwait<OeeMachineRow>;
	//oee_machine_signals:  by row, by machine, or all.
	struct OeeSignalFilter final{ optional<OeeSignalPK> Id; optional<OeeMachinePK> Machine; };
	α SelectOeeSignals( OeeSignalFilter filter={}, SRCE )ι->HistRowsAwait<OeeSignalRow>;

	//A time as the engine's tables hold it, UA ticks, and a machine's `processed`, a dateTime column, to the second.
	α OeeTicks( TimePoint t )ι->DB::Value;
	α OeeSecond( TimePoint t )ι->TimePoint;

	//What the sink writes, each for DS()->Execute.  The engine's rows carry no row id:  a stop is keyed by its machine and start.
	namespace OeeSql{
		constexpr uint IntervalsPerInsert{ 200 };//nine parameters a row, under SQL Server's 2100 a statement.
		α InsertIntervals( OeeMachinePK machine, std::span<const Oee::Interval> rows )ε->DB::Sql;//IntervalsPerInsert at most.
		α SetProcessed( OeeMachinePK machine, TimePoint processed )ε->DB::Sql;
		α InsertStop( OeeMachinePK machine, const Oee::Stop& stop )ε->DB::Sql;
		α UpdateStop( OeeMachinePK machine, const Oee::Stop& stop )ε->DB::Sql;//its finish, code and unknown, the row at its start.
		α DeleteStop( OeeMachinePK machine, TimePoint start )ε->DB::Sql;
		α ClearStopFinish( OeeMachinePK machine, TimePoint start )ε->DB::Sql;
		α DeleteStopsFrom( OeeMachinePK machine, TimePoint from )ε->DB::Sql;//start at or after.
		α DeleteIntervalsFrom( OeeMachinePK machine, TimePoint from )ε->DB::Sql;
		//A rebuild's (docs/oee/spec.md *Intervals*):  the rows that start in [from, to), and those a span touches.
		α DeleteStopsBetween( OeeMachinePK machine, TimePoint from, TimePoint to )ε->DB::Sql;
		α DeleteIntervalsBetween( OeeMachinePK machine, TimePoint from, TimePoint to )ε->DB::Sql;
		//The interval rows over `span`, by start:  start at or before its end and end after its start.  start, ms.
		α SelectIntervalsOver( OeeMachinePK machine, Oee::Span span )ε->DB::Sql;
		//The stop row over `at`:  its start before it and its finish null or after it.  start, finish, code, unknown, id.
		α SelectStopOver( OeeMachinePK machine, TimePoint at )ε->DB::Sql;
		//The stop rows over [from, to), by start:  start before `to` and finish null or after `from`.  SelectStopOver's columns.
		α SelectStopsOver( OeeMachinePK machine, TimePoint from, TimePoint to )ε->DB::Sql;
		α ToStop( const DB::Row& row )ι->Oee::Stop;//SelectStopOver's.
		α ToTime( const DB::Row& row, uint i )ι->TimePoint;//a cell of UA ticks.
		α CountCodings( uint32 reason )ε->DB::Sql;
		α ReasonsCoded( int64_t code )ε->DB::Sql;//oee_reason_id, name, deleted.
		α ReasonId( const DB::Key& key )ε->DB::Sql;
	}
}