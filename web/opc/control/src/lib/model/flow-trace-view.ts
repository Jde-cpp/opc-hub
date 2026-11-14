import Long from 'long';
import { FlowGraph, FlowVersion } from './flow';
import { FlowCountRow, FlowNodeCounts, FlowTraceRecord, flowWireKey, noFlowCounts } from './flow-trace';

//What History makes of the trace's records (#456, docs/flows/spec.md *History*):  the version in force at a time, the counts
//onto the wires, a message's path from its lineage, and Play's stepping of a range's records on a clock of its own.  Apart
//from flow-trace.ts, whose readers the gateway's service needs, so none of this is in the initial bundle.

export function addFlowCounts( a:FlowNodeCounts, b:FlowNodeCounts ):FlowNodeCounts{
	const sent = Array.from( {length: Math.max( a.sent.length, b.sent.length )}, ( _, i )=>(a.sent[i] ?? 0)+(b.sent[i] ?? 0) );
	return { received: a.received+b.received, sent, drops: a.drops+b.drops, errors: a.errors+b.errors };
}
//a record's json as a panel shows it:  pretty where it is a value, as it came where cut text
export function flowJsonText( v:unknown ):string{
	if( v===undefined )
		return '';
	if( typeof v=='string' )
		return v;
	return JSON.stringify( v, ( _k, x )=>Long.isLong( x ) ? x.toString() : x, '\t' ) ?? '';
}
//what a row says of a message in a word or two:  its payload, else its topic, else nothing
export function flowRecordSummary( r:Pick<FlowTraceRecord,'json'|'error'|'status'|'kind'> ):string{
	if( r.error )
		return r.error;
	if( r.kind=='status' )
		return r.status?.text ?? '';
	const json = r.json;
	if( json==null )
		return '';
	if( typeof json=='string' )
		return json;
	const o = <Record<string,unknown>>json;
	const payload = typeof o=='object' && 'payload' in o ? o['payload'] : json;
	const text = flowJsonText( payload ).replace( /\s+/g, ' ' );
	return text.length>80 ? `${text.substring( 0, 79 )}…` : text;
}

//A range's width as a count of buckets:  a minute at least, and the step from these that keeps a range within `most` buckets, so
//a day reads by the quarter hour and a month by the half day.
export const flowCountSteps = [ {by: 'PT1M', ms: 60_000}, {by: 'PT5M', ms: 300_000}, {by: 'PT15M', ms: 900_000}, {by: 'PT1H', ms: 3_600_000}, {by: 'PT6H', ms: 21_600_000}, {by: 'PT12H', ms: 43_200_000}, {by: 'P1D', ms: 86_400_000}, {by: 'P7D', ms: 604_800_000} ] as const;
export function flowCountStep( start:Date, end:Date, most=200 ):{ by:string; ms:number }{
	const width = Math.max( 0, end.getTime()-start.getTime() );
	return flowCountSteps.find( s=>width/s.ms<=most ) ?? flowCountSteps[flowCountSteps.length-1];
}
//The counts onto the graph:  each node's over the rows, and each wire's, its port's `sent` under the node, as the canvas labels
//them.  A wire is keyed as the live push names one, flowWireKey's, and the ports past a node's wires are nobody's.
export type FlowGraphCounts = { nodes:Map<string,FlowNodeCounts>; wires:Map<string,number> };
export function flowGraphCounts( rows:FlowCountRow[], graph:FlowGraph ):FlowGraphCounts{
	const nodes = new Map<string,FlowNodeCounts>();
	for( const row of rows )
		nodes.set( row.node, addFlowCounts( nodes.get( row.node ) ?? noFlowCounts(), row.counts ) );
	const wires = new Map<string,number>();
	for( const node of graph.nodes ){
		const sent = nodes.get( node.id )?.sent;
		if( !sent )
			continue;
		node.wires.forEach( ( targets, port )=>targets.forEach( to=>wires.set( flowWireKey( node.id, port, to ), sent[port] ?? 0 ) ) );
	}
	return { nodes, wires };
}

//The version in force at a time, the scrubber's mapping:  the last deployed at or before it, and the first deployed where none
//was, since nothing ran before the first deploy and the canvas draws something.  None with no versions.
export function flowVersionAt( versions:FlowVersion[], at:Date ):FlowVersion|undefined{
	const dated = versions.filter( v=>v.deployed ).sort( ( a, b )=>a.deployed!.getTime()-b.deployed!.getTime() );
	if( !dated.length )
		return [...versions].sort( ( a, b )=>a.version-b.version )[0];
	let y = dated[0];
	for( const v of dated ){
		if( v.deployed!.getTime()>at.getTime() )
			break;
		y = v;
	}
	return y;
}
//the deploys within a range, the scrubber's marks, each where it sits along it, 0 to 1
export function flowDeployMarks( versions:FlowVersion[], start:Date, end:Date ):{ version:FlowVersion; at:number }[]{
	const width = end.getTime()-start.getTime();
	return versions.filter( v=>v.deployed && v.deployed>=start && v.deployed<=end && width>0 ).map( v=>({ version: v, at: (v.deployed!.getTime()-start.getTime())/width }) ).sort( ( a, b )=>a.at-b.at );
}

//A message's path from its lineage, the records flowMessage answers in time order:  the message's own records, its ancestors'
//and its descendants'.  The nodes each passed, in order, and the wires between a record and the next of the same message, the
//port the one the node's wires take to the next, so the canvas lights the path.  `own` is the message selected, `kin` the rest.
export type FlowLineagePath = { own:{ nodes:string[]; wires:string[] }; kin:{ nodes:string[]; wires:string[] } };
export function flowLineagePath( records:FlowTraceRecord[], id:string, graph:FlowGraph ):FlowLineagePath{
	const byNode = new Map( graph.nodes.map( n=>[n.id, n] ) );
	const own = { nodes: new Set<string>(), wires: new Set<string>() }, kin = { nodes: new Set<string>(), wires: new Set<string>() };
	const last = new Map<string,FlowTraceRecord>();//each message's record before
	for( const r of records ){
		if( !r.msgId )
			continue;
		const path = r.msgId==id ? own : kin;
		path.nodes.add( r.node );
		//a clone's first record follows its parent's at the node it was made:  the wire is from there
		const before = last.get( r.msgId ) ?? ( r.kind=='clone' && r.parentId ? last.get( r.parentId ) : undefined );
		if( before && before.node!=r.node ){
			const port = byNode.get( before.node )?.wires.findIndex( targets=>targets.includes( r.node ) ) ?? -1;
			if( port>=0 )
				path.wires.add( flowWireKey( before.node, port, r.node ) );
		}
		last.set( r.msgId, r );
	}
	return { own: { nodes: [...own.nodes], wires: [...own.wires] }, kin: { nodes: [...kin.nodes].filter( n=>!own.nodes.has( n ) ), wires: [...kin.wires].filter( w=>!own.wires.has( w ) ) } };
}
//the records of the message itself, in order:  its path, with its json where a node changed it
export function flowOwnRecords( records:FlowTraceRecord[], id:string ):FlowTraceRecord[]{ return records.filter( r=>r.msgId==id ); }

//Play (spec *History*):  the range's records stepped through the canvas at a speed, the live animation driven from the files.
//The player stands at a time of the range, `at`, and moves it by the wall clock's step times the speed each `tick`, answering
//the records between where it stood and where it stands, in order, and whether the range is done.  The records come in pages,
//`add`ed as they are read, oldest first;  `starved` says the player has stepped to the end of what it holds while more is to
//come, so the tab reads the next page before it goes on.  A fake clock drives it in a spec:  nothing here reads Date.now.
export class FlowPlayer{
	constructor( readonly start:Date, readonly end:Date ){ this.at = start.getTime(); }
	at:number;//ms, the time the player stands at
	speed = 1;
	#records:FlowTraceRecord[] = [];
	#next = 0;//the first record not yet fired
	#complete = false;//every page is in
	#wall?:number;//the wall clock at the tick before, while playing
	playing = false;
	add( records:FlowTraceRecord[], complete:boolean ):void{
		this.#records.push( ...records );
		this.#complete = complete;
	}
	get held():number{ return this.#records.length-this.#next; }
	get complete():boolean{ return this.#complete; }
	get starved():boolean{ return this.playing && !this.#complete && this.#next>=this.#records.length; }
	get done():boolean{ return this.#complete && this.#next>=this.#records.length && this.at>=this.end.getTime(); }
	play( wall:number ):void{
		if( this.done )
			this.seek( this.start.getTime() );
		this.playing = true;
		this.#wall = wall;
	}
	pause():void{
		this.playing = false;
		this.#wall = undefined;
	}
	//the player put at a time:  what is before it is as fired, and the records from it on fire as it goes
	seek( ms:number ):void{
		this.at = Math.min( Math.max( ms, this.start.getTime() ), this.end.getTime() );
		this.#next = this.#records.findIndex( r=>r.time.getTime()>=this.at );
		if( this.#next<0 )
			this.#next = this.#records.length;
	}
	//A tick of the wall clock:  the player moves speed times as far, no further than the last record it holds while more are
	//to come, and answers the records it passed.
	tick( wall:number ):FlowTraceRecord[]{
		if( !this.playing || this.#wall===undefined )
			return [];
		let to = this.at+(wall-this.#wall)*this.speed;
		this.#wall = wall;
		if( !this.#complete && this.#records.length )
			to = Math.min( to, this.#records[this.#records.length-1].time.getTime() );
		else if( !this.#complete )
			to = this.at;
		to = Math.min( to, this.end.getTime() );
		const fired:FlowTraceRecord[] = [];
		while( this.#next<this.#records.length && this.#records[this.#next].time.getTime()<=to )
			fired.push( this.#records[this.#next++] );
		this.at = Math.max( to, this.at );
		if( this.done )
			this.playing = false;
		return fired;
	}
}