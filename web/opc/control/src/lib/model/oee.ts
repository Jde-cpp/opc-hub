//OEE's reads as a machine's page holds them (#405, docs/oee/spec.md *Reads*, *Figures*, *The Gantt chart*; oee.jsonnet):  the
//buckets of `oee`, the stops, the loss rows and the timeline, with the period picker's ranges, the figures as the page says
//them and the coding dialog's calls.  A time the gateway answers goes back into codeOee as it came, to a hundred nanoseconds,
//so a stop's and a segment's bounds keep their text beside the Date the page draws with.

export type OeeReasonRef = { id:number; name:string; category:string };
export function toReasonRef( json:any ):OeeReasonRef|undefined{
	return json && json.id!=null ? { id: Number( json.id ), name: String( json.name ?? json.id ), category: String( json.category ?? '' ) } : undefined;
}
const optional = ( v:unknown ):number|undefined=>v==null ? undefined : Number( v );

//A bucket of `oee`:  times in milliseconds, counts within planned time, and the factors as fractions, none where one isn't measured.
export type OeeBucket = {
	start:Date; end:Date; shift?:string;
	planned:number; run:number; unplanned:number; plannedStop:number; small:number; excluded:number; unknown:number;
	total:number; good:number; reject:number; outside:number; ideal:number;
	availability?:number; performance?:number; quality?:number; oee?:number; teep?:number;
	measured:string[]; partial:boolean;
	machines?:number; coverage?:{performance:number; quality:number}; members?:OeeMemberBucket[];//a line's:  the machines it is over, how many each factor is, and with `members` a bucket per direct member
};
export type OeeNamed = { id:number; name:string };
//a direct member's bucket over the line's bounds:  the machine or the line it is
export type OeeMemberBucket = OeeBucket & { machine?:OeeNamed; line?:OeeNamed & {level:string} };
const toNamed = ( json:any ):OeeNamed|undefined=>json && json.id!=null ? { id: Number( json.id ), name: String( json.name ?? json.id ) } : undefined;
export function toOeeBucket( json:any ):OeeBucket{
	const n = ( name:string )=>Number( json[name] ?? 0 );
	return {
		start: new Date( json.start ), end: new Date( json.end ), shift: json.shift || undefined,
		planned: n( 'planned' ), run: n( 'run' ), unplanned: n( 'unplanned' ), plannedStop: n( 'plannedStop' ), small: n( 'small' ), excluded: n( 'excluded' ), unknown: n( 'unknown' ),
		total: n( 'total' ), good: n( 'good' ), reject: n( 'reject' ), outside: n( 'outside' ), ideal: n( 'ideal' ),
		availability: optional( json.availability ), performance: optional( json.performance ), quality: optional( json.quality ), oee: optional( json.oee ), teep: optional( json.teep ),
		measured: Array.isArray( json.measured ) ? json.measured.map( String ) : [], partial: json.partial===true,
		...(json.machines==null ? {} : {machines: Number( json.machines )}),
		...(json.coverage ? {coverage: {performance: Number( json.coverage.performance ?? 0 ), quality: Number( json.coverage.quality ?? 0 )}} : {}),
		...(Array.isArray( json.members ) ? {members: json.members.map( ( m:any ):OeeMemberBucket=>{
			const machine = toNamed( m.machine ), line = toNamed( m.line );
			return { ...toOeeBucket( m ), ...(machine ? {machine} : {}), ...(line ? {line: {...line, level: String( m.line.level ?? '' )}} : {}) };
		} )} : {})
	};
}

export type OeeCoding = { id:number; start:Date; end:Date; reason?:OeeReasonRef; comment?:string; userName:string; time?:Date };
export function toOeeCoding( json:any ):OeeCoding{
	return { id: Number( json.id ), start: new Date( json.start ), end: new Date( json.end ), reason: toReasonRef( json.reason ), comment: json.comment || undefined, userName: String( json.userName ?? '' ), time: json.time ? new Date( json.time ) : undefined };
}
//A stop of `oeeStops`:  `ms` an open one's so far, `reason` the coding's that covers it whole, else its code's, `machine` whose
//it is, in a line's read.
export type OeeStop = { machine?:OeeNamed; id:number; start:Date; startText:string; end?:Date; endText?:string; ms:number; small:boolean; open:boolean; code?:unknown; uncoded:boolean; reason?:OeeReasonRef; codings:OeeCoding[] };
export function toOeeStop( json:any ):OeeStop{
	return {
		id: Number( json.id ), start: new Date( json.start ), startText: String( json.start ), end: json.end ? new Date( json.end ) : undefined, endText: json.end || undefined,
		ms: Number( json.ms ?? 0 ), small: json.small===true, open: json.open===true, code: json.code ?? undefined, uncoded: json.uncoded===true,
		reason: toReasonRef( json.reason ), codings: ( <any[]>(json.codings ?? []) ).map( toOeeCoding ), ...(toNamed( json.machine ) ? {machine: toNamed( json.machine )} : {})
	};
}
export type OeeStopsPage = { stops:OeeStop[]; continuation?:string };
export function toOeeStopsPage( json:any ):OeeStopsPage{
	return { stops: ( <any[]>(json?.stops ?? []) ).map( toOeeStop ), continuation: json?.continuation || undefined };
}

//A loss row of `oeeLosses`, longest first:  a reason's time, the uncoded time or the small stops', with the stops in it.
export type OeeLoss = { kind:'reason'|'uncoded'|'small'; reason?:OeeReasonRef; ms:number; stops:number };
export function toOeeLoss( json:any ):OeeLoss{
	return { kind: json.kind=='uncoded' || json.kind=='small' ? json.kind : 'reason', reason: toReasonRef( json.reason ), ms: Number( json.ms ?? 0 ), stops: Number( json.stops ?? 0 ) };
}

//`oeeTimeline`, what the Gantt chart draws:  the calendar's occurrences over the range, their breaks to cut out, and the
//machine's time cut at every change, a stop cut again where its codings split it.
export type OeeSpan = { start:Date; end:Date };
export type OeeShift = OeeSpan & { name?:string };//a shift bucket's bounds and its occurrence's name
export type OeeOccurrence = OeeSpan & { name?:string; shift?:number; breaks:OeeSpan[] };
export type OeeState = 'running'|'stopped'|'unknown';
export type OeeSegment = OeeSpan & { startText:string; endText:string; state:OeeState; stop?:number; reason?:OeeReasonRef; small:boolean; uncoded:boolean; coding?:{id:number; comment?:string; userName?:string} };
export type OeeTimeline = { planned:OeeOccurrence[]; segments:OeeSegment[] };
const toSpan = ( json:any ):OeeSpan=>({ start: new Date( json.start ), end: new Date( json.end ) });
export function toOeeTimeline( json:any ):OeeTimeline{
	const row = Array.isArray( json ) ? json[0] : json;//a machine's read is a list of one
	return {
		planned: ( <any[]>(row?.planned ?? []) ).map( o=>({ ...toSpan( o ), name: o.name || undefined, shift: optional( o.shift ), breaks: ( <any[]>(o.breaks ?? []) ).map( toSpan ) }) ),
		segments: ( <any[]>(row?.segments ?? []) ).map( s=>({
			...toSpan( s ), startText: String( s.start ), endText: String( s.end ), state: s.state=='running' || s.state=='stopped' ? s.state : 'unknown', stop: optional( s.stop ),
			reason: toReasonRef( s.reason ), small: s.small===true, uncoded: s.uncoded===true,
			coding: s.coding && s.coding.id!=null ? { id: Number( s.coding.id ), comment: s.coding.comment || undefined, userName: s.coding.userName || undefined } : undefined
		}) )
	};
}
//a line's `oeeTimeline`:  one per machine it reaches, in member order
export type OeeMachineTimeline = OeeTimeline & { machine:OeeNamed };
export function toOeeTimelines( json:any ):OeeMachineTimeline[]{
	return ( <any[]>(Array.isArray( json ) ? json : []) ).map( row=>({ ...toOeeTimeline( row ), machine: toNamed( row.machine ) ?? {id: 0, name: ''} }) );
}

//The picker's periods (spec *Web*).  A day begins at the browser's midnight and a week on its Monday;  the shift is the
//calendar's occurrence the page read, so a machine with no calendar has none.
export const oeePeriods = [
	{ id: 'shift', label: 'This shift' }, { id: 'today', label: 'Today' }, { id: 'yesterday', label: 'Yesterday' },
	{ id: 'week', label: 'This week' }, { id: 'lastWeek', label: 'Last week' }, { id: 'custom', label: 'Custom' }
] as const;
export type OeePeriod = typeof oeePeriods[number]['id'];
//`period` at `now`:  an open one ends now.  None for a shift with no occurrence, and for a custom range that is no range.
export function oeePeriodRange( period:OeePeriod, now:Date, given:{shift?:OeeSpan; from?:Date; to?:Date}={} ):OeeSpan|undefined{
	const day = ( offset:number )=>new Date( now.getFullYear(), now.getMonth(), now.getDate()+offset );//the Date's own carry, so a DST day is 23 or 25 hours
	const monday = -( (now.getDay()+6)%7 );
	switch( period ){
		case 'shift': return given.shift && given.shift.start<now ? { start: given.shift.start, end: given.shift.end<now ? given.shift.end : now } : undefined;
		case 'today': return { start: day( 0 ), end: now };
		case 'yesterday': return { start: day( -1 ), end: day( 0 ) };
		case 'week': return { start: day( monday ), end: now };
		case 'lastWeek': return { start: day( monday-7 ), end: day( monday ) };
		case 'custom':{
			const start = given.from, to = given.to ?? now, end = to<now ? to : now;//clipped first, so a From past now is refused
			return start && isFinite( start.getTime() ) && isFinite( to.getTime() ) && start<end ? { start, end } : undefined;
		}
	}
}
//The newer half of a range the gateway refused as more than it reads at once (`oeeTimeline` above readLimit segments), for the
//next try;  none under a minute.
export function narrowOeeRange( range:OeeSpan ):OeeSpan|undefined{
	const half = ( range.end.getTime()-range.start.getTime() )/2;
	return half>=60000 ? { start: new Date( range.end.getTime()-half ), end: range.end } : undefined;
}
//the refusals a narrower range answers:  each names the setting
export function isOeeRangeRefusal( message:string ):boolean{ return /readLimit/.test( message ); }

//a factor as the page says it, to a tenth of a percent;  a dash where it isn't measured
export function oeePercent( v:number|undefined ):string{ return v==undefined || !isFinite( v ) ? '–' : `${( v*100 ).toFixed( 1 )}%`; }
//a length of time in its two largest units:  2d 3h, 6h 30m, 4m 12s, 8s
export function oeeDuration( ms:number ):string{
	const s = Math.round( Math.max( 0, ms )/1000 ), units:[number,string][] = [[86400, 'd'], [3600, 'h'], [60, 'm'], [1, 's']];
	const first = units.findIndex( ([size])=>s>=size );
	if( first<0 )
		return '0s';
	const [size, unit] = units[first], rest = s%size, next = units[first+1];
	const minor = next ? Math.floor( rest/next[0] ) : 0;
	return `${Math.floor( s/size )}${unit}${minor ? ` ${minor}${next![1]}` : ''}`;
}
//a count as it reads:  whole where it is, a Rate's integral to a tenth
export function oeeCount( v:number ):string{ return ( Number.isInteger( v ) ? v : Number( v.toFixed( 1 ) ) ).toLocaleString(); }

//The period's one bucket as the figures panel lists it:  the planned time split into what ran and each loss, and beside it the
//time that is in no factor.  `share` is of the planned time.
export type OeeSplitRow = { id:string; label:string; ms:number; share?:number; hint:string };
export function oeeLossSplit( b:OeeBucket ):{planned:OeeSplitRow[]; beside:OeeSplitRow[]}{
	const row = ( id:string, label:string, ms:number, hint:string ):OeeSplitRow=>({ id, label, ms, share: b.planned>0 ? ms/b.planned : undefined, hint });
	return {
		planned: [
			row( 'run', 'Run', b.run, 'Planned time less the unplanned and the planned stops:  the small stops are in it.' ),
			row( 'unplanned', 'Unplanned stops', b.unplanned, 'Stops no shorter than the small-stop threshold, coded Unplanned or uncoded.' ),
			row( 'plannedStop', 'Planned stops', b.plannedStop, 'Stops coded Planned.' )
		],
		beside: [
			{ id: 'small', label: 'Small stops', ms: b.small, share: b.run>0 ? b.small/b.run : undefined, hint: 'Stops shorter than the threshold:  run time that made nothing, a performance loss.' },
			{ id: 'excluded', label: 'Excluded', ms: b.excluded, hint: 'Stops coded Excluded, taken out of planned time.' },
			{ id: 'unknown', label: 'Unknown', ms: b.unknown, hint: 'Time the feed was down or Bad, taken out of planned time.' }
		]
	};
}

//The coding dialog's calls (spec *Reasons and codings*):  one coding over the stop, or with a split time two, the stop's start to
//the split and the split to its end.  An open stop is coded to `now`.  The stop's own bounds go as the gateway gave them.
export type OeeCodingPart = { reason:number; comment?:string };
export type OeeCodingCall = { start:string; end:string; reason:number; comment?:string };
export type OeeCodingTarget = { start:Date; startText:string; end?:Date; endText?:string };
export function oeeCodingCalls( stop:OeeCodingTarget, first:OeeCodingPart, split?:{at:Date; second:OeeCodingPart}, now=new Date() ):OeeCodingCall[]{
	const end = stop.endText ?? now.toISOString();
	const call = ( start:string, to:string, part:OeeCodingPart ):OeeCodingCall=>({ start, end: to, reason: part.reason, ...(part.comment?.trim() ? {comment: part.comment.trim()} : {}) });
	if( !split )
		return [call( stop.startText, end, first )];
	const at = split.at.toISOString();
	return [call( stop.startText, at, first ), call( at, end, split.second )];
}
//why a split time can't be used, or nothing:  it falls inside the stop
export function oeeSplitProblem( stop:OeeCodingTarget, at:Date|undefined, now=new Date() ):string|undefined{
	if( !at )
		return 'Enter the split time.';
	return at<=stop.start || at>=( stop.end ?? now ) ? 'The split time is inside the stop:  after its start and before its end.' : undefined;
}