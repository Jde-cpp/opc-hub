import { Service } from '@angular/core';
import { OeeBucket, OeeCoding, OeeCodingCall, OeeLoss, OeeMachineTimeline, OeeReasonRef, OeeShift, OeeSpan, OeeStop, OeeStopsPage, OeeTimeline, toOeeBucket, toOeeCoding, toOeeLoss, toOeeStopsPage, toOeeTimeline, toOeeTimelines, toReasonRef } from '../model/oee';
import { OeePlant, toOeeLineMember, toOeeLineRef } from '../model/oee-lines';
import { HistPoster, HistReader } from './history-service';

const bucket = 'start end shift planned run unplanned plannedStop small excluded unknown total good reject outside ideal availability performance quality oee teep measured partial';
const reason = 'reason{ id name category }';
const span = ( range:OeeSpan )=>({ start: range.start.toISOString(), end: range.end.toISOString() });
//What a read is of:  a machine, by its id, or a line (#409), which each of the four reads takes in the machine's place.
export type OeeOf = number|{line:number};
type Arg = 'machine'|'line';
const argOf = ( of:OeeOf ):Arg=>typeof of=='number' ? 'machine' : 'line';
const idOf = ( of:OeeOf ):Record<string,number>=>typeof of=='number' ? {machine: of} : {line: of.line};

//OEE's reads and codings for a machine's page (#405, docs/oee/spec.md *Reads*, *Reasons and codings*; oee.jsonnet) and a
//line's (#409), each needing Read on the machine's group, a line's on the group of every machine it reaches.  A throw is the
//gateway's refusal:  a range above its readLimit, a right withheld.
@Service()
export class OeeService{
	//One bucket over the range, or one per `by`:  shift, day, week, month or an ISO 8601 duration.  A line's bucket says the
	//machines it is over and how many each factor is, and with `members` carries a bucket per direct member, named with its page's slug.
	async figures( reader:HistReader, of:OeeOf, range:OeeSpan, by?:string, members=false ):Promise<OeeBucket[]>{
		const data = await reader.query<any>( OeeService.figuresQL( argOf( of ), !!by, members ), {...idOf( of ), ...span( range ), ...(by ? {by} : {}), ...(members ? {members: true} : {})} );
		return ( <any[]>(data?.oee ?? []) ).map( toOeeBucket );
	}
	static figuresQL( arg:Arg='machine', by=false, members=false ):string{
		const line = arg=='line' ? ` machines coverage{ performance quality }${members ? ` members{ ${bucket} machines machine{ id name slug } line{ id name slug level } }` : ''}` : '';
		return `oee( ${arg}: $${arg}, start: $start, end: $end${by ? ', by: $by' : ''}${members ? ', members: $members' : ''} ){ ${bucket}${line} }`;
	}
	static readonly figuresQuery = OeeService.figuresQL();
	static readonly figuresByQuery = OeeService.figuresQL( 'machine', true );
	//the stops that overlap the range, newest first, a page of `limit`;  `continuation` asks for the page after.  A line's are
	//every machine's it reaches, each naming its machine.
	async stops( reader:HistReader, of:OeeOf, range:OeeSpan, args:{uncoded:boolean; limit:number; continuation?:string} ):Promise<OeeStopsPage>{
		const more = args.continuation ? ', continuation: $continuation' : '', arg = argOf( of );
		const data = await reader.query<any>( `oeeStops( ${arg}: $${arg}, start: $start, end: $end, uncoded: $uncoded, limit: $limit${more} ){ ${arg=='line' ? OeeService.lineStopsSelection : OeeService.stopsSelection} }`,
			{...idOf( of ), ...span( range ), uncoded: args.uncoded, limit: args.limit, ...(args.continuation ? {continuation: args.continuation} : {})} );
		return toOeeStopsPage( data?.oeeStops );
	}
	static readonly stopsSelection = `continuation stops{ id start end ms small open code uncoded ${reason} codings{ id start end ${reason} comment userName time } }`;
	static readonly lineStopsSelection = OeeService.stopsSelection.replace( / \}$/, ' machine{ id name } }' );
	async losses( reader:HistReader, of:OeeOf, range:OeeSpan ):Promise<OeeLoss[]>{
		const arg = argOf( of );
		const data = await reader.query<any>( `oeeLosses( ${arg}: $${arg}, start: $start, end: $end ){ kind ${reason} ms stops }`, {...idOf( of ), ...span( range )} );
		return ( <any[]>(data?.oeeLosses ?? []) ).map( toOeeLoss );
	}
	async timeline( reader:HistReader, machine:number, range:OeeSpan ):Promise<OeeTimeline>{
		const data = await reader.query<any>( OeeService.timelineQuery, {machine, ...span( range )} );
		return toOeeTimeline( data?.oeeTimeline );
	}
	//a line's:  one per machine it reaches, in member order
	async timelines( reader:HistReader, line:number, range:OeeSpan ):Promise<OeeMachineTimeline[]>{
		const data = await reader.query<any>( OeeService.lineTimelineQuery, {line, ...span( range )} );
		return toOeeTimelines( data?.oeeTimeline );
	}
	static readonly timelineQuery = `oeeTimeline( machine: $machine, start: $start, end: $end ){ planned{ shift name start end breaks } segments{ start end state stop ${reason} small uncoded coding } }`;
	static readonly lineTimelineQuery = `oeeTimeline( line: $line, start: $start, end: $end ){ machine{ id name } planned{ shift name start end breaks } segments{ start end state stop ${reason} small uncoded coding } }`;
	//The calendar's occurrence at `now`, or the last before it:  the newest shift bucket begun in the day and a half behind it.
	//The read runs a day past now, so a running shift's bucket isn't cut at now and its end is the occurrence's.  None where the
	//machine has no shift there;  a machine with no calendar refuses the read.  A line's is its own calendar's.
	async shift( reader:HistReader, of:OeeOf, now:Date ):Promise<OeeShift|undefined>{
		const arg = argOf( of );
		const data = await reader.query<any>( `oee( ${arg}: $${arg}, start: $start, end: $end, by: $by ){ start end shift }`, {...idOf( of ), start: new Date( now.getTime()-36*3600000 ).toISOString(), end: new Date( now.getTime()+24*3600000 ).toISOString(), by: 'shift'} );
		const last = ( <any[]>(data?.oee ?? []) ).filter( b=>new Date( b.start )<=now ).at( -1 );
		return last ? { start: new Date( last.start ), end: new Date( last.end ), name: last.shift || undefined } : undefined;
	}
	//The gateway's live machines, by name, with its live lines and every membership (#406, #409):  what the dashboard orders its
	//tiles and rows by, and what a line's pages pick and link from.  A caller who may not read the lines or their members has
	//none of either, so every machine is one in no line;  `strict` throws instead, for a page that edits them.
	async plant( reader:HistReader, strict=false ):Promise<OeePlant>{
		const lines = Promise.all( [reader.query<any>( OeeService.linesQuery ), reader.query<any>( OeeService.lineMembersQuery )] ).then( ( [lines, members] )=>({
			lines: ( <any[]>(lines?.oeeLines ?? []) ).map( toOeeLineRef ), members: ( <any[]>(members?.oeeLineMembers ?? []) ).map( toOeeLineMember )
		}) );
		const [data, held] = await Promise.all( [reader.query<any>( OeeService.machinesQuery ), strict ? lines : lines.catch( e=>{
			console.warn( "Could not read the lines' members.", e );
			return { lines: [], members: [] };
		} )] );
		return { machines: ( <any[]>(data?.oeeMachines ?? []) ).map( m=>({ id: Number( m.id ), name: String( m.name ?? m.slug ), slug: String( m.slug ) }) ).sort( ( a, b )=>a.name.localeCompare( b.name ) ), ...held };
	}
	static readonly machinesQuery = 'oeeMachines( deleted: null ){ id name slug }';
	static readonly linesQuery = 'oeeLines( deleted: null ){ id name slug level }';
	static readonly lineMembersQuery = 'oeeLineMembers{ id oeeLineId oeeMachineId memberLineId sort }';
	//the reasons a stop is coded with:  the live ones
	async reasons( reader:HistReader ):Promise<OeeReasonRef[]>{
		const data = await reader.query<any>( OeeService.reasonsQuery );
		return ( <any[]>(data?.oeeReasons ?? []) ).map( r=>toReasonRef( r )! ).sort( ( a, b )=>a.name.localeCompare( b.name ) );
	}
	static readonly reasonsQuery = 'oeeReasons( deleted: null ){ id name category }';
	//The stop of `id` as the gateway has it now, read at a time within it:  its start, or where the period cuts it.  None where
	//the engine has since made its stops again.
	async stop( reader:HistReader, machine:number, id:number, at:Date ):Promise<OeeStop|undefined>{
		const page = await this.stops( reader, machine, {start: at, end: new Date( at.getTime()+1 )}, {uncoded: false, limit: 2} );//a stop ending where it starts is the other
		return page.stops.find( s=>s.id==id );
	}

	//A reason over a range of the machine's time, replacing the coding at the same start, which keeps its id.  The coding as
	//written;  needs Update on the codings.
	async code( poster:HistPoster, machine:number, call:OeeCodingCall ):Promise<OeeCoding|undefined>{
		const data = await poster.postQL<any>( OeeService.codeMutation, {machine, start: call.start, end: call.end, reason: call.reason, comment: call.comment ?? null} );
		const written = Array.isArray( data?.codeOee ) ? data.codeOee[0] : data?.codeOee;
		return written?.id==null ? undefined : toOeeCoding( written );
	}
	static readonly codeMutation = `mutation codeOee( machine: $machine, start: $start, end: $end, reason: $reason, comment: $comment ){ id start end ${reason} comment userName time }`;
	//a coding taken away.  Needs Delete on the codings.
	async uncode( poster:HistPoster, id:number ):Promise<void>{
		await poster.postQL<any>( OeeService.uncodeMutation, {id} );
	}
	static readonly uncodeMutation = 'mutation uncodeOee( id: $id ){ id }';
}