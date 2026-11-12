import { oeeDuration, oeePercent, OeeReasonRef, OeeSegment, OeeSpan, OeeState } from './oee';
import { OeeLiveEvent, OeeLiveStatus } from './oee-live';

//What the dashboard makes of a machine's live OEE (#406, docs/oee/spec.md *Web*, *The Gantt chart*):  a tile's model from the
//sequence of its statuses, and the Gantt chart's last band, which grows with each and gives way to a new one on a state change.

export type OeeMachineRef = { id:number; name:string; slug:string };
//A tile:  its machine, how its subscription stands, and the last status it brought.  `ended` is why the gateway ended it - the
//machine deleted, OEE stopping - after which nothing follows.
export type OeeTileLink = 'connecting'|'live'|'dropped'|'ended';
export type OeeTile = { machine:OeeMachineRef; link:OeeTileLink; status?:OeeLiveStatus; ended?:string };
export function oeeTile( machine:OeeMachineRef ):OeeTile{ return { machine, link: 'connecting' }; }
//The tile after an event of its subscription.  A drop keeps what the gateway last said, marked;  an end keeps nothing.
export function oeeTileNext( tile:OeeTile, event:OeeLiveEvent|{kind:'ended'; message:string} ):OeeTile{
	if( tile.link=='ended' )
		return tile;
	switch( event.kind ){
		case 'status': return { machine: tile.machine, link: 'live', status: event.status };
		case 'dropped': return { ...tile, link: 'dropped' };
		case 'ended': return { machine: tile.machine, link: 'ended', ended: event.message };
	}
}
//A tile as it reads:  the state and since when, for how long by the engine's time, the stop's reason or why it has none, and
//the shift's four figures, dashes where one isn't measured or the machine is between shifts.
export type OeeTileView = {
	state:OeeState|'none'; stateLabel:string; since?:Date; length?:string;
	reason?:OeeReasonRef; badge?:'Uncoded'|'Small stop';
	period?:string; figures:{label:string; value:string}[]; partial:boolean; notes:string[];
};
export function oeeTileView( tile:OeeTile ):OeeTileView{
	const s = tile.status, f = s?.figures;
	const figures = [['Availability', f?.availability], ['Performance', f?.performance], ['Quality', f?.quality], ['OEE', f?.oee]].map( ( [label, v] )=>({ label: <string>label, value: oeePercent( <number|undefined>v ) }) );
	if( !s )
		return { state: 'none', stateLabel: tile.link=='ended' ? 'Ended' : tile.link=='dropped' ? 'Disconnected' : 'Connecting', figures, partial: false, notes: tile.ended ? [tile.ended] : [] };
	const stopped = s.state=='stopped', at = f?.end, between = oeeBetweenShifts( s );
	const notes:string[] = [];
	if( tile.link=='dropped' )
		notes.push( 'The connection to the gateway was lost:  this is the last it said.' );
	if( between )
		notes.push( 'Between shifts.' );
	else if( !s.planned )
		notes.push( 'Outside planned time.' );
	if( s.calendarMissing )
		notes.push( "Its calendar isn't found, so the figures are the UTC day's." );
	return {
		state: s.state, stateLabel: s.state=='running' ? 'Running' : stopped ? 'Stopped' : 'Unknown',
		since: s.since, length: s.since && at && at>s.since ? oeeDuration( at.getTime()-s.since.getTime() ) : undefined,
		reason: stopped ? s.stop?.reason : undefined, badge: !stopped || s.stop?.reason ? undefined : s.stop?.small ? 'Small stop' : 'Uncoded',
		period: s.shift ? s.shift.name ?? 'Shift' : between ? undefined : 'UTC day', figures, partial: f?.partial===true, notes
	};
}
//Between shifts:  a calendar found, and none of its shifts now - without a calendar every time is planned (SetOeeShift).  A
//status with no figures is no sign of it:  the gateway has none before it has read the machine's rows, for a machine no engine
//runs that has no rows yet, at the first instant of a shift or a UTC day, or where they couldn't be computed.
export function oeeBetweenShifts( status:OeeLiveStatus ):boolean{ return !status.shift && !status.planned; }

const dayMs = 86400000;
//What the dashboard's Gantt chart covers for a machine:  its calendar's occurrence now, or without one the UTC day the
//figures are over, or with none `now`'s.  None between shifts.
export function oeeLiveRange( status:OeeLiveStatus, now:Date ):OeeSpan|undefined{
	if( status.shift )
		return { start: status.shift.start, end: status.shift.end };
	if( oeeBetweenShifts( status ) )
		return undefined;
	const start = status.figures?.start ?? new Date( Math.floor( now.getTime()/dayMs )*dayMs );
	return { start, end: new Date( start.getTime()+dayMs ) };
}
//what a status's band is:  its state, and a stop's reason, small, or uncoded
export function oeeLiveMark( status:OeeLiveStatus ):Pick<OeeSegment,'state'|'reason'|'small'|'uncoded'>{
	const stopped = status.state=='stopped', reason = stopped ? status.stop?.reason : undefined, small = stopped && status.stop?.small===true;
	return { state: status.state, reason, small, uncoded: stopped && !small && !reason };
}
//the band a status is, from `start` to `end`.  Its stop's id isn't the feed's to say.
export function oeeLiveSegment( status:OeeLiveStatus, start:Date, end:Date ):OeeSegment{
	return { start, end, startText: start.toISOString(), endText: end.toISOString(), ...oeeLiveMark( status ) };
}
//A machine's bands after a status (spec *The Gantt chart*):  the last grows to the engine's time - `now` where the status has
//no figures - while the state is the one it is, and a new one opens where the state began:  the band before ends there, and
//what was drawn past it is cut.
//A stop's last band takes the status's reason and `small` when it is the stop whole, as the chart has it begin;  a piece a
//coding cut keeps its own.  The whole stop's coding, read with it, stays only while its reason is the status's.
//`from` is where the chart begins:  no band starts before it.
export function growOeeBands( segments:OeeSegment[], status:OeeLiveStatus, now:Date, from?:Date ):OeeSegment[]{
	const last = segments.at( -1 ), at = status.figures?.end ?? now, since = status.since;
	if( last && last.state==status.state && ( !since || since<=last.end ) ){
		const end = at>last.end ? at : last.end, stop = status.stop;
		const begun = stop?.start && from && stop.start<from ? from : stop?.start;//a stop begun before the chart is drawn from its start
		const whole = status.state=='stopped' && !!stop && ( !begun || last.start<=begun );
		let grown:OeeSegment;
		if( whole ){
			const { coding, ...band } = last, mark = oeeLiveMark( status );
			grown = { ...band, ...mark, end, endText: end.toISOString(), ...(coding && mark.reason?.id===last.reason?.id ? {coding} : {}) };
		}
		else
			grown = { ...last, end, endText: end.toISOString(), ...(stop ? {small: stop.small, uncoded: last.uncoded && !stop.small} : {}) };
		return [...segments.slice( 0, -1 ), grown];
	}
	let start = since ?? last?.end ?? at;
	if( from && start<from )
		start = from;
	const kept = segments.filter( s=>s.start<start ).map( ( s, i, all )=>s.end>start || i==all.length-1 ? {...s, end: start, endText: start.toISOString()} : s );//the one before ran until this began
	const end = at>start ? at : start, before = kept.at( -1 );
	if( before && before.state==status.state && status.state!='stopped' )//the state it was in, begun again:  one band
		return [...kept.slice( 0, -1 ), {...before, end, endText: end.toISOString()}];
	return [...kept, oeeLiveSegment( status, start, end )];
}