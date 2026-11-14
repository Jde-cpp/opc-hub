import { Service } from '@angular/core';
import { Flow, FlowContextValue, FlowDeployed, FlowGraph, FlowRun, FlowVersion, toFlowContextValue, toFlowDeployed, toFlowRun, toFlowVersion } from '../model/flow';
import { FlowCountRow, FlowRecordKind, FlowTraceRecord, toFlowCountRow, toFlowTraceRecord } from '../model/flow-trace';
import { HistPoster, HistReader } from './history-service';
import { HistAdminService } from './hist-admin-service';

//The trace's reads (#456, spec *History*), each of one trace:  the live one with no `run`, or a replay's by its flow_runs id.
//flowTrace:  the records in a range, oldest first from `start`, newest first back from `end` alone, narrowed to a node, a
//message or a kind, a page at a time by the continuation.  flowMessage:  a message's lineage, its own records, its ancestors'
//and its descendants', in time order.  flowCounts:  the minute figures summed into buckets of `by`, a page of whole buckets.
export type FlowTraceArgs = { flow:number; start?:Date; end?:Date; node?:string; msg?:string; kind?:FlowRecordKind; run?:number; limit?:number; continuation?:string };
export type FlowTracePage = { records:FlowTraceRecord[]; continuation?:string };
export type FlowMessageArgs = { flow:number; id:string; run?:number; start?:Date; end?:Date; limit?:number };
export type FlowLineage = { records:FlowTraceRecord[]; more:boolean };
export type FlowCountsArgs = { flow:number; start:Date; end:Date; by:string; run?:number; limit?:number; continuation?:string };
export type FlowCountsPage = { counts:FlowCountRow[]; continuation?:string };
const recordFields = 'time msgId parentId runId node kind ports json cut error status{ fill shape text } blocks';

//The flows' tables and mutations for the pages (#455, docs/flows/spec.md *Flows*, *Context*, *Replay*):  the generic queries over
//flow_versions, flow_contexts and flow_runs, and the edits, each by POST as every mutation goes.  A throw is the gateway's refusal;
//a deploy's names its node, `Node '<id>':  <why>` (toFlowFault).  A flow's own row is saved through DetailPage, as a group's is.
//The trace's three reads (#456) are here too, each given only the arguments it has, as the history read is.
@Service()
export class FlowService{
	async trace( reader:HistReader, args:FlowTraceArgs ):Promise<FlowTracePage>{
		const {params, vars} = FlowService.readArgs( args, ['flow', 'start', 'end', 'node', 'msg', 'kind', 'run', 'limit', 'continuation'] );
		const data = await reader.query<any>( `flowTrace( ${params} ){ continuation records{ ${recordFields} } }`, vars );
		return { records: ( <any[]>(data?.flowTrace?.records ?? []) ).map( toFlowTraceRecord ), continuation: data?.flowTrace?.continuation || undefined };
	}
	async message( reader:HistReader, args:FlowMessageArgs ):Promise<FlowLineage>{
		const {params, vars} = FlowService.readArgs( args, ['flow', 'id', 'run', 'start', 'end', 'limit'] );
		const data = await reader.query<any>( `flowMessage( ${params} ){ more records{ ${recordFields} } }`, vars );
		return { records: ( <any[]>(data?.flowMessage?.records ?? []) ).map( toFlowTraceRecord ), more: !!data?.flowMessage?.more };
	}
	async counts( reader:HistReader, args:FlowCountsArgs ):Promise<FlowCountsPage>{
		const {params, vars} = FlowService.readArgs( args, ['flow', 'start', 'end', 'by', 'run', 'limit', 'continuation'] );
		const data = await reader.query<any>( `flowCounts( ${params} ){ continuation counts{ time node received sent drops errors } }`, vars );
		return { counts: ( <any[]>(data?.flowCounts?.counts ?? []) ).map( toFlowCountRow ), continuation: data?.flowCounts?.continuation || undefined };
	}
	//the arguments given, in the read's order, a time as ISO text:  the gateway reads an absent start or end as an open end
	static readArgs( args:Record<string,unknown>, names:string[] ):{ params:string; vars:Record<string,unknown> }{
		const vars:Record<string,unknown> = {};
		for( const name of names ){
			const v = args[name];
			if( v!==undefined && v!=='' )
				vars[name] = v instanceof Date ? v.toISOString() : v;
		}
		return { params: Object.keys( vars ).map( k=>`${k}: $${k}` ).join( ', ' ), vars };
	}
	//a flow's deploys, newest first
	async versions( reader:HistReader, flowId:number ):Promise<FlowVersion[]>{
		const data = await reader.query<any>( FlowService.versionsQuery, {flow: flowId} );
		return ( <any[]>(data?.flowVersions ?? []) ).map( toFlowVersion ).sort( ( a, b )=>b.version-a.version );
	}
	static readonly versionsQuery = 'flowVersions( flowId: $flow ){ id version graph deployed identityId userName comment }';
	//the context values a flow's page shows:  the flow's own and its nodes', then the gateway's, which every flow shares
	async contexts( reader:HistReader, flowId:number ):Promise<FlowContextValue[]>{
		const [own, global] = await Promise.all( [reader.query<any>( FlowService.contextsQuery, {flow: flowId} ), reader.query<any>( FlowService.globalContextsQuery )] );
		return [...<any[]>(own?.flowContexts ?? []), ...<any[]>(global?.flowContexts ?? [])].map( toFlowContextValue );
	}
	static readonly contextsQuery = 'flowContexts( flowId: $flow ){ id flowId nodeId name value }';
	static readonly globalContextsQuery = 'flowContexts( flowId: null ){ id flowId nodeId name value }';
	//a flow's replays, newest first
	async runs( reader:HistReader, flowId:number ):Promise<FlowRun[]>{
		const data = await reader.query<any>( FlowService.runsQuery, {flow: flowId} );
		return ( <any[]>(data?.flowRuns ?? []) ).map( toFlowRun ).sort( ( a, b )=>b.id-a.id );
	}
	static readonly runsQuery = 'flowRuns( flowId: $flow ){ id version start finish mode started finished userName status }';

	//A flow made:  its row, with the guid the gateway gave it.  A slug is its trace's directory, and never changes.
	async create( poster:HistPoster, flow:{ name:string; slug:string; description?:string } ):Promise<Flow>{
		const vars:Record<string,unknown> = { name: flow.name, slug: flow.slug };
		if( flow.description )
			vars['description'] = flow.description;
		const data = await poster.postQL<any>( `mutation createFlow( ${Object.keys( vars ).map( k=>`${k}: $${k}` ).join( ', ' )} ){ id guid enabled version }`, vars );
		const row = data?.createFlow;
		const created = Array.isArray( row ) ? row[0] : row;
		return new Flow( {...flow, ...created, id: HistAdminService.createdId( data, 'createFlow' )} );
	}
	//the row's own columns changed:  `enabled` starts or stops the flow, `trace` restarts a running one at its level
	async update( poster:HistPoster, id:number, changes:{ enabled?:boolean; trace?:string|null; name?:string; description?:string|null } ):Promise<void>{
		const keys = Object.keys( changes );
		await poster.postQL<any>( `mutation updateFlow( id: $id${keys.map( k=>`, ${k}: $${k}` ).join( '' )} )`, { id, ...changes } );
	}
	setEnabled( poster:HistPoster, id:number, enabled:boolean ):Promise<void>{ return this.update( poster, id, {enabled} ); }
	//the graph stored as the flow's next version and, for an enabled flow, started in place of the one running
	async deploy( poster:HistPoster, flowId:number, graph:FlowGraph, comment?:string ):Promise<FlowDeployed>{
		const data = await poster.postQL<any>( FlowService.deployMutation, { flow: flowId, graph, comment: comment?.trim() || null } );
		return toFlowDeployed( data?.deployFlow );
	}
	static readonly deployMutation = 'mutation deployFlow( flow: $flow, graph: $graph, comment: $comment ){ flow version deployed userName running dropped fault{ node message } }';
	//an inject node's message sent, or `message` in its place
	async inject( poster:HistPoster, flowId:number, node:string, message?:Record<string,unknown> ):Promise<void>{
		await poster.postQL<any>( FlowService.injectMutation, { flow: flowId, node, message: message ?? null } );
	}
	static readonly injectMutation = 'mutation injectFlow( flow: $flow, node: $node, message: $message ){ node }';
	//A context value set, any json but null, or taken away by none:  no flow is the gateway's scope, no node the flow's.
	async setContext( poster:HistPoster, key:{ flowId?:number; nodeId?:string; name:string }, value:unknown ):Promise<void>{
		await poster.postQL<any>( FlowService.setContextMutation, { flow: key.flowId ?? null, node: key.nodeId || null, name: key.name, value: value ?? null } );
	}
	static readonly setContextMutation = 'mutation setFlowContext( flow: $flow, node: $node, name: $name, value: $value ){ name value }';
	//the flow gone, with its versions, runs, context and trace
	async purge( poster:HistPoster, id:number ):Promise<void>{
		await poster.postQL<any>( 'mutation purgeFlow( id: $id )', {id} );
	}
	//The current version run over its sources' history (spec *Replay*), dry or commit:  answered as the run starts, with its row.
	async replay( poster:HistPoster, flowId:number, from:Date, to:Date, mode:'dry'|'commit' ):Promise<FlowRun>{
		const data = await poster.postQL<any>( FlowService.replayMutation, { flow: flowId, from: from.toISOString(), to: to.toISOString(), mode } );
		return toFlowRun( data?.replayFlow );
	}
	static readonly replayMutation = 'mutation replayFlow( flow: $flow, from: $from, to: $to, mode: $mode ){ id version start finish mode started userName status }';
	//a run under way stopped, answered once its row says how it ended
	async cancelRun( poster:HistPoster, id:number ):Promise<void>{
		await poster.postQL<any>( 'mutation cancelFlowRun( id: $id ){ id status finished }', {id} );
	}
}