import { FlowLiveEvent } from './flow-live';
import { FlowNodeCounts, FlowNodeStatus, FlowRecordKind, FlowTraceRecord, flowWireKey, noFlowCounts } from './flow-trace';
import { addFlowCounts } from './flow-trace-view';

//The state the Activity tab keeps of a subscribed flow's events (#456, docs/flows/spec.md *Live*):  each node's status and
//figures since the ack, its last error, each wire's rate and last message, and the hops for the debug panel.  Apart from
//flow-live.ts, whose readers the gateway's service needs, so none of this is in the initial bundle.

//A node as the canvas decorates it:  its status, its figures since the ack, its last error's text, and whether it has erred.
export type FlowNodeLive = { status?:FlowNodeStatus; counts:FlowNodeCounts; error?:string };
//A wire as the canvas decorates it:  the messages down it since the ack, its rate a second over the last push's interval,
//and the last message the pushes carried.
export type FlowWireLive = { count:number; rate:number; at:number; msgId?:string; time?:Date; json?:unknown; cut:boolean };
//The state of a subscribed flow, built from its events:  `version` the run's, `nodes` and `wires` by the canvas's keys, the
//`hops` newest first, capped, `dropped` the oldest the gateway or the cap left out.  A push's rate is its count over the time
//since the push before, and `settle` lets a wire's rate go once no push has named it for `quiet` ms, since a push comes only
//where something changed.
export type FlowLiveState = { version?:number; nodes:Map<string,FlowNodeLive>; wires:Map<string,FlowWireLive>; hops:FlowTraceRecord[]; dropped:number; pushed:number; pushes:number };
export const flowLiveStart = ():FlowLiveState=>({ nodes: new Map(), wires: new Map(), hops: [], dropped: 0, pushed: 0, pushes: 0 });
export const flowHopsKept = 500;
export const flowLiveQuiet = 2_000;//ms after its last push a wire's rate stands
export function flowLiveNext( state:FlowLiveState, event:FlowLiveEvent, now:number, kept=flowHopsKept ):FlowLiveState{
	if( event.kind=='dropped' )
		return { ...state, wires: new Map( [...state.wires].map( ( [k, w] )=>[k, {...w, rate: 0}] ) ) };
	if( event.kind=='ack' ){//a run:  its figures from the start, the wires' from nothing, the panel's hops kept
		const nodes = new Map<string,FlowNodeLive>( event.nodes.map( n=>[n.node, { status: n.status, counts: n.counts ?? noFlowCounts() }] ) );
		return { ...state, version: event.version, nodes, wires: new Map(), pushed: now, pushes: 0 };
	}
	const { activity } = event;
	const nodes = new Map( state.nodes );
	for( const n of activity.nodes ){
		const was = nodes.get( n.node ) ?? { counts: noFlowCounts() };
		nodes.set( n.node, { status: n.status ?? was.status, counts: n.counts ? addFlowCounts( was.counts, n.counts ) : was.counts, error: n.error ?? was.error } );
	}
	const interval = Math.max( 1, now-state.pushed )/1000;
	const wires = new Map( state.wires );
	for( const w of activity.wires ){
		const key = flowWireKey( w.node, w.port, w.to ), was = wires.get( key );
		wires.set( key, { count: (was?.count ?? 0)+w.count, rate: w.count/interval, at: now, msgId: w.msgId ?? was?.msgId, time: w.time ?? was?.time, json: w.msgId ? w.json : was?.json, cut: w.msgId ? w.cut : (was?.cut ?? false) } );
	}
	let hops = state.hops, dropped = state.dropped+activity.hopsDropped;
	if( activity.hops.length ){
		hops = [...activity.hops].reverse().concat( state.hops );
		if( hops.length>kept ){
			dropped += hops.length-kept;
			hops = hops.slice( 0, kept );
		}
	}
	return { ...state, nodes, wires, hops, dropped, pushed: now, pushes: state.pushes+1 };
}
//the rates of the wires no push has named for a while let go:  the same state where none has
export function flowLiveSettle( state:FlowLiveState, now:number, quiet=flowLiveQuiet ):FlowLiveState{
	let changed = false;
	const wires = new Map( [...state.wires].map( ( [k, w] )=>{
		if( w.rate && now-w.at>=quiet ){
			changed = true;
			return [k, {...w, rate: 0}];
		}
		return [k, w];
	} ) );
	return changed ? { ...state, wires } : state;
}
//the debug panel's hops, filtered by node and by kind, newest first
export function flowHopsShown( hops:FlowTraceRecord[], node:string|undefined, kind:FlowRecordKind|undefined ):FlowTraceRecord[]{
	return hops.filter( h=>(!node || h.node==node) && (!kind || h.kind==kind) );
}