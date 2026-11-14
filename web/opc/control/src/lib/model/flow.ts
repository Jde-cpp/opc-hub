import { ISlugRow, Mutation, MutationType, SlugRow, SlugRowProps } from 'jde-framework';

//The gateway's flows as the pages hold them (#455, docs/flows/spec.md *Flows*, *Context*, *Replay*):  a flow's row, saved as a
//group's is, its graph, Node-RED's shape, which a deploy stores as a version, and the rows the host writes - versions, context
//values and replays - read through the generic queries.

//A node of the graph:  `wires` has one list per output port of the ids the port feeds.
export interface FlowNode{ id:string; type:string; name:string; x:number; y:number; props:Record<string,unknown>; wires:string[][]; }
export interface FlowGraph{ nodes:FlowNode[]; }

export const flowTraceLevels = ['off', 'counts', 'errors', 'hops'] as const;

//`enabled` is not the form's:  Enable and Disable change it at once (FlowService.setEnabled), since a change starts or stops the
//flow.  `trace` blank takes the gateway's level.  `version` is deployFlow's alone, none until the first deploy.
export class Flow extends SlugRow<Flow>{
	constructor( obj:FlowProps ){
		super( Flow.typeName, <SlugRowProps>obj );
		this.guid = obj.guid ?? undefined;
		this.enabled = obj.enabled ?? true;
		this.trace = obj.trace || undefined;
		this.version = obj.version==null ? undefined : Number( obj.version );
	}
	override equals( row:ISlugRow ):boolean{ return super.equals( row ) && (this.trace ?? '')==((<Flow>row).trace ?? ''); }
	override mutation( original:Flow ):Mutation[]{
		console.assert( this.canSave );
		const args:Record<string,unknown> = super.mutationArgs( original );
		if( (this.trace ?? '')!=(original?.trace ?? '') )
			args['trace'] = this.trace ?? null;
		return Object.keys( args ).length ? [new Mutation( this.type, this.id, args, original?.id ? MutationType.Update : MutationType.Create )] : [];
	}
	get properties():Flow{ return new Flow( this ); }
	guid?:string;
	enabled:boolean;
	trace?:string;
	version?:number;
	static typeName = 'Flow';
}
export type FlowProps = Partial<SlugRowProps> & { guid?:string|null; enabled?:boolean|null; trace?:string|null; version?:number|string|null };

//What a deploy refuses by, or why an enabled flow did not start:  the node the editor marks, none for a fault of the graph's own.
export type FlowFault = { node?:string; message:string };
//A refusal as the gateway words it (FlowHost), `Node '<id>':  <why>`, behind the http status a thrown answer's text leads with.
export function toFlowFault( text:string ):FlowFault{
	const message = text.replace( /^\(\d+\)\s*/, '' );
	const named = /^Node '((?:[^'\\]|\\.)*)':\s+([\s\S]*)$/.exec( message );
	return named ? { node: named[1], message: named[2] } : { message };
}
//a fault as a page says it:  its node by its name, or by its type where it has none
export function flowFaultText( fault:FlowFault, graph?:FlowGraph ):string{
	const node = fault.node ? graph?.nodes.find( n=>n.id==fault.node ) : undefined;
	const where = !fault.node ? '' : node?.name ? `Node '${node.name}':  ` : node ? `The ${node.type} node:  ` : `Node '${fault.node}':  `;
	return `${where}${fault.message}`;
}

//deployFlow's answer:  the version written, and whether the flow runs from it.
export type FlowDeployed = { version:number; deployed?:Date; userName:string; running:boolean; dropped:number; fault?:FlowFault };
export function toFlowDeployed( json:any ):FlowDeployed{
	return {
		version: Number( json?.version ?? 0 ), deployed: toDate( json?.deployed ), userName: String( json?.userName ?? '' ), running: !!json?.running, dropped: Number( json?.dropped ?? 0 ),
		fault: json?.fault ? { node: json.fault.node ?? undefined, message: String( json.fault.message ?? '' ) } : undefined
	};
}

//A deploy, as flowVersions answers it:  `graph` is the column's json text, read;  `unread` says why where it is not a graph.
export type FlowVersion = { id:number; version:number; graph:FlowGraph; unread?:string; deployed?:Date; identityId:number; userName:string; comment?:string };
export function toFlowVersion( json:any ):FlowVersion{
	let graph:FlowGraph = { nodes: [] }, unread:string|undefined;
	try{
		graph = readFlowGraph( typeof json.graph=='string' ? JSON.parse( json.graph ) : json.graph );
	}
	catch( e ){
		unread = e instanceof Error ? e.message : String( e );
	}
	return { id: Number( json.id ), version: Number( json.version ), graph, unread, deployed: toDate( json.deployed ), identityId: Number( json.identityId ?? 0 ), userName: String( json.userName ?? '' ), comment: json.comment ?? undefined };
}

//A context value, as flowContexts answers it:  the gateway's with no flow, the flow's own with no node, else a node's.  `value` is
//json text.
export type FlowContextValue = { id:number; flowId?:number; nodeId:string; name:string; value:string };
export type FlowContextScope = 'global'|'flow'|'node';
export function toFlowContextValue( json:any ):FlowContextValue{
	return { id: Number( json.id ), flowId: json.flowId==null ? undefined : Number( json.flowId ), nodeId: String( json.nodeId ?? '' ), name: String( json.name ?? '' ), value: String( json.value ?? '' ) };
}
export function flowContextScope( value:Pick<FlowContextValue,'flowId'|'nodeId'> ):FlowContextScope{ return value.flowId==undefined ? 'global' : value.nodeId ? 'node' : 'flow'; }

//A replay, as flowRuns and replayFlow answer it:  `status` none while it is under way, then completed, cancelled or `failed:  <why>`.
export type FlowRun = { id:number; version:number; start?:Date; finish?:Date; mode:string; started?:Date; finished?:Date; userName:string; status?:string };
export function toFlowRun( json:any ):FlowRun{
	return { id: Number( json.id ), version: Number( json.version ?? 0 ), start: toDate( json.start ), finish: toDate( json.finish ), mode: String( json.mode ?? 'dry' ), started: toDate( json.started ), finished: toDate( json.finished ), userName: String( json.userName ?? '' ), status: json.status ?? undefined };
}
function toDate( v:unknown ):Date|undefined{
	const d = v==null || v==='' ? undefined : new Date( <string>v );
	return d && !isNaN( d.getTime() ) ? d : undefined;
}

//A graph from json, each node whole:  no name is '', no place the origin, no props none and no wires none, as the gateway reads
//them (Graph::Read).  Throws for what is no graph;  what a deploy would refuse of one that is, is flowGraphFault's to say.
export function readFlowGraph( json:unknown ):FlowGraph{
	const nodes = (<any>json)?.nodes;
	if( !json || typeof json!='object' || !Array.isArray( nodes ) )
		throw new Error( 'A graph is an object with a `nodes` list.' );
	return { nodes: nodes.map( ( n:any, i:number )=>{
		if( !n || typeof n!='object' || Array.isArray( n ) )
			throw new Error( `Node ${i+1} of the graph is not an object.` );
		const wires = Array.isArray( n.wires ) ? n.wires.map( ( port:unknown )=>Array.isArray( port ) ? port.map( String ) : [] ) : [];
		const number = ( v:unknown )=>typeof v=='number' && Number.isFinite( v ) ? v : 0;
		return { id: n.id==null ? '' : String( n.id ), type: n.type==null ? '' : String( n.type ), name: typeof n.name=='string' ? n.name : '', x: number( n.x ), y: number( n.y ), props: n.props && typeof n.props=='object' && !Array.isArray( n.props ) ? {...n.props} : {}, wires };
	} ) };
}

//A flow as a file:  its graph, with the name and description it had.  Import reads this, or a bare graph.
export type FlowExport = { name?:string; slug?:string; description?:string; graph:FlowGraph };
export function exportFlow( flow:{ name?:string; slug?:string; description?:string }, graph:FlowGraph ):string{
	const y:FlowExport = { name: flow.name, slug: flow.slug, graph };
	if( flow.description )
		y.description = flow.description;
	return JSON.stringify( y, undefined, '\t' );
}
//The text of an export or of a graph, read.  Throws what is wrong with it.  An array is Node-RED's own export, a list of nodes
//with `z` and `wires`:  the importer of the spec's *Later* reads it in fromNodeRed.
export function importFlow( text:string ):FlowExport{
	let json:any;
	try{
		json = JSON.parse( text );
	}
	catch( e ){
		throw new Error( `It is not json:  ${e instanceof Error ? e.message : e}` );
	}
	if( Array.isArray( json ) )
		return fromNodeRed( json );
	if( json && typeof json=='object' && json.graph!==undefined ){
		const text = ( v:unknown )=>typeof v=='string' && v ? v : undefined;
		return { name: text( json.name ), slug: text( json.slug ), description: text( json.description ), graph: readFlowGraph( json.graph ) };
	}
	return { graph: readFlowGraph( json ) };
}
function fromNodeRed( _nodes:unknown[] ):FlowExport{
	throw new Error( 'It is a Node-RED export, a list of nodes, which is not imported yet:  import a flow exported from this editor, or a graph, { "nodes": [ … ] }.' );
}

//Two graphs by node, as Versions shows a deploy against another:  a node the second has alone, one the first has alone, and one in
//both that differs, with what of it does - its type, its name, each prop by its key and its wires.  A node that only moved is
//`moved`, apart from the changes.
export type FlowNodeChange = { id:string; name:string; type:string; kind:'added'|'removed'|'changed'|'moved'; what:string[] };
export function diffFlowGraphs( from:FlowGraph, to:FlowGraph ):FlowNodeChange[]{
	const was = new Map( from.nodes.map( n=>[n.id, n] ) ), now = new Map( to.nodes.map( n=>[n.id, n] ) );
	const label = ( id:string )=>{ const n = now.get( id ) ?? was.get( id ); return n?.name || n?.type || id; };
	const y:FlowNodeChange[] = [];
	for( const node of to.nodes ){
		const old = was.get( node.id );
		if( !old ){
			y.push( {id: node.id, name: node.name, type: node.type, kind: 'added', what: []} );
			continue;
		}
		const what:string[] = [];
		if( old.type!=node.type )
			what.push( `type, from ${old.type}` );
		if( old.name!=node.name )
			what.push( `name, from ${old.name ? `'${old.name}'` : 'none'}` );
		for( const key of [...new Set( [...Object.keys( old.props ), ...Object.keys( node.props )] )].sort() ){
			if( !sameJson( old.props[key], node.props[key] ) )
				what.push( key in node.props ? key in old.props ? key : `${key}, added` : `${key}, removed` );
		}
		const ports = Math.max( old.wires.length, node.wires.length );
		for( let port=0; port<ports; ++port ){
			const before = old.wires[port] ?? [], after = node.wires[port] ?? [];
			const added = after.filter( id=>!before.includes( id ) ), removed = before.filter( id=>!after.includes( id ) );
			if( added.length )
				what.push( `output ${port+1} wired to ${added.map( label ).join( ', ' )}` );
			if( removed.length )
				what.push( `output ${port+1} unwired from ${removed.map( label ).join( ', ' )}` );
		}
		if( what.length )
			y.push( {id: node.id, name: node.name, type: node.type, kind: 'changed', what} );
		else if( old.x!=node.x || old.y!=node.y )
			y.push( {id: node.id, name: node.name, type: node.type, kind: 'moved', what: []} );
	}
	for( const node of from.nodes ){
		if( !now.has( node.id ) )
			y.push( {id: node.id, name: node.name, type: node.type, kind: 'removed', what: []} );
	}
	return y;
}
//json values alike, whatever order their keys are written in
export function sameJson( a:unknown, b:unknown ):boolean{
	if( a===b )
		return true;
	if( !a || !b || typeof a!='object' || typeof b!='object' || Array.isArray( a )!=Array.isArray( b ) )
		return false;
	if( Array.isArray( a ) )
		return a.length==(<unknown[]>b).length && a.every( ( x, i )=>sameJson( x, (<unknown[]>b)[i] ) );
	const keys = Object.keys( a ), other = Object.keys( b );
	return keys.length==other.length && keys.every( k=>k in <object>b && sameJson( (<any>a)[k], (<any>b)[k] ) );
}
//graphs alike, their nodes' places among it:  whether the editor holds anything to deploy
export function sameFlowGraph( a:FlowGraph, b:FlowGraph ):boolean{ return sameJson( a, b ); }