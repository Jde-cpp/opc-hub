import { BadBoundNotFound, histResumeReach } from '../model/hist';
import { Gateway, HistEvent } from './gateway-service';
import { silence } from 'jde-spa/testing';
import { nodeA as A, nodeB as B, TestGateway } from 'jde-opc/testing';

const BadNodeIdUnknown = 0x80340000, BadDataLost = 0x809D0000;

//TestGateway's socket:  the historian subscriptions it sent, and the cancellations.
class HistGateway extends TestGateway{
	get subscribes(){ return this.#wired().filter( s=>s.m.histSubscribe ); }
	get unsubscribes(){ return this.#wired().filter( s=>s.m.histUnsubscribe ); }
	#wired(){ return this.wire.mock.calls.map( ([requestId, m])=>({requestId, m}) ); }
}
const listen = ( gateway:HistGateway, from?:Date )=>{
	const events:HistEvent[] = [];
	let error:any;
	const subscription = gateway.histSubscribe( {group: 3, nodes: [A, B], from} ).subscribe( {next: e=>events.push( e ), error: e=>error = e} );
	const requestId = gateway.subscribes.at( -1 )!.requestId;
	return { events, subscription, requestId, get error(){ return error; } };
};
const value = ( node:number, ms:number, v?:number, extra:object={} )=>({ node, value: v==undefined ? undefined : {doubleValue: v}, sc: 0, source: new Date(ms), heartbeat: false, bound: false, ...extra });

describe( 'Gateway.histSubscribe', ()=>{
	afterEach( ()=>vi.restoreAllMocks() );//a spy restored after its assertions stayed mocked past one that failed (historian-live-trend #15)

	it( 'sends HistSubscribe with the group, the nodes as expanded ids and the time, and the ack names a status per node in order', ()=>{
		const gateway = new HistGateway();
		const s = listen( gateway, new Date(1000) );
		expect( gateway.subscribes ).toHaveLength( 1 );
		expect( gateway.subscribes[0].m.histSubscribe ).toEqual( {group: 3, nodes: [{node: {namespaceIndex: 2, numeric: 1}, namespaceUri: "", serverIndex: 0}, {node: {namespaceIndex: 2, numeric: 2}, namespaceUri: "", serverIndex: 0}], fromTime: new Date(1000)} );
		gateway.receive( {requestId: s.requestId, histSubscribeAck: {statuses: [0, BadNodeIdUnknown]}} );
		expect( s.events ).toEqual( [{kind: 'ack', statuses: [0, BadNodeIdUnknown]}] );
		s.subscription.unsubscribe();
	} );
	it( 'leaves fromTime off for a subscription from now', ()=>{
		const gateway = new HistGateway();
		const s = listen( gateway );
		expect( gateway.subscribes[0].m.histSubscribe.fromTime ).toBeUndefined();
		s.subscription.unsubscribe();
	} );
	it( "places a snapshot's records on the node each slot names, with its times, status, bound and heartbeat", ()=>{
		const gateway = new HistGateway();
		const warn = silence();
		const s = listen( gateway );
		gateway.receive( {requestId: s.requestId, histSnapshot: {values: [
			value( 0, 5000, 7, {server: new Date(5001), bound: true} ),
			{ node: 1, sc: BadDataLost, source: new Date(6000), heartbeat: false, bound: false },//a marker:  no value
			value( 1, 6500, 2, {heartbeat: true} ),
			{ node: 0, value: {doubleValue: 1}, sc: 0, server: new Date(7000), heartbeat: false, bound: false },//no source time:  filed by server
			value( 5, 8000, 9 )//a slot the subscribe didn't name
		], more: true}} );
		expect( s.events ).toHaveLength( 1 );
		const page = <Extract<HistEvent,{kind:'snapshot'}>>s.events[0];
		expect( page.more ).toBe( true );
		expect( page.values.map( v=>v.node.key ) ).toEqual( [A.key, B.key, B.key, A.key] );
		expect( page.values[0] ).toMatchObject( {source: new Date(5000), server: new Date(5001), status: 0, value: 7, bound: true, heartbeat: false} );
		expect( page.values[0].sourceTime ).toBeUndefined();//a Date's, which no edit can name
		expect( page.values[1] ).toMatchObject( {status: BadDataLost, value: undefined} );
		expect( page.values[2] ).toMatchObject( {value: 2, heartbeat: true} );
		expect( page.values[3] ).toMatchObject( {source: new Date(7000), server: new Date(7000)} );
		expect( warn ).toHaveBeenCalledTimes( 1 );
		s.subscription.unsubscribe();
	} );
	it( 'delivers the records stored since, and a modification with the value put there, or none for a delete', ()=>{
		const gateway = new HistGateway();
		const s = listen( gateway );
		gateway.receive( {requestId: s.requestId, histRecords: {values: [value( 0, 8000, 8 )], modifications: [
			{ node: 1, target: new Date(6000), type: 4, time: new Date(9000), user: 'me' },
			{ node: 0, target: new Date(5000), type: 2, value: value( 0, 5000, 9 ), time: new Date(9001), user: 'me' }
		]}} );
		const records = <Extract<HistEvent,{kind:'records'}>>s.events[0];
		expect( records.values ).toHaveLength( 1 );
		expect( records.values[0].node.key ).toBe( A.key );
		expect( records.values[0] ).toMatchObject( {source: new Date(8000), value: 8} );
		expect( records.modifications ).toHaveLength( 2 );
		expect( records.modifications.map( m=>m.node.key ) ).toEqual( [B.key, A.key] );
		expect( records.modifications[0] ).toMatchObject( {target: new Date(6000), kind: 'delete', value: undefined, time: new Date(9000), user: 'me'} );
		expect( records.modifications[1] ).toMatchObject( {target: new Date(5000), kind: 'replace', time: new Date(9001)} );
		expect( records.modifications[1].value?.node.key ).toBe( A.key );
		expect( records.modifications[1].value ).toMatchObject( {source: new Date(5000), value: 9} );
		s.subscription.unsubscribe();
	} );
	it( 'sends HistUnsubscribe naming the subscription when the listener goes', ()=>{
		const gateway = new HistGateway();
		const s = listen( gateway );
		gateway.receive( {requestId: s.requestId, histSubscribeAck: {statuses: [0, 0]}} );
		s.subscription.unsubscribe();
		expect( gateway.unsubscribes ).toHaveLength( 1 );
		expect( gateway.unsubscribes[0].m.histUnsubscribe ).toEqual( {subscription: s.requestId} );
		expect( gateway.unsubscribes[0].requestId ).not.toBe( s.requestId );
		gateway.receive( {requestId: s.requestId, histRecords: {values: [value( 0, 8000, 8 )], modifications: []}} );//a stray:  nothing of it follows the completion, but a listener gone hears nothing either way
		expect( s.events ).toHaveLength( 1 );
	} );
	//The teardown forgets the id at once:  a page or push already on the wire, and the ack the gateway sends after the
	//unsubscribe of one still registering, each warned of no subscription (historian-live-trend #10).
	it( 'drops a page, a push and the late ack under an ended subscription, without a warning', ()=>{
		const gateway = new HistGateway();
		const warn = silence();
		const s = listen( gateway );
		s.subscription.unsubscribe();
		gateway.receive(
			{requestId: s.requestId, histSnapshot: {values: [value( 0, 5000, 7 )], more: true}},
			{requestId: s.requestId, histRecords: {values: [value( 0, 8000, 8 )], modifications: []}},
			{requestId: s.requestId, histSubscribeAck: {statuses: [0, 0]}}
		);
		expect( s.events ).toEqual( [] );
		expect( warn ).not.toHaveBeenCalled();
	} );
	//the gateway ends a subscription with an exception under its id (spec *Subscriptions*):  NotFound for a group removed,
	//ServiceUnavailable for a historian stopped - and the id has no callback by then
	it( 'ends on an exception under the id after the ack, with its status, and unsubscribes nothing', ()=>{
		const gateway = new HistGateway();
		const s = listen( gateway );
		gateway.receive( {requestId: s.requestId, histSubscribeAck: {statuses: [0, 0]}} );
		gateway.receive( {requestId: s.requestId, exception: {what: "The group was removed, which ends the subscription.", statusCode: 404}} );
		expect( s.error ).toEqual( {error: {requestId: s.requestId, message: "The group was removed, which ends the subscription.", sc: 0, httpStatus: 404}} );
		expect( gateway.unsubscribes ).toHaveLength( 0 );
	} );
	it( 'ends on a refusal of the HistSubscribe itself', ()=>{
		const gateway = new HistGateway();
		const s = listen( gateway );
		gateway.receive( {requestId: s.requestId, exception: {what: "Group 3 isn't one this gateway historizes.", statusCode: 404}} );
		expect( s.error?.error?.httpStatus ).toBe( 404 );
		expect( s.events ).toEqual( [] );
		expect( gateway.unsubscribes ).toHaveLength( 0 );
	} );

	describe( 'after a drop', ()=>{
		beforeEach( ()=>vi.useFakeTimers() );
		afterEach( ()=>vi.useRealTimers() );
		//the socket carried the subscription, so the gateway has forgotten it:  subscribe again from the oldest of the nodes'
		//latest source times delivered, so the snapshot fills the outage for a node whose clock runs behind (historian-live-trend #5)
		it( "says so, and subscribes again from the oldest of its nodes' latest source times", ()=>{
			const gateway = new HistGateway();
			const s = listen( gateway, new Date(1000) );
			gateway.receive( {requestId: s.requestId, histSubscribeAck: {statuses: [0, 0]}} );
			gateway.receive( {requestId: s.requestId, histSnapshot: {values: [value( 0, 7000, 7 ), value( 1, 5000, 5 )], more: false}} );
			gateway.drop();
			expect( s.events.at( -1 ) ).toEqual( {kind: 'dropped'} );
			expect( gateway.subscribes ).toHaveLength( 1 );
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			expect( gateway.subscribes ).toHaveLength( 2 );
			const again = gateway.subscribes[1];
			expect( again.requestId ).not.toBe( s.requestId );
			expect( again.m.histSubscribe.fromTime ).toEqual( new Date(5000) );
			gateway.receive( {requestId: again.requestId, histSubscribeAck: {statuses: [0, 0]}} );
			expect( s.events.at( -1 ) ).toEqual( {kind: 'ack', statuses: [0, 0]} );
			s.subscription.unsubscribe();
			expect( gateway.unsubscribes[0].m.histUnsubscribe ).toEqual( {subscription: again.requestId} );
		} );
		//a node quiet longer than the reach has its value as the snapshot's bound:  the resubscribe goes no further back
		it( "subscribes again from no further back than the reach before the newest source time", ()=>{
			const gateway = new HistGateway();
			const s = listen( gateway, new Date(1000) );
			const newest = 5000+histResumeReach+1000;
			gateway.receive( {requestId: s.requestId, histSubscribeAck: {statuses: [0, 0]}} );
			gateway.receive( {requestId: s.requestId, histSnapshot: {values: [value( 1, 5000, 5 ), value( 0, newest, 7 )], more: false}} );
			gateway.drop();
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			expect( gateway.subscribes[1].m.histSubscribe.fromTime ).toEqual( new Date(newest-histResumeReach) );
			s.subscription.unsubscribe();
		} );
		//the snapshot's Bad_BoundNotFound stand-in for a node with no record before fromTime is no value:  not delivered, and no
		//time to resume from (historian-live-trend #6)
		it( "drops a node's Bad_BoundNotFound bound, which sets no time to resume from", ()=>{
			const gateway = new HistGateway();
			const s = listen( gateway, new Date(1000) );
			gateway.receive( {requestId: s.requestId, histSubscribeAck: {statuses: [0, 0]}} );
			gateway.receive( {requestId: s.requestId, histSnapshot: {values: [
				{ node: 1, sc: BadBoundNotFound, source: new Date(1000), heartbeat: false, bound: true },
				value( 0, 5000, 5, {bound: true} )
			], more: false}} );
			const page = <Extract<HistEvent,{kind:'snapshot'}>>s.events[1];
			expect( page.values.map( v=>v.node.key ) ).toEqual( [A.key] );
			gateway.drop();
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			expect( gateway.subscribes[1].m.histSubscribe.fromTime ).toEqual( new Date(5000) );
			s.subscription.unsubscribe();
		} );
		it( 'keeps trying while the gateway is down, from the time it asked for, and says dropped once', ()=>{
			const gateway = new HistGateway();
			const s = listen( gateway, new Date(1000) );
			gateway.receive( {requestId: s.requestId, histSubscribeAck: {statuses: [0, 0]}} );
			gateway.drop();
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			gateway.drop();//the retry's connect failed too
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			expect( gateway.subscribes ).toHaveLength( 3 );
			expect( gateway.subscribes.map( x=>x.m.histSubscribe.fromTime ) ).toEqual( [new Date(1000), new Date(1000), new Date(1000)] );
			expect( s.events.filter( e=>e.kind=='dropped' ) ).toHaveLength( 1 );
			s.subscription.unsubscribe();
		} );
		//a drop before the first ack says so too, once until an ack, and the next outage again (historian-live-trend #8)
		it( 'says dropped for a drop before the first ack, once until an ack, and again for the next outage', ()=>{
			const gateway = new HistGateway();
			const s = listen( gateway, new Date(1000) );
			gateway.drop();
			expect( s.events ).toEqual( [{kind: 'dropped'}] );
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			gateway.drop();//the gateway is still down
			vi.advanceTimersByTime( Gateway.histRetryDelay );
			expect( s.events ).toEqual( [{kind: 'dropped'}] );
			gateway.receive( {requestId: gateway.subscribes[2].requestId, histSubscribeAck: {statuses: [0, 0]}} );
			gateway.drop();
			expect( s.events.map( e=>e.kind ) ).toEqual( ['dropped', 'ack', 'dropped'] );
			s.subscription.unsubscribe();
		} );
		it( 'sends nothing for a listener that goes while waiting, and tries no more', ()=>{
			const gateway = new HistGateway();
			const s = listen( gateway );
			gateway.receive( {requestId: s.requestId, histSubscribeAck: {statuses: [0, 0]}} );
			gateway.drop();
			s.subscription.unsubscribe();
			vi.advanceTimersByTime( 2*Gateway.histRetryDelay );
			expect( gateway.subscribes ).toHaveLength( 1 );
			expect( gateway.unsubscribes ).toHaveLength( 0 );
		} );
	} );
} );