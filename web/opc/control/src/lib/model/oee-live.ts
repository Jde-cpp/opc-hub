import type * as FromServer from 'jde-proto/Opc.FromServer';
import type { OeeReasonRef, OeeSpan, OeeState } from './oee';

//A machine's live OEE (#406, docs/oee/spec.md *Live*):  the OeeStatus an OeeSubscribe's ack and pushes carry, as
//Gateway.oeeSubscribe delivers it.  What the dashboard makes of them is oee-live-view.ts, which the gateway's service, and so
//the initial bundle, has no need of.

export type OeeLiveStop = { start?:Date; reason?:OeeReasonRef; small:boolean; code?:string };
//the shift's figures so far, or without a calendar the UTC day's:  `end` is how far they run, the engine's time
export type OeeLiveFigures = { start?:Date; end?:Date; availability?:number; performance?:number; quality?:number; oee?:number; partial:boolean };
//A line's state (#409):  the machines it reaches, by theirs.  A line's status has these, and no state, since, stop or last
//count of its own;  its `planned`, `shift` and figures are by its own calendar.
export type OeeLiveCounts = { running:number; stopped:number; unknown:number };
export type OeeLiveStatus = {
	state:OeeState; since?:Date; stop?:OeeLiveStop; planned:boolean;
	shift?:OeeSpan & { name?:string }; figures?:OeeLiveFigures; lastCount?:Date; calendarMissing:boolean;
	machines?:OeeLiveCounts;
};
export function toOeeLiveStatus( p:FromServer.OeeStatus|undefined ):OeeLiveStatus{
	const reason = p?.stop?.reason, shift = p?.shift, f = p?.figures;
	return {
		state: p?.state==1 ? 'running' : p?.state==2 ? 'stopped' : 'unknown', since: p?.since, planned: p?.planned===true,
		stop: p?.stop ? { start: p.stop.start, reason: reason ? {id: reason.id, name: reason.name || String( reason.id ), category: reason.category} : undefined, small: p.stop.small, code: p.stop.code==undefined ? undefined : String( p.stop.code ) } : undefined,
		shift: shift?.start && shift.end ? { start: shift.start, end: shift.end, name: shift.name || undefined } : undefined,
		figures: f ? { start: f.start, end: f.end, availability: f.availability, performance: f.performance, quality: f.quality, oee: f.oee, partial: f.partial } : undefined,
		lastCount: p?.lastCount, calendarMissing: p?.calendarMissing===true,
		...(p?.machines ? {machines: {running: p.machines.running, stopped: p.machines.stopped, unknown: p.machines.unknown}} : {})
	};
}
//What a subscription delivers:  the status at its ack and each one pushed after, and `dropped` when the socket went, once
//until the next status - the gateway subscribes again, and the ack's status follows.
export type OeeLiveEvent = { kind:'status'; status:OeeLiveStatus } | { kind:'dropped' };