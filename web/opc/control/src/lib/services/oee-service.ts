import { Service } from '@angular/core';
import { OeeBucket, OeeCodingCall, OeeLoss, OeeReasonRef, OeeSpan, OeeStopsPage, OeeTimeline, toOeeBucket, toOeeLoss, toOeeStopsPage, toOeeTimeline } from '../model/oee';
import { HistPoster, HistReader } from './history-service';

const bucket = 'start end shift planned run unplanned plannedStop small excluded unknown total good reject outside ideal availability performance quality oee teep measured partial';
const reason = 'reason{ id name category }';
const span = ( range:OeeSpan )=>({ start: range.start.toISOString(), end: range.end.toISOString() });

//OEE's reads and codings for a machine's page (#405, docs/oee/spec.md *Reads*, *Reasons and codings*; oee.jsonnet), each needing
//Read on the machine's group.  A throw is the gateway's refusal:  a range above its readLimit, a right withheld.
@Service()
export class OeeService{
	//one bucket over the range, or one per `by`:  shift, day, week, month or an ISO 8601 duration
	async figures( reader:HistReader, machine:number, range:OeeSpan, by?:string ):Promise<OeeBucket[]>{
		const data = await reader.query<any>( by ? OeeService.figuresByQuery : OeeService.figuresQuery, {machine, ...span( range ), ...(by ? {by} : {})} );
		return ( <any[]>(data?.oee ?? []) ).map( toOeeBucket );
	}
	static readonly figuresQuery = `oee( machine: $machine, start: $start, end: $end ){ ${bucket} }`;
	static readonly figuresByQuery = `oee( machine: $machine, start: $start, end: $end, by: $by ){ ${bucket} }`;
	//the stops that overlap the range, newest first, a page of `limit`;  `continuation` asks for the page after
	async stops( reader:HistReader, machine:number, range:OeeSpan, args:{uncoded:boolean; limit:number; continuation?:string} ):Promise<OeeStopsPage>{
		const more = args.continuation ? ', continuation: $continuation' : '';
		const data = await reader.query<any>( `oeeStops( machine: $machine, start: $start, end: $end, uncoded: $uncoded, limit: $limit${more} ){ ${OeeService.stopsSelection} }`,
			{machine, ...span( range ), uncoded: args.uncoded, limit: args.limit, ...(args.continuation ? {continuation: args.continuation} : {})} );
		return toOeeStopsPage( data?.oeeStops );
	}
	static readonly stopsSelection = `continuation stops{ id start end ms small open code uncoded ${reason} codings{ id start end ${reason} comment userName time } }`;
	async losses( reader:HistReader, machine:number, range:OeeSpan ):Promise<OeeLoss[]>{
		const data = await reader.query<any>( OeeService.lossesQuery, {machine, ...span( range )} );
		return ( <any[]>(data?.oeeLosses ?? []) ).map( toOeeLoss );
	}
	static readonly lossesQuery = `oeeLosses( machine: $machine, start: $start, end: $end ){ kind ${reason} ms stops }`;
	async timeline( reader:HistReader, machine:number, range:OeeSpan ):Promise<OeeTimeline>{
		const data = await reader.query<any>( OeeService.timelineQuery, {machine, ...span( range )} );
		return toOeeTimeline( data?.oeeTimeline );
	}
	static readonly timelineQuery = `oeeTimeline( machine: $machine, start: $start, end: $end ){ planned{ shift name start end breaks } segments{ start end state stop ${reason} small uncoded coding } }`;
	//The calendar's occurrence at `now`, or the last before it:  the newest shift bucket of the day and a half behind it.  None
	//where the machine has no shift there;  a machine with no calendar refuses the read.
	async shift( reader:HistReader, machine:number, now:Date ):Promise<OeeSpan|undefined>{
		const data = await reader.query<any>( OeeService.shiftQuery, {machine, start: new Date( now.getTime()-36*3600000 ).toISOString(), end: now.toISOString(), by: 'shift'} );
		const last = ( <any[]>(data?.oee ?? []) ).at( -1 );
		return last ? { start: new Date( last.start ), end: new Date( last.end ) } : undefined;
	}
	static readonly shiftQuery = 'oee( machine: $machine, start: $start, end: $end, by: $by ){ start end shift }';
	//the reasons a stop is coded with:  the live ones
	async reasons( reader:HistReader ):Promise<OeeReasonRef[]>{
		const data = await reader.query<any>( OeeService.reasonsQuery );
		return ( <any[]>(data?.oeeReasons ?? []) ).map( r=>({ id: Number( r.id ), name: String( r.name ?? r.id ), category: String( r.category ?? '' ) }) ).sort( ( a, b )=>a.name.localeCompare( b.name ) );
	}
	static readonly reasonsQuery = 'oeeReasons( deleted: null ){ id name category }';

	//A reason over a range of the machine's time, replacing the coding at the same start.  Needs Update on the codings.
	async code( poster:HistPoster, machine:number, call:OeeCodingCall ):Promise<void>{
		await poster.postQL<any>( OeeService.codeMutation, {machine, start: call.start, end: call.end, reason: call.reason, comment: call.comment ?? null} );
	}
	static readonly codeMutation = 'mutation codeOee( machine: $machine, start: $start, end: $end, reason: $reason, comment: $comment ){ id }';
	//a coding taken away.  Needs Delete on the codings.
	async uncode( poster:HistPoster, id:number ):Promise<void>{
		await poster.postQL<any>( OeeService.uncodeMutation, {id} );
	}
	static readonly uncodeMutation = 'mutation uncodeOee( id: $id ){ id }';
}