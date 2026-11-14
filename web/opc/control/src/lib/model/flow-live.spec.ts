import Long from 'long';
import type * as FromServer from 'jde-proto/Opc.FromServer';
import { toFlowLiveActivity, toFlowLiveNode } from './flow-live';
import { flowHopsShown, flowLiveNext, flowLiveSettle, flowLiveStart } from './flow-live-view';

const bytes = ( s:string )=>new TextEncoder().encode( s );
const counts = ( received:number, sent:number[], drops=0, errors=0 ):FromServer.FlowCounts=>({ received: Long.fromNumber( received, true ), sent: sent.map( n=>Long.fromNumber( n, true ) ), drops: Long.fromNumber( drops, true ), errors: Long.fromNumber( errors, true ) });
const wire = ( node:string, port:number, to:string, count:number, msgId:number, json:string, cut=false ):FromServer.FlowWire=>({ node, port, to, count: Long.fromNumber( count, true ), msgId: Long.fromNumber( msgId, true ), time: new Date( 1000 ), json: bytes( json ), cut });
const hop = ( msgId:number, node:string, kind:number, more:Partial<FromServer.FlowHop>={} ):FromServer.FlowHop=>({ time: new Date( 2000 ), msgId: Long.fromNumber( msgId, true ), parentId: Long.UZERO, runId: 0, node, kind, ports: [], cut: false, error: new Uint8Array(), blocks: [], ...more });
const activity = ( more:Partial<FromServer.FlowActivity>={} ):FromServer.FlowActivity=>({ nodes: [], wires: [], hops: [], hopsDropped: 0, ...more });

describe( 'flow-live', ()=>{
	it( 'reads the ack and a push from the protos, their text from bytes', ()=>{
		const ack = toFlowLiveNode( {node: 'in', status: {fill: bytes( 'green' ), shape: bytes( 'dot' ), text: bytes( 'ok' )}, counts: counts( 3, [3] )} );
		expect( ack ).toEqual( {node: 'in', status: {fill: 'green', shape: 'dot', text: 'ok'}, counts: {received: 3, sent: [3], drops: 0, errors: 0}} );
		const push = toFlowLiveActivity( activity( {
			nodes: [{node: 'fn', counts: counts( 1, [0], 0, 1 ), status: undefined, error: bytes( 'boom' )}],
			wires: [wire( 'in', 0, 'fn', 2, 7, '{"payload":1,"_id":{"low":7,"high":0,"unsigned":true}}' ), wire( 'fn', 0, 'out', 1, 8, '{"payload":', true )],
			hops: [hop( 7, 'in', 1, {ports: [0], json: bytes( '{"payload":1}' )} ), hop( 7, 'fn', 3, {error: bytes( 'boom' )} ), hop( 0, 'fn', 4, {status: {fill: bytes( 'red' ), shape: bytes( 'ring' ), text: bytes( 'failed' )}} )],
			hopsDropped: 2
		} ) );
		expect( push.nodes ).toEqual( [{node: 'fn', status: undefined, counts: {received: 1, sent: [0], drops: 0, errors: 1}, error: 'boom'}] );
		expect( push.wires[0] ).toMatchObject( {node: 'in', port: 0, to: 'fn', count: 2, msgId: '7', json: {payload: 1, _id: {low: 7, high: 0, unsigned: true}}, cut: false} );
		expect( push.wires[1] ).toMatchObject( {msgId: '8', json: '{"payload":', cut: true} );
		expect( push.hops.map( h=>[h.msgId, h.node, h.kind] ) ).toEqual( [['7', 'in', 'birth'], ['7', 'fn', 'error'], [undefined, 'fn', 'status']] );
		expect( push.hops[0] ).toMatchObject( {ports: [0], json: {payload: 1}} );
		expect( push.hops[1].error ).toBe( 'boom' );
		expect( push.hops[2].status ).toEqual( {fill: 'red', shape: 'ring', text: 'failed'} );
		expect( push.hopsDropped ).toBe( 2 );
	} );
	//the ack starts the figures, each push adds its own, a wire's rate is its count over the push's interval, and the hops
	//collect newest first under the cap
	it( 'keeps the nodes, wires and hops up to date from the events', ()=>{
		let state = flowLiveStart();
		state = flowLiveNext( state, {kind: 'ack', version: 2, nodes: [{node: 'in', counts: {received: 0, sent: [10], drops: 0, errors: 0}}, {node: 'fn', status: {fill: 'green', shape: 'dot', text: 'ok'}, counts: {received: 10, sent: [9], drops: 0, errors: 1}}]}, 1000 );
		expect( [state.version, state.pushes, state.nodes.get( 'fn' )?.counts.errors] ).toEqual( [2, 0, 1] );
		const push = toFlowLiveActivity( activity( {
			nodes: [{node: 'fn', counts: counts( 4, [3], 1, 0 ), status: {fill: bytes( 'yellow' ), shape: bytes( 'ring' ), text: bytes( 'slow' )}, error: new Uint8Array()}],
			wires: [wire( 'in', 0, 'fn', 4, 11, '{"payload":4}' )],
			hops: [hop( 11, 'in', 1 ), hop( 11, 'fn', 0 ), hop( 12, 'in', 1 )]
		} ) );
		state = flowLiveNext( state, {kind: 'activity', activity: push}, 1500 );
		expect( state.nodes.get( 'fn' ) ).toEqual( {status: {fill: 'yellow', shape: 'ring', text: 'slow'}, counts: {received: 14, sent: [12], drops: 1, errors: 1}, error: undefined} );
		expect( state.nodes.get( 'in' )?.counts.sent ).toEqual( [10] );
		expect( state.wires.get( 'in:0>fn' ) ).toMatchObject( {count: 4, rate: 8, at: 1500, msgId: '11', json: {payload: 4}} );
		expect( state.hops.map( h=>h.msgId ) ).toEqual( ['12', '11', '11'] );
		expect( state.pushes ).toBe( 1 );
		//a push with the wire again, its count added and its last message kept where the push names none
		state = flowLiveNext( state, {kind: 'activity', activity: toFlowLiveActivity( activity( {nodes: [{node: 'fn', error: bytes( 'boom' ), counts: counts( 0, [], 0, 1 )}], wires: [{...wire( 'in', 0, 'fn', 2, 0, '' ), time: undefined}], hops: [hop( 13, 'in', 1 )]} ) )}, 2500, 3 );
		expect( state.wires.get( 'in:0>fn' ) ).toMatchObject( {count: 6, rate: 2, msgId: '11', json: {payload: 4}} );
		expect( state.nodes.get( 'fn' ) ).toMatchObject( {error: 'boom', counts: {errors: 2}} );
		expect( [state.hops.map( h=>h.msgId ), state.dropped] ).toEqual( [['13', '12', '11'], 1] );//capped at three:  the oldest left out and counted
		//quiet wires let go of their rate, a drop every one's
		expect( flowLiveSettle( state, 3000 ) ).toBe( state );
		expect( flowLiveSettle( state, 4600 ).wires.get( 'in:0>fn' )?.rate ).toBe( 0 );
		expect( flowLiveNext( state, {kind: 'dropped'}, 2600 ).wires.get( 'in:0>fn' )?.rate ).toBe( 0 );
		//a new run's ack:  the figures its own, the wires from nothing, the panel's hops kept
		state = flowLiveNext( state, {kind: 'ack', version: 3, nodes: [{node: 'in', counts: {received: 0, sent: [0], drops: 0, errors: 0}}]}, 5000 );
		expect( [state.version, state.nodes.size, state.wires.size, state.hops.length] ).toEqual( [3, 1, 0, 3] );
		expect( flowHopsShown( state.hops, 'in', undefined ).map( h=>h.msgId ) ).toEqual( ['13', '12'] );
		expect( flowHopsShown( state.hops, 'fn', undefined ).map( h=>h.msgId ) ).toEqual( ['11'] );
		expect( flowHopsShown( state.hops, undefined, 'birth' ).map( h=>h.msgId ) ).toEqual( ['13', '12'] );
		expect( flowHopsShown( state.hops, 'in', 'hop' ) ).toEqual( [] );
	} );
} );