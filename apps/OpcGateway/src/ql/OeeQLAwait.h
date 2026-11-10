#pragma once
#include <jde/ql/IQLAwaitExe.h>
#include "../oee/OeeRows.h"

namespace Jde::Opc::Gateway{
	//OEE's reads (docs/oee/spec.md *Reads*, *Figures*, *Authorization*), custom queries that name no `opc`, each for a `machine`,
	//an oee_machines id, and each needing Read on the machine's group through HistHost::Gate, as a read of its history does;  a
	//machine that isn't there is NotFound once the caller has passed the table's row.  The rows are read by SQL over the range,
	//the engine's unwritten rows after them, and libs/oee's formulas run over them under the calendar client's snapshot, the
	//reasons and the codings as they are now.  Times are ISO 8601, to UA's hundred nanoseconds, so a stop's bounds go back into
	//codeOee as they came;  durations are milliseconds.
	//oee( machine, start, end, by ){ start end shift planned run unplanned plannedStop small excluded unknown total good reject
	//outside ideal availability performance quality oee teep measured partial }:  a list, one bucket over the range or one per
	//`by` - shift, day, week, month, or an ISO 8601 duration, raised to the widest row in the range.  `shift` is a shift bucket's
	//occurrence name, `measured` the factors in oee, by name.  More than readLimit buckets, the hist block's, is refused, and so
	//is `by: shift` on a machine with no calendar, or one whose calendar the client doesn't hold.
	//oeeStops( machine, start, end, uncoded, limit, continuation ){ continuation stops{ id start end ms small open code uncoded
	//reason{ id name category } codings{ id start end reason{…} comment userName time } machine{ id name } } }:  the stops
	//that overlap the range, whole, newest first, the unknown spans left out.  `reason` is the coding's that covers the whole
	//stop, else its code's;  `uncoded` says some of a stop that isn't small has no reason, and `uncoded: true` keeps those.
	//`limit` is readLimit at most, and `continuation` the page before's, good for the same arguments.
	//oeeLosses( machine, start, end ){ kind reason{ id name category } ms stops }:  the loss rows, longest first;  `kind` is
	//reason, uncoded or small.
	//oeeTimeline( machine, start, end ){ machine{ id name } planned{ shift name day start end breaks } segments{ start end state
	//stop reason{…} small uncoded coding{ id comment userName } } }:  what the Gantt chart draws, a list of one;  refused above
	//readLimit segments.
	//oeeMachineStatus( machine ){ machine{ id name } enabled calendar calendarId processed engine why state }:  for the admin
	//page, and answered without OEE or the historian too.  `calendar` is none, found or missing;  `engine` is live, starting,
	//waiting, between engines, blocked, with `why`, or deleted;  `state` is the machine's, running, stopped or unknown.
	//The engine's three tables take no generic select (spec *Reads*):  oeeIntervals and oeeCodings, either spelling, oeeStop, and
	//any of them nested in another query, are refused here, since the generic path knows nothing of the gate.
	struct OeeQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		OeeQLAwait( QL::TableQL&& q, QL::Creds creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool;
		static constexpr sv FiguresCommand{ "oee" }, StopsCommand{ "oeeStops" }, LossesCommand{ "oeeLosses" }, TimelineCommand{ "oeeTimeline" }, StatusCommand{ "oeeMachineStatus" };
		static constexpr array<sv,5> Tables{ FiguresCommand, StopsCommand, LossesCommand, TimelineCommand, StatusCommand };//their sub-tables inherit.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};

	//What the reads and codeOee share.
	namespace OeeQL{
		α Time( const QL::Input& input, sv name, SL sl )ε->TimePoint;//a required time argument, to UA's tick.
		α Iso( TimePoint t )ι->string;
		//`full` cut to what `q` names:  its columns, and its sub-tables, an object's or each of a list's.
		α Trim( const QL::TableQL& q, const jobject& full )ι->jobject;
		α ReasonJson( const Oee::Reason* reason )ι->jvalue;//{ id name category }, null for none.
		α CodingJson( const OeeCodingRow& coding, const Oee::Reason* reason )ι->jobject;//{ id start end reason{…} comment userName time }.
	}
}