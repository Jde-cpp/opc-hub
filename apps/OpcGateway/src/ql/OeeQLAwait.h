#pragma once
#include <jde/ql/IQLAwaitExe.h>
#include "../oee/OeeRows.h"

namespace Jde::Opc::Gateway{
	//OEE's reads (docs/oee/spec.md *Reads*, *Figures*, *Lines*, *Authorization*), custom queries that name no `opc`, each for a
	//`machine`, an oee_machines id, or a `line`, an oee_lines id, one of the two.  A machine's needs Read on its group through
	//HistHost::Gate, as a read of its history does, and a line's on the group of every machine it reaches, one refused refusing
	//the read;  a machine or a line that isn't there is NotFound once the caller has passed the table's row.  The rows are read
	//by SQL over the range, the engine's unwritten rows after them, and libs/oee's formulas run over them under the calendar
	//client's snapshot, the reasons and the codings as they are now.  Times are ISO 8601, to UA's hundred nanoseconds, so a
	//stop's bounds go back into codeOee as they came;  durations are milliseconds.
	//oee( machine, start, end, by ){ start end shift planned run unplanned plannedStop small excluded unknown total good reject
	//outside ideal availability performance quality oee teep measured partial }:  a list, one bucket over the range or one per
	//`by` - shift, day, week, month, or an ISO 8601 duration, raised to the widest row in the range.  `shift` is a shift bucket's
	//occurrence name, `measured` the factors in oee, by name.  More than readLimit buckets, the hist block's, is refused, and so
	//is `by: shift` on a machine with no calendar, or one whose calendar the client doesn't hold.  A calendar `by` over a range
	//that must hold more than readLimit buckets is refused before any is built, and oeeTimeline over more than readLimit days of a
	//calendar.
	//oee( line, start, end, by, members ):  the line's buckets, cut by its own calendar, each its machines' combined
	//(Oee::Lines::Combine), a machine's planned time its own calendar's and a deleted machine, or a deleted member line's, left
	//out.  A duration `by` is raised to the widest row any machine has in the range.  Each bucket adds `machines`, the number
	//it is over, and `coverage{ performance quality }`, the number each factor is over, so `measured` is what every machine
	//measured;  `members: true` adds `members`, a bucket of the same shape per direct member over the same bounds, with
	//`machine{ id name }` or `line{ id name level }` set on each.
	//oeeStops( machine, start, end, uncoded, limit, continuation ){ continuation stops{ id start end ms small open code uncoded
	//reason{ id name category } codings{ id start end reason{…} comment userName time } machine{ id name } } }:  the stops
	//that overlap the range, whole, newest first, the unknown spans left out;  a line's over every machine it reaches.  `reason`
	//is the coding's that covers the whole stop, else its code's;  `uncoded` says some of a stop that isn't small has no reason,
	//and `uncoded: true` keeps those.  `limit` is readLimit at most, and `continuation` the page before's, good for the same
	//arguments.  An open stop runs to the engine's time, or with no engine to the machine's `processed`, where the rows end.
	//oeeLosses( machine, start, end ){ kind reason{ id name category } ms stops }:  the loss rows, longest first;  `kind` is
	//reason, uncoded or small.  A line's are its machines' summed, the reasons being the gateway's.
	//oeeTimeline( machine, start, end ){ machine{ id name } planned{ shift name day start end breaks } segments{ start end state
	//stop reason{…} small uncoded coding{ id comment userName } } }:  what the Gantt chart draws, a list of one, or for a line
	//one per machine it reaches, in member order;  refused above readLimit segments or planned occurrences in all.
	//oeeMachineStatus( machine ){ machine{ id name } enabled calendar calendarId processed engine why state }:  for the admin
	//page, and answered without OEE or the historian too.  `calendar` is none, found or missing;  `engine` is live, starting,
	//waiting, between engines, blocked, with `why`, or deleted;  `state` is the machine's, running, stopped or unknown.
	//oeeLineStatus( line ){ line{ id name level } enabled calendar calendarId machines{ … } }:  the same for a line, `calendar`
	//its own and `machines` an oeeMachineStatus per machine it reaches, past Read on each one's group's own row.
	//The engine's three tables take no generic select (spec *Reads*):  their meta's noSelect refuses one in TableQL::AuthorizeRead,
	//since the generic path knows nothing of the gate.
	struct OeeQLAwait final : QL::IQLTableAwaitExe{
		using base = QL::IQLTableAwaitExe;
		OeeQLAwait( QL::TableQL&& q, QL::Creds creds, SRCE )ι:base{ move(q), move(creds), sl }{}
		Ω IsApplicable( const QL::TableQL& q )ι->bool;
		static constexpr sv FiguresCommand{ "oee" }, StopsCommand{ "oeeStops" }, LossesCommand{ "oeeLosses" }, TimelineCommand{ "oeeTimeline" }, StatusCommand{ "oeeMachineStatus" }, LineStatusCommand{ "oeeLineStatus" };
		static constexpr array<sv,6> Tables{ FiguresCommand, StopsCommand, LossesCommand, TimelineCommand, StatusCommand, LineStatusCommand };//their sub-tables inherit.
	private:
		α Query()ι->TAwait<jvalue>::Task override;
	};

	//What the reads and codeOee share.
	namespace OeeQL{
		α Time( const QL::Input& input, sv name, SL sl )ε->TimePoint;//a required time argument, to UA's tick.
		α Iso( TimePoint t )ι->string;
		α ReasonJson( const Oee::Reason* reason )ι->jvalue;//{ id name category }, null for none.
		α CodingJson( const OeeCodingRow& coding, const Oee::Reason* reason )ι->jobject;//{ id start end reason{…} comment userName time }.
	}
}