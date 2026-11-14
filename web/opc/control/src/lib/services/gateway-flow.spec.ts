import Long from 'long';
import { FlowLiveEvent } from '../model/flow-live';
import { Gateway } from './gateway-service';
import { TestGateway } from 'jde-opc/testing';

//TestGateway's socket:  the flow subscriptions it sent, and the cancellations.
class FlowGateway extends TestGateway{
	get subscribes(){ return this.#wired().filter( s=>s.m.flowSubscribe ); }
	get unsubscribes(){ return this.#wired().filter( s=>s.m.flowUnsubscribe ); }
	#wired(){ return this.wire.mock.calls.map( ([requestId, m])=>({requestId, m}) ); }
}
const listen = ( gateway:FlowGateway, flow=7, hops=false )=>{
	const events:FlowLiveEvent[] = [];
	let error:any;
	const subscription = gateway.flowSubscribe( flow, hops ).subscribe( {next: e=>events.push( e ), error: e=>error = e} );
	const requestId = gateway.subscribes.at( -1 )!.requestId;
	return { events, subscription, requestId, get error(){ return error; } };
};
const bytes = ( s:string )=>new TextEncoder().encode( s );
const n = ( v:number )=>Long.fromNumber( v, true );
const ack = ( version:number )=>({ version, nodes: [{node: 'in', status: {fill: bytes( 'green' ), shape: bytes( 'dot' ), text: bytes( 'ok' )}, counts: {received: n( 0 ), sent: [n( 2 )], drops: n( 0 ), errors: n( 0 )}}] });

describe( 'Gateway.flowSubscribe', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );

	it( "sends FlowSubscribe with the flow, and delivers the ack, then each push under its id", ()=>{
		const gateway = new FlowGateway();
		const s = listen( gateway, 7, true );
		expect( gateway.subscribes.map( x=>x.m.flowSubscribe ) ).toEqual( [{flow: 7, hops: true}] );
		gateway.receive( {requestId: s.requestId, flowSubscribeAck: ack( 2 )} );
		gateway.receive( {requestId: s.requestId, flowActivity: {nodes: [{node: 'in', counts: {received: n( 0 ), sent: [n( 1 )], drops: n( 0 ), errors: n( 0 )}, error: new Uint8Array()}], wires: [{node: 'in', port: 0, to: 'fn', count: n( 1 ), msgId: n( 9 ), time: new Date( 1000 ), json: bytes( '{"payload":1}' ), cut: false}], hops: [], hopsDropped: 0}} );
		expect( s.events.map( e=>e.kind ) ).toEqual( ['ack', 'activity'] );
		expect( s.events[0] ).toEqual( {kind: 'ack', version: 2, nodes: [{node: 'in', status: {fill: 'green', shape: 'dot', text: 'ok'}, counts: {received: 0, sent: [2], drops: 0, errors: 0}}]} );
		expect( s.events[1] ).toMatchObject( {kind: 'activity', activity: {wires: [{node: 'in', port: 0, to: 'fn', count: 1, msgId: '9', json: {payload: 1}}]}} );
		//a deploy:  another ack under the same id
		gateway.receive( {requestId: s.requestId, flowSubscribeAck: ack( 3 )} );
		expect( s.events.at( -1 ) ).toMatchObject( {kind: 'ack', version: 3} );
		s.subscription.unsubscribe();
		expect( gateway.unsubscribes.map( x=>x.m.flowUnsubscribe ) ).toEqual( [{subscription: s.requestId}] );
	} );
	it( 'keeps two flows apart by their ids, and drops what is on the wire for one that ended', ()=>{
		const gateway = new FlowGateway();
		const a = listen( gateway, 7 ), b = listen( gateway, 8 );
		expect( gateway.subscribes.map( x=>x.m.flowSubscribe ) ).toEqual( [{flow: 7, hops: false}, {flow: 8, hops: false}] );
		gateway.receive( {requestId: b.requestId, flowSubscribeAck: ack( 1 )} );
		expect( [a.events.length, b.events.length] ).toEqual( [0, 1] );
		a.subscription.unsubscribe();
		gateway.receive( {requestId: a.requestId, flowSubscribeAck: ack( 1 )} );
		expect( a.events ).toHaveLength( 0 );
		b.subscription.unsubscribe();
	} );
	//the gateway ends a subscription with an exception under its id (spec *Live*):  the flow stopped, purged, or refused at a re-check
	it( 'ends on an exception under the id, with its status, and unsubscribes nothing', ()=>{
		const gateway = new FlowGateway();
		const s = listen( gateway );
		gateway.receive( {requestId: s.requestId, flowSubscribeAck: ack( 2 )} );
		gateway.receive( {requestId: s.requestId, exception: {what: "Flow 'pump' is not running:  disabled.", statusCode: 409}} );
		expect( s.error ).toEqual( {error: {requestId: s.requestId, message: "Flow 'pump' is not running:  disabled.", sc: 0, httpStatus: 409}} );
		s.subscription.unsubscribe();
		expect( gateway.unsubscribes ).toHaveLength( 0 );
	} );
	describe( 'after a drop', ()=>{
		beforeEach( ()=>vi.useFakeTimers() );
		afterEach( ()=>vi.useRealTimers() );
		it( 'says so once, subscribes again under a new id, and the new ack follows', ()=>{
			const gateway = new FlowGateway();
			const s = listen( gateway, 7, true );
			gateway.receive( {requestId: s.requestId, flowSubscribeAck: ack( 2 )} );
			gateway.drop();
			expect( s.events.at( -1 ) ).toEqual( {kind: 'dropped'} );
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			const again = gateway.subscribes[1];
			expect( again.requestId ).not.toBe( s.requestId );
			expect( again.m.flowSubscribe ).toEqual( {flow: 7, hops: true} );
			gateway.receive( {requestId: again.requestId, flowSubscribeAck: ack( 2 )} );
			expect( s.events.map( e=>e.kind ) ).toEqual( ['ack', 'dropped', 'ack'] );
			s.subscription.unsubscribe();
			expect( gateway.unsubscribes.map( x=>x.m.flowUnsubscribe ) ).toEqual( [{subscription: again.requestId}] );
		} );
	} );
} );