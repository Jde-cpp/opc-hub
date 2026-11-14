import type * as FromServer from 'jde-proto/Opc.FromServer';
import { flowMessageId, FlowNodeCounts, FlowNodeStatus, FlowRecordKind, flowRecordKinds, FlowTraceRecord } from './flow-trace';

//A flow's live activity (#456, docs/flows/spec.md *Live*):  what a FlowSubscribe's ack and its pushes carry, as Gateway.flowSubscribe
//delivers them.  The protos' text is bytes, since a Function body may write what is not UTF-8;  it is read as text here, a bad
//byte the replacement character.  What the Activity tab keeps of the events is flow-live-view.ts, which the gateway's service,
//and so the initial bundle, has no need of.

const decoder = new TextDecoder();
const text = ( v:Uint8Array|undefined ):string=>v?.length ? decoder.decode( v ) : '';
const count = ( v:{ toNumber():number }|number|undefined ):number=>v==undefined ? 0 : typeof v=='number' ? v : v.toNumber();
//a message's json as the push carries it:  parsed where whole, its text where cut, nothing where none
function json( v:Uint8Array|undefined, cut:boolean ):unknown{
	const s = text( v );
	if( !s )
		return undefined;
	if( cut )
		return s;
	try{
		return JSON.parse( s );
	}
	catch{
		return s;
	}
}
export function toFlowLiveStatus( p:FromServer.FlowStatus|undefined ):FlowNodeStatus|undefined{
	return p ? { fill: text( p.fill ), shape: text( p.shape ), text: text( p.text ) } : undefined;
}
export function toFlowLiveCounts( p:FromServer.FlowCounts|undefined ):FlowNodeCounts|undefined{
	return p ? { received: count( p.received ), sent: p.sent.map( count ), drops: count( p.drops ), errors: count( p.errors ) } : undefined;
}
//a node as the ack lists it:  its status and its figures since the flow started
export type FlowLiveNode = { node:string; status?:FlowNodeStatus; counts?:FlowNodeCounts; error?:string };
//a wire a message went down since the push before:  the count, and the first of them
export type FlowLiveWire = { node:string; port:number; to:string; count:number; msgId?:string; time?:Date; json?:unknown; cut:boolean };
export type FlowLiveActivity = { nodes:FlowLiveNode[]; wires:FlowLiveWire[]; hops:FlowTraceRecord[]; hopsDropped:number };
export function toFlowLiveNode( p:FromServer.FlowNodeState|FromServer.FlowNodeActivity ):FlowLiveNode{
	const y:FlowLiveNode = { node: p.node, status: toFlowLiveStatus( p.status ), counts: toFlowLiveCounts( p.counts ) };
	const error = 'error' in p ? text( p.error ) : '';
	if( error )
		y.error = error;
	return y;
}
export function toFlowLiveWire( p:FromServer.FlowWire ):FlowLiveWire{
	return { node: p.node, port: p.port, to: p.to, count: count( p.count ), msgId: flowMessageId( p.msgId ), time: p.time, json: json( p.json, p.cut ), cut: p.cut };
}
export function toFlowLiveHop( p:FromServer.FlowHop ):FlowTraceRecord{
	const error = text( p.error );
	return {
		time: p.time ?? new Date( 0 ), msgId: flowMessageId( p.msgId ), parentId: flowMessageId( p.parentId ), runId: p.runId, node: p.node,
		kind: (<readonly FlowRecordKind[]>flowRecordKinds)[p.kind] ?? 'hop', ports: [...p.ports], json: json( p.json, p.cut ), cut: p.cut, error: error || undefined, status: toFlowLiveStatus( p.status ), blocks: p.blocks.map( b=>text( b ) )
	};
}
export function toFlowLiveActivity( p:FromServer.FlowActivity ):FlowLiveActivity{
	return { nodes: p.nodes.map( toFlowLiveNode ), wires: p.wires.map( toFlowLiveWire ), hops: p.hops.map( toFlowLiveHop ), hopsDropped: p.hopsDropped };
}
//What a subscription delivers:  the ack, the run's version with each node's status and figures since it started, again for
//each run that takes its place;  each push after it;  and `dropped` when the socket went, once until the next ack - the gateway
//subscribes again, and the new run's ack follows.
export type FlowLiveEvent =
	{ kind:'ack'; version:number; nodes:FlowLiveNode[] } |
	{ kind:'activity'; activity:FlowLiveActivity } |
	{ kind:'dropped' };