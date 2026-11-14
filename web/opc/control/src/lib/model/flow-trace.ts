import Long from 'long';

//The flow's trace as the pages read it (#456, docs/flows/spec.md *Trace*, *History*):  a record as flowTrace and flowMessage
//answer one and as a live push carries one, and the minute figures flowCounts sums into buckets.  What History makes of them is
//flow-trace-view.ts, which the gateway's service, and so the initial bundle, has no need of.

export const flowRecordKinds = ['hop', 'birth', 'clone', 'error', 'status', 'drop', 'captured'] as const;
export type FlowRecordKind = typeof flowRecordKinds[number];
//a node's status as Node-RED shapes it:  a fill colour, a dot or a ring, and its text
export type FlowNodeStatus = { fill:string; shape:string; text:string };
//a node's figures:  since the flow started, since the push before, or over a bucket
export type FlowNodeCounts = { received:number; sent:number[]; drops:number; errors:number };
export const noFlowCounts = ():FlowNodeCounts=>({ received: 0, sent: [], drops: 0, errors: 0 });
//A message's id as the pages hold it:  decimal text, since a 64-bit integer is past a number and the trace reads take text.
//`msgId` is none for a status record, which is no message's.
export type FlowTraceRecord = {
	time:Date; msgId?:string; parentId?:string; runId:number; node:string; kind:FlowRecordKind; ports:number[];
	json?:unknown; cut:boolean; error?:string; status?:FlowNodeStatus; blocks:string[];
};
//A 64-bit integer as the gateway writes one into json, the web's Long, `{ low, high, unsigned }`, or a number or decimal text, as
//text;  none for 0, which no message has, and for what is none of these.
export function flowMessageId( v:unknown ):string|undefined{
	if( v==null )
		return undefined;
	let text:string;
	if( Long.isLong( v ) )
		text = v.toString();
	else if( typeof v=='number' )
		text = Number.isInteger( v ) ? String( v ) : '';
	else if( typeof v=='string' )
		text = /^\d+$/.test( v ) ? v : '';
	else if( typeof v=='object' && typeof (<any>v).low=='number' && typeof (<any>v).high=='number' )
		text = Long.fromBits( (<any>v).low, (<any>v).high, !!(<any>v).unsigned ).toString();
	else
		text = '';
	return text && text!='0' ? text : undefined;
}
export function toFlowRecordKind( v:unknown ):FlowRecordKind{ return (<readonly string[]>flowRecordKinds).includes( <string>v ) ? <FlowRecordKind>v : 'hop'; }
export function toFlowNodeStatus( json:any ):FlowNodeStatus|undefined{
	return json && typeof json=='object' ? { fill: String( json.fill ?? '' ), shape: String( json.shape ?? '' ), text: String( json.text ?? '' ) } : undefined;
}
//a record as the reads answer one:  its json parsed where whole, its text where cut
export function toFlowTraceRecord( json:any ):FlowTraceRecord{
	return {
		time: new Date( json.time ), msgId: flowMessageId( json.msgId ), parentId: flowMessageId( json.parentId ), runId: Number( json.runId ?? 0 ),
		node: String( json.node ?? '' ), kind: toFlowRecordKind( json.kind ), ports: Array.isArray( json.ports ) ? json.ports.map( Number ) : [],
		json: json.json===undefined ? undefined : json.json, cut: !!json.cut, error: json.error || undefined, status: toFlowNodeStatus( json.status ),
		blocks: Array.isArray( json.blocks ) ? json.blocks.map( String ) : []
	};
}
//a wire as the live push and the counts name one, by its ends:  `node:port>to`
export const flowWireKey = ( node:string, port:number, to:string )=>`${node}:${port}>${to}`;
//a bucket's figures of one node, as flowCounts answers them
export type FlowCountRow = { time:Date; node:string; counts:FlowNodeCounts };
export function toFlowCountRow( json:any ):FlowCountRow{
	return { time: new Date( json.time ), node: String( json.node ?? '' ), counts: { received: Number( json.received ?? 0 ), sent: Array.isArray( json.sent ) ? json.sent.map( Number ) : [], drops: Number( json.drops ?? 0 ), errors: Number( json.errors ?? 0 ) } };
}