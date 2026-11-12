//OEE's reads and system mutations (docs/oee/spec.md *Reads*, *Reasons and codings*, *The engine*, *Authorization*).  The
//mutations are registered by their whole names, and the reads are custom queries, so the schema introspects none of them
//itself.  Config-only types - no view behind them, so __type(name:...) is answered from here alone.
//oee( machine, start, end, by ){ … } (ql/OeeQLAwait.cpp):  a list of buckets, one over the range or one per `by`.
//oeeStops( machine, start, end, uncoded, limit, continuation ){ continuation stops{ … } }:  the stops over the range, newest first.
//oeeLosses( machine, start, end ){ kind reason{ … } ms stops }:  the loss rows, longest first.
//oeeTimeline( machine, start, end ){ machine{ … } planned{ … } segments{ … } }:  what the Gantt chart draws, a list of one.
//oeeMachineStatus( machine ){ … }:  the machine's engine and calendar, for the admin page.
//Each of the four takes `line`, an oee_lines id, in place of `machine` (spec *Lines*):  its machines' combined, a bucket
//adding `machines` and `coverage`, and with `members: true` a bucket per direct member.  oeeLineStatus( line ){ … } is the
//line's oeeMachineStatus.
//Each needs Read on the machine's group, a line's on the group of every machine it reaches.  Times are ISO 8601 to a hundred
//nanoseconds, durations milliseconds.
//recomputeOee( machine, start, end ){ machine start end } (ql/OeeMAwait.cpp):  the machine's rows over start to end made
//again from its group's history (oee/OeeMachineHost.cpp), for a caller with Update on oee_machines and Read on the group;
//answers the span queued, as the engine ran it, once it is queued, not done.
//codeOee( machine, start, end, reason, comment ){ … } and uncodeOee( id ){ id }:  an operator's reason over a range of the
//machine's time, and its removal, for a caller with Update, or Delete, on oee_codings and Read on the group.
local types = import 'scalars.libsonnet';
local String = types.String, UInt = types.UInt, Boolean = types.Boolean, DateTime = types.DateTime, Json = types.Json, Float = types.Float, Object = types.Object, NonNull = types.NonNull, List = types.List;
local recomputeOee = {
	fields: [
		{ name: "machine", type: NonNull(UInt) }, //the oee_machines row.
		{ name: "start", type: NonNull(DateTime) }, //the span's, as asked.
		{ name: "end", type: NonNull(DateTime) }
	]
};
local oee = {
	fields: [
		{ name: "start", type: NonNull(DateTime) },
		{ name: "end", type: NonNull(DateTime) },
		{ name: "shift", type: String }, //a shift bucket's occurrence name.
		{ name: "planned", type: NonNull(UInt) }, //the calendar's planned time less excluded and unknown.
		{ name: "run", type: NonNull(UInt) }, //planned less unplanned and plannedStop.
		{ name: "unplanned", type: NonNull(UInt) },
		{ name: "plannedStop", type: NonNull(UInt) },
		{ name: "small", type: NonNull(UInt) },
		{ name: "excluded", type: NonNull(UInt) },
		{ name: "unknown", type: NonNull(UInt) },
		{ name: "total", type: NonNull(Float) }, //counted within planned time, as good and reject are.
		{ name: "good", type: NonNull(Float) },
		{ name: "reject", type: NonNull(Float) },
		{ name: "outside", type: NonNull(Float) }, //the total counted outside planned time.
		{ name: "ideal", type: NonNull(UInt) }, //the total times the ideal cycle.
		{ name: "availability", type: Float }, //null where it isn't measured, as the four after it.
		{ name: "performance", type: Float },
		{ name: "quality", type: Float },
		{ name: "oee", type: Float },
		{ name: "teep", type: Float },
		{ name: "measured", type: List(NonNull(String)) }, //the factors in oee:  availability, performance, quality.
		{ name: "partial", type: NonNull(Boolean) }, //unknown time falls in it, or the rows don't cover it.
		//A line's.
		{ name: "machines", type: UInt }, //the machines the bucket is over.
		{ name: "coverage", type: Object("OeeCoverage") }, //how many of them each factor is over.
		{ name: "members", type: List(Object("OeeMember")) }, //with members: true, a bucket per direct member.
		//A member's bucket's:  the machine or the line it is.
		{ name: "machine", type: Object("OeeMachineRef") },
		{ name: "line", type: Object("OeeLineRef") }
	]
};
local oeeLineStatus = {
	fields: [
		{ name: "line", type: Object("OeeLineRef") },
		{ name: "enabled", type: NonNull(Boolean) }, //the gateway has an oee block and its historian.
		{ name: "calendar", type: NonNull(String) }, //none, found or missing:  the line's own.
		{ name: "calendarId", type: UInt },
		{ name: "deleted", type: NonNull(Boolean) },
		{ name: "machines", type: List(Object("OeeMachineStatus")) } //one per machine it reaches, in member order.
	]
};
local oeeStops = {
	fields: [
		{ name: "continuation", type: String }, //for the next page; null on the last.
		{ name: "stops", type: List(Object("OeeStopRead")) }
	]
};
local oeeLosses = {
	fields: [
		{ name: "kind", type: NonNull(String) }, //reason, uncoded or small.
		{ name: "reason", type: Object("OeeReasonRef") }, //kind reason's.
		{ name: "ms", type: NonNull(UInt) },
		{ name: "stops", type: NonNull(UInt) } //the stops with time in the row.
	]
};
local oeeTimeline = {
	fields: [
		{ name: "machine", type: Object("OeeMachineRef") },
		{ name: "planned", type: List(Object("OeeOccurrence")) },
		{ name: "segments", type: List(Object("OeeSegment")) }
	]
};
local oeeMachineStatus = {
	fields: [
		{ name: "machine", type: Object("OeeMachineRef") },
		{ name: "enabled", type: NonNull(Boolean) }, //the gateway has an oee block and its historian.
		{ name: "calendar", type: NonNull(String) }, //none, found or missing.
		{ name: "calendarId", type: UInt },
		{ name: "processed", type: DateTime }, //the end of the last row the engine wrote.
		{ name: "engine", type: NonNull(String) }, //live, starting, waiting, blocked or deleted.
		{ name: "why", type: String }, //blocked's.
		{ name: "state", type: String } //running, stopped or unknown;  null without an engine.
	]
};
local oeeCoding = {
	fields: [
		{ name: "id", type: NonNull(UInt) },
		{ name: "start", type: NonNull(DateTime) },
		{ name: "end", type: NonNull(DateTime) },
		{ name: "reason", type: Object("OeeReasonRef") },
		{ name: "comment", type: String },
		{ name: "userName", type: NonNull(String) }, //the operator's name as it was then.
		{ name: "time", type: NonNull(DateTime) } //when it was coded.
	]
};
{
	RecomputeOee: recomputeOee,
	recomputeOee: recomputeOee,
	Oee: oee,
	oee: oee,
	OeeStops: oeeStops,
	oeeStops: oeeStops,
	OeeLosses: oeeLosses,
	oeeLosses: oeeLosses,
	OeeTimeline: oeeTimeline,
	oeeTimeline: oeeTimeline,
	OeeMachineStatus: oeeMachineStatus,
	oeeMachineStatus: oeeMachineStatus,
	OeeLineStatus: oeeLineStatus,
	oeeLineStatus: oeeLineStatus,
	OeeMember: oee,
	OeeCoverage: { fields: [ { name: "performance", type: NonNull(UInt) }, { name: "quality", type: NonNull(UInt) } ] },
	OeeLineRef: { fields: [ { name: "id", type: NonNull(UInt) }, { name: "name", type: NonNull(String) }, { name: "slug", type: NonNull(String) }, { name: "level", type: String } ] },
	OeeCodingRead: oeeCoding,
	codeOee: oeeCoding,
	uncodeOee: { fields: [ { name: "id", type: NonNull(UInt) } ] },
	OeeMachineRef: { fields: [ { name: "id", type: NonNull(UInt) }, { name: "name", type: NonNull(String) }, { name: "slug", type: NonNull(String) } ] },
	OeeReasonRef: { fields: [ { name: "id", type: NonNull(UInt) }, { name: "name", type: NonNull(String) }, { name: "category", type: NonNull(String) } ] },
	OeeStopRead: {
		fields: [
			{ name: "id", type: NonNull(UInt) }, //the oee_stops row.
			{ name: "start", type: NonNull(DateTime) },
			{ name: "end", type: DateTime }, //null while it is open.
			{ name: "ms", type: NonNull(UInt) }, //an open stop's, so far.
			{ name: "small", type: NonNull(Boolean) },
			{ name: "open", type: NonNull(Boolean) },
			{ name: "code", type: Json }, //the Reason signal's.
			{ name: "uncoded", type: NonNull(Boolean) }, //it isn't small, and some of it has no reason.
			{ name: "reason", type: Object("OeeReasonRef") }, //the coding's that covers it whole, else its code's.
			{ name: "codings", type: List(Object("OeeCodingRead")) },
			{ name: "machine", type: Object("OeeMachineRef") }
		]
	},
	OeeOccurrence: {
		fields: [
			{ name: "shift", type: UInt }, //null for an open exception, and for a machine with no calendar.
			{ name: "name", type: String },
			{ name: "day", type: String },
			{ name: "start", type: NonNull(DateTime) },
			{ name: "end", type: NonNull(DateTime) },
			{ name: "breaks", type: List(Json) } //{ start end }.
		]
	},
	OeeSegment: {
		fields: [
			{ name: "start", type: NonNull(DateTime) },
			{ name: "end", type: NonNull(DateTime) },
			{ name: "state", type: NonNull(String) }, //running, stopped or unknown.
			{ name: "stop", type: UInt }, //the oee_stops row.
			{ name: "reason", type: Object("OeeReasonRef") }, //a stop's piece's:  its coding's, else its code's.
			{ name: "small", type: NonNull(Boolean) },
			{ name: "uncoded", type: NonNull(Boolean) },
			{ name: "coding", type: Json } //{ id comment userName }, the coding over the piece.
		]
	}
}